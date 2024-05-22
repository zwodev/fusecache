/*
 * Copyright (c) 2024 Nils Zweiling
 *
 * This file is part of fusecache which is released under the MIT license.
 * See file LICENSE or go to https://github.com/zwodev/fusecache/tree/master/LICENSE
 * for full license details.
 */

#include "CacheManager.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <filesystem>

bool exec(std::string cmd, std::string& result) {
    std::array<char, 128> buffer;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return false;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return true;
}

CacheManager::CacheManager()
{
}

CacheManager::~CacheManager()
{
    stop();
}

bool CacheManager::canPartFileBeDeleted(const std::string& path) 
{
	// file is not cached yet
	if (access(path.c_str(), F_OK) >= 0) {
		struct stat sb;
		int res = lstat(path.c_str(), &sb);
		if (res == -1) {
			return true;
		}

		time_t now = time(0);
		double diff = difftime(now, sb.st_mtim.tv_sec);
		
		// part file has not been updated since > 2 min
		if (diff > 120.0) {
			return true;
		} 
	}

	return false;
}

int CacheManager::waitForFile(const char *path) 
{
	
    std::string partPath = partFilePath(path);
	if (access(partPath.c_str(), F_OK) >= 0) {
		float timeout_in_secs = 15.0f * 60.0f;
		int wait_in_secs = 30;
		int count = (int)(timeout_in_secs / (float)wait_in_secs);
		while (count > 0) {
			if (access(partPath.c_str(), F_OK) == -1) {
				return 0;
			}
			else if (canPartFileBeDeleted(partPath)) {
				remove(partPath.c_str());
				return 0;
			}
			count -= 1;
			sleep(wait_in_secs);
		}
		return -1;
	}

	return 0;
}

int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

int CacheManager::copyFile(const char *from, const char *to)
{
	const int BUF_SIZE = 20480;
	const double goalTime = ((double)BUF_SIZE) / (1024.0f * 1024.0f * m_maxUpBandwidth);
	
	clock_t start, end;	
    int fd_to, fd_from;
    char buf[BUF_SIZE];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0) {
        return -1;
	}

    std::string toPart = partFilePath(to);
    std::string dir = std::filesystem::path(toPart).parent_path().u8string();
	std::filesystem::create_directories(dir);

    fd_to = open(toPart.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd_to < 0)
        goto out_error;

    while (true)
    {
		start = clock();
		nread = read(fd_from, buf, sizeof buf);
		if (nread <= 0)
			break;

		end = clock();
		double time = ((double)(end - start)) / (double) CLOCKS_PER_SEC;
		double diff = goalTime - time;

        char *out_ptr = buf;
        ssize_t nwritten;

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);

		if (diff > 0) {
			long msecs = (long)(diff * 1000.0);
			msleep(msecs);
		}
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);

		printf("COPY SUCCESS - rename part file: %s\n", toPart.c_str());
		rename(toPart.c_str(), to);
        return 0;
    }

  out_error:
    saved_errno = errno;

    close(fd_from);
    if (fd_to >= 0) {
        close(fd_to);
	}

	printf("COPY ERROR - delete part file: %s\n", toPart.c_str());
    errno = saved_errno;
    return -1;
}

bool CacheManager::needsCopy(const std::string& path) 
{
	bool needs_copy = false;

    const char* from = origFilePath(path).c_str();
    const char* to = readCacheFilePath(path).c_str();
	

	// file is not cached yet
	if (access(to, F_OK) == -1) {
		needs_copy = true;
	}
	// file is cached but maybe there is a newer version
	else {
		struct stat sb_from;
		struct stat sb_to;
		int res_from = lstat(from, &sb_from);
		int res_to = lstat(to, &sb_to);
		if (res_from == -1 || res_to == -1) {
			return -1;
		}

		float diff = difftime(sb_to.st_mtim.tv_sec, sb_from.st_mtim.tv_sec);
		
		// is origin file newer or has different file size
		if (diff < 0 || (sb_from.st_size != sb_to.st_size)) {
			needs_copy = true;
		} 

	}

	return needs_copy;
}

int CacheManager::copyFileOnDemand(const char *from, const char *to) 
{
	std::lock_guard<std::mutex> guard(m_copyMutex);
	int res = 0;
	bool needs_copy = false;
	m_isCopying = true;

	// file is not cached yet
	if (access(to, F_OK) == -1) {
		needs_copy = true;
	}
	// file is cached but maybe there is a newer version
	else {
		struct stat sb_from;
		struct stat sb_to;
		int res_from = lstat(from, &sb_from);
		int res_to = lstat(to, &sb_to);
		if (res_from == -1 || res_to == -1) {
			m_isCopying = false;
			return -1;
		}

		float diff = difftime(sb_to.st_mtim.tv_sec, sb_from.st_mtim.tv_sec);
		
		// is origin file newer or has different file size
		if (diff < 0 || (sb_from.st_size != sb_to.st_size)) {
			needs_copy = true;
		} 

	}

	if (needs_copy) {
		printf("COPYING file from: %s to: %s\n", from, to);
		res = copyFile(from, to);
		if (access(to, F_OK) >= 0) {
			struct stat sb_from;
			struct stat sb_to;
			int res_from = lstat(from, &sb_from);
			int res_to = lstat(to, &sb_to);
			if (res_from == -1 || res_to == -1) {
				m_isCopying = false;
				return -1;
			}
			struct utimbuf tb;
			tb.actime = sb_from.st_atim.tv_sec;
			tb.modtime = sb_from.st_mtim.tv_sec;
			res = utime(to, &tb);
			if (res == -1) {
				m_isCopying = false;
				return -1;
			}
		}
		else {
			m_isCopying = false;
			return -1;
		}
	}

	m_isCopying = false;
	return res;
}

bool CacheManager::checkDependencies()
{
	std::string rsyncCommand = "rsync -V";
	std::string result;
	if (!exec(rsyncCommand, result)) {
		printf("ERROR - RSYNC not installed. \n");
		return false;
	}

	return true;
}

void CacheManager::createDirectories()
{
	std::string dir = std::filesystem::path(rootPath()).u8string();
	std::filesystem::create_directories(dir);

	dir = std::filesystem::path(readCacheDir()).u8string();
	std::filesystem::create_directories(dir);

	dir = std::filesystem::path(writeCacheDir()).u8string();
	std::filesystem::create_directories(dir);

	dir = std::filesystem::path(mountPoint()).u8string();
	std::filesystem::create_directories(dir);
}

void CacheManager::run()
{
    std::string rsyncCommand = "rsync -av ";
	if (m_maxDownBandwidth > 0) {
		int maxDown = (int)(m_maxDownBandwidth * 1024.0f);
		rsyncCommand += "--bwlimit=" + std::to_string(maxDown);
	}
	rsyncCommand += " ";
	rsyncCommand += "--exclude='*.part'";
	rsyncCommand += " ";
	rsyncCommand += writeCacheDir() + "/";
	rsyncCommand += " ";
	rsyncCommand += rootPath();

	while(m_isRunning) {
		std::string result;
		printf("RSYNC CMD: %s\n", rsyncCommand.c_str()); 
		if (exec(rsyncCommand, result)) {
			printf("RSYNC SUCCESS: %s\n", result.c_str());
		}
		else {
			printf("RSYNC ERROR: %s\n", result.c_str());
		}
        sleep(30);
    }
}

void CacheManager::start()
{
    m_isRunning = true;
    m_syncThread = std::thread(&CacheManager::run, this);
}

void CacheManager::stop()
{
    m_isRunning = false;
    m_syncThread.join();
}

int CacheManager::openFile(const char* filePath, int flags)
{   
    std::string origPath = origFilePath(filePath);
    std::string cachePath = readCacheFilePath(filePath);
    
	//printf("Flags: %i", flags);

	int ret;
    if (!(flags & O_NOATIME)) {
		ret = waitForFile(cachePath.c_str());
		if (ret == -1) {
			return -EACCES;
		}

		ret = copyFileOnDemand(origPath.c_str(), cachePath.c_str());
		if (ret == -1) {
			return -EACCES;
		}

		ret = open(cachePath.c_str(), flags);
		if (ret == -1) {
			return -errno;
		}
	}
	else {
		ret = open(origPath.c_str(), flags);
		if (ret == -1)
			return -errno;
	}
    
    return ret;
}

int CacheManager::closeFile(int vfh)
{
    close(vfh);
    return 0;
}

int CacheManager::readFile(int vfh, char* buf, size_t size, off_t offset)
{
	int res = pread(vfh, buf, size, offset);
	if (res == -1)
		res = -errno;

		
	return res;
}

int CacheManager::createFile(const char* filePath, mode_t mode, int flags)
{   
    std::string cachePath = writeCacheFilePath(filePath);
    std::string dir = std::filesystem::path(filePath).parent_path().u8string();
	std::filesystem::create_directories(dir);

	int res = open(cachePath.c_str(), flags, mode);
	if (res == -1)
		res = -errno;

	return res;
}

int CacheManager::writeFile(int vfh, const char* buf, size_t size, off_t offset)
{
	int res = pwrite(vfh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

std::string CacheManager::origFilePath(const std::string& filePath)
{
    std::string newFilePath = m_rootPath + filePath;
    return newFilePath;
}

std::string CacheManager::readCacheFilePath(const std::string& filePath)
{
    std::string newFilePath = m_readCacheDir + filePath;
    return newFilePath;
}

std::string CacheManager::writeCacheFilePath(const std::string& filePath)
{
    std::string newFilePath = m_writeCacheDir + filePath;
    return newFilePath;
}

std::string CacheManager::partFilePath(const std::string& filePath)
{
    std::string newFilePath = filePath + ".part";
    return newFilePath;
}

const std::string& CacheManager::rootPath()
{
    return m_rootPath;
}

const std::string& CacheManager::readCacheDir()
{
    return m_readCacheDir;
}

const std::string& CacheManager::writeCacheDir()
{
    return m_writeCacheDir;
}

const std::string& CacheManager::mountPoint()
{
    return m_mountPoint;
}

void CacheManager::setRootPath(const std::string& rootPath)
{
    m_rootPath = rootPath;
}

void CacheManager::setReadCacheDir(const std::string& readCacheDir)
{
    m_readCacheDir = readCacheDir;
}

void CacheManager::setWriteCacheDir(const std::string& writeCacheDir)
{
    m_writeCacheDir = writeCacheDir;
}

void CacheManager::setMountPoint(const std::string& mountPoint)
{
    m_mountPoint = mountPoint;
}

void CacheManager::setMaxUpBandwidth(float mbPerSecond)
{
    m_maxUpBandwidth = mbPerSecond;
}

void CacheManager::setMaxDownBandwidth(float mbPerSecond)
{
    m_maxDownBandwidth = mbPerSecond;
}
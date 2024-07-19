/*
 * Copyright (c) 2024 Nils Zweiling
 *
 * This file is part of fusecache which is released under the MIT license.
 * See file LICENSE or go to https://github.com/zwodev/fusecache/tree/master/LICENSE
 * for full license details.
 */


#define FUSE_USE_VERSION 31

#include <sys/types.h>
#include <utime.h>
#include <time.h>
#include <stdbool.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>

#include <filesystem>

#include "Helper.h"
#include "Log.h"
#include "CacheManager.h"

static fuse_fill_dir_flags fill_dir_plus = (fuse_fill_dir_flags ) 0;

CacheManager* cache_manager = nullptr;
Log* g_log = nullptr;

static void *fc_init(struct fuse_conn_info *conn,
		      struct fuse_config *cfg)
{
	(void) conn;
	cfg->use_ino = 1;
	cfg->entry_timeout = 0;
	cfg->attr_timeout = 0;
	cfg->negative_timeout = 0;

	return NULL;
}

static int fc_getattr(const char *path, struct stat *stbuf,
		       struct fuse_file_info *fi)
{
	//g_log->debug(formatStr("fc_getattr: %s", path));

	(void) fi;
	int res;

	std::string orig_path = cache_manager->origFilePath(path);
	res = lstat(orig_path.c_str(), stbuf);
	if (res == -1) {
		std::string cache_path = cache_manager->writeCacheFilePath(path);
		res = lstat(cache_path.c_str(), stbuf);
	}

	if (res == -1)
		return -errno;

	return 0;
}

static int fc_access(const char *path, int mask)
{
	g_log->debug(formatStr("fc_access: %s", path));
	
	int res;
	std::string orig_path = cache_manager->origFilePath(path);
	res = access(orig_path.c_str(), mask);
	if (res == -1) {
		std::string cache_path = cache_manager->writeCacheFilePath(path);
		res = access(cache_path.c_str(), mask);
	}

	if (res == -1)
		return -errno;

	return 0;
}

static int fc_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi,
		       enum fuse_readdir_flags flags)
{
	//g_log->debug(formatStr("fc_readdir: %s", path));

	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;
	(void) flags;

	std::string orig_path = cache_manager->origFilePath(path).c_str();

	dp = opendir(orig_path.c_str());
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0, fill_dir_plus))
			break;
	}

	closedir(dp);
	return 0;
}

static int fc_mkdir(const char *path, mode_t mode)
{
	g_log->debug(formatStr("fc_mkdir: %s", path));

	std::string cache_path = cache_manager->writeCacheFilePath(path);
	int res = mkdir(cache_path.c_str(), mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int fc_unlink(const char *path)
{
	g_log->debug(formatStr("fc_unlink: %s", path));

	int res = unlink(cache_manager->writeCacheFilePath(path).c_str());
	if (res == -1)
		return -errno;

	return 0;
}

static int fc_rmdir(const char *path)
{
	g_log->debug(formatStr("fc_rmdir: %s", path));
	
	int res;
	std::string orig_path = cache_manager->origFilePath(path);
	res = rmdir(orig_path.c_str());
	if (res == -1)
		return -errno;

	return 0;
}

static int fc_rename(const char *from, const char *to, unsigned int flags)
{
	if (flags)
		return -EINVAL;

	std::string cache_path_from = cache_manager->writeCacheFilePath(from);
	std::string cache_path_to = cache_manager->writeCacheFilePath(to);

	int res = rename(cache_path_from.c_str(), cache_path_to.c_str());
	if (res == -1)
		return -errno;

	return 0;
}

static int fc_chmod(const char *path, mode_t mode,
		     struct fuse_file_info *fi)
{
	(void) fi;

	std::string cache_path = cache_manager->writeCacheFilePath(path);
	int res = chmod(cache_path.c_str(), mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int fc_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
	(void) fi;

	std::string orig_path = cache_manager->origFilePath(path);
	int res = lchown(orig_path.c_str(), uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int fc_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	g_log->debug(formatStr("fc_create: %s", path));
	
	int res = cache_manager->createFile(path, mode, fi->flags);
	
	if (res < 0) {
	 	return res;
	}

	fi->fh = res;
	return 0;
}

static int fc_open(const char *path, struct fuse_file_info *fi)
{
	g_log->debug(formatStr("fc_open: %s", path));

	int res = cache_manager->openFile(path, fi->flags);
	if (res < 0) {
		g_log->debug(formatStr("ERROR OPENING FILE - FLAGS: %d", fi->flags));
	 	return res;
	}

	fi->fh = res;

	return 0;
}

static int fc_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int res = cache_manager->readFile(fi->fh, buf, size, offset);

	return res;
}

static int fc_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int res = cache_manager->writeFile(fi->fh, buf, size, offset);
	return res;
}

static int fc_statfs(const char *path, struct statvfs *stbuf)
{
	g_log->debug(formatStr("fc_statfs: %s", path));

	int res;

	std::string orig_path = cache_manager->origFilePath(path);
	res = statvfs(orig_path.c_str(), stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int fc_release(const char *path, struct fuse_file_info *fi)
{
	g_log->debug(formatStr("fc_release: %s", path));

	(void) path;
	
	int res = cache_manager->closeFile(fi->fh);
	return res;
}

static off_t fc_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi)
{
	int fd;
	off_t res;

	std::string orig_path = cache_manager->origFilePath(path);

	if (fi == NULL)
		fd = open(orig_path.c_str(), O_RDONLY);
	else
		fd = fi->fh;

	if (fd == -1)
		return -errno;

	res = lseek(fd, off, whence);
	if (res == -1)
		res = -errno;

	if (fi == NULL)
		close(fd);
	return res;
}

static int fc_truncate(const char *path, off_t size,
                        struct fuse_file_info *fi)
{
	g_log->debug(formatStr("fc_truncate: %s", path));
	
	std::string cache_path = cache_manager->writeCacheFilePath(path);

	int res;
	if (fi != NULL)
		res = ftruncate(fi->fh, size);
	else
		res = truncate(cache_path.c_str(), size);
	if (res == -1)
		return -errno;
 

    return 0;
}

static int fc_fsync(const char *path, int isdatasync,
                     struct fuse_file_info *fi)
{
	g_log->debug(formatStr("fc_sync: %s", path));

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

static void assign_operations(fuse_operations &op) {
	op.init     = fc_init;
	op.getattr	= fc_getattr;
	op.access	= fc_access;
	op.readdir	= fc_readdir;
	op.mkdir	= fc_mkdir;
	op.unlink	= fc_unlink;
	op.rmdir	= fc_rmdir;
	op.rename	= fc_rename;
	op.chmod	= fc_chmod;
	op.chown	= fc_chown;
	op.open		= fc_open;
	op.create 	= fc_create;
	op.read		= fc_read;
	op.write	= fc_write;
	op.statfs	= fc_statfs;
	op.release	= fc_release;
	op.lseek	= fc_lseek;
	op.truncate = fc_truncate;
	op.fsync	= fc_fsync;
}

int main(int argc, char *argv[])
{
	fuse_operations fc_oper {};
    assign_operations(fc_oper);

	enum { MAX_ARGS = 10 };
	int new_argc;
	char *new_argv[MAX_ARGS];

	umask(0);

	std::string name;
	
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-name") == 0 && (i+1 < argc)) {
			try
			{
				name = std::string(argv[i+1]);	
			}
			catch (...)
			{
				continue;
			}
		}
	}

	char path[512];
	getcwd(path, 512);

	if (!name.empty()) {
		name = "/" + name;
		std::string subPath = std::string(path) + name;
		std::string dir = std::filesystem::path(subPath).u8string();
		std::filesystem::create_directories(dir);
	}

	std::string rootPath = std::string(path) + name + "/orig";
	std::string readCacheDir = std::string(path) + name + "/cache";
	std::string writeCacheDir = std::string(path) + name + "/cache";
	std::string mountPoint = std::string(path) + name + "/mnt";

	g_log = new Log(writeCacheDir + "/fusecache.log");
	cache_manager = new CacheManager(g_log);
	if (!cache_manager->checkDependencies()) {
		delete cache_manager;
		return -1;
	}

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-readcacheonly") == 0) {
			try
			{
				cache_manager->setReadCacheOnly(true);	
			}
			catch (...)
			{
				continue;
			}
		}
		else if (strcmp(argv[i], "-ulimit") == 0 && (i+1 < argc)) {
			try
			{
				std::string valueString(argv[i+1]);
				float limitInMBPerSec = std::stof(valueString);
				cache_manager->setMaxUpBandwidth(limitInMBPerSec);	
			}
			catch (...)
			{
				continue;
			}
		}
		else if (strcmp(argv[i], "-dlimit") == 0 && (i+1 < argc)) {
			try
			{
				std::string valueString(argv[i+1]);
				float limitInMBPerSec = std::stof(valueString);
				cache_manager->setMaxDownBandwidth(limitInMBPerSec);	
			}
			catch (...)
			{
				continue;
			}
		}
	}

	cache_manager->setRootPath(rootPath);
	cache_manager->setReadCacheDir(readCacheDir);
	cache_manager->setWriteCacheDir(writeCacheDir);
	cache_manager->setMountPoint(mountPoint);
	cache_manager->createDirectories();
	cache_manager->start();

	fill_dir_plus = FUSE_FILL_DIR_PLUS;
	new_argv[0] = argv[0];
	new_argv[1] = (char*)"-f";	
	new_argv[2] = (char*)"-o";	
	new_argv[3] = (char*)"allow_other";	
	new_argv[4] = (char*)"./mnt";
	new_argc = 5;
	
	

	int ret = fuse_main(new_argc, new_argv, &fc_oper, NULL);
	delete cache_manager;
	delete g_log;
	return  ret;
}
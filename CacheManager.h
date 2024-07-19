/*
 * Copyright (c) 2024 Nils Zweiling
 *
 * This file is part of fusecache which is released under the MIT license.
 * See file LICENSE or go to https://github.com/zwodev/fusecache/tree/master/LICENSE
 * for full license details.
 */

#pragma once

#include <mutex>
#include <thread>
#include <string>
#include <map>
#include <vector>
#include <list>

#include "Log.h"

class CacheManager
{
    
public:
    CacheManager(Log* log);
    ~CacheManager();

private:
    bool canPartFileBeDeleted(const std::string& path);
    int waitForFile(const char *path);
    bool needsCopy(const std::string& path);
    int copyFile(const char *from, const char *to);
    int copyFileOnDemand(const char *from, const char *to);

public:
    bool checkDependencies();
    void createDirectories();
    
    void run();
    void start();
    void stop();

    int openFile(const char* filePath, int flags);
    int closeFile(int id);
    int readFile(int id, char* buf, size_t size, off_t offset);
    int createFile(const char* filePath, mode_t mode, int flags);
    int writeFile(int id, const char* buf, size_t size, off_t offset);

    std::string origFilePath(const std::string& filePath);
    std::string readCacheFilePath(const std::string& filePath);
    std::string writeCacheFilePath(const std::string& filePath);
    std::string partFilePath(const std::string& filePath);

    const std::string& rootPath();
    const std::string& readCacheDir();
    const std::string& writeCacheDir();
    const std::string& mountPoint();

    void setRootPath(const std::string& rootPath);
    void setReadCacheDir(const std::string& readCacheDir);
    void setWriteCacheDir(const std::string& writeCacheDir);
    void setMountPoint(const std::string& mountPoint);
    void setName(const std::string& name);
    void setReadCacheOnly(bool enabled);
    void setMaxUpBandwidth(float mbPerSecond);
    void setMaxDownBandwidth(float mbPerSecond);

private:
    Log* m_log = nullptr;
    std::mutex m_copyMutex;
    std::thread m_syncThread;
    std::string m_name;
    bool m_readCacheOnly = false;
    bool m_isRunning = false;
    bool m_isCopying= false;
    float m_maxUpBandwidth = 1.0f;
    float m_maxDownBandwidth = 1.0f;
    std::string m_rootPath;
    std::string m_readCacheDir;
    std::string m_writeCacheDir;
    std::string m_mountPoint;
};
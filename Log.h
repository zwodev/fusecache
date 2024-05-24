/*
 * Copyright (c) 2024 Nils Zweiling
 *
 * This file is part of fusecache which is released under the MIT license.
 * See file LICENSE or go to https://github.com/zwodev/fusecache/tree/master/LICENSE
 * for full license details.
 */

#pragma once

#include <ctime> 
#include <fstream> 
#include <iostream> 
#include <sstream> 

class Log { 
    
public:
    Log () {}

    Log(const std::string& filename) 
    { 
        m_logFile.open(filename, std::ios::app); 
        if (!m_logFile.is_open()) { 
            std::cerr << "Error opening log file." << std::endl; 
        } 
    } 
  
    ~Log()
    { 
        m_logFile.close(); 
    } 
  
    void debug(const std::string& message) {
        writeLog("DEBUG", message);
    }

    void info(const std::string& message) {
        writeLog("INFO", message);
    }

    void warning(const std::string& message) {
        writeLog("WARNING", message);
    }

    void error(const std::string& message) {
        writeLog("ERROR", message);
    }

private:
    void writeLog(const std::string& level, const std::string& message) 
    { 
        time_t now = time(0); 
        tm* timeinfo = localtime(&now); 
        char timestamp[20]; 
        strftime(timestamp, sizeof(timestamp), 
                 "%Y-%m-%d %H:%M:%S", timeinfo); 
  
        std::ostringstream logEntry; 
        logEntry << "[" << timestamp << "] "
                 << level << ": " << message 
                 << std::endl; 
  
        std::cout << logEntry.str(); 
  
        if (m_logFile.is_open()) { 
            m_logFile << logEntry.str(); 
            m_logFile 
                .flush();
        }
    }

private:
    std::ofstream m_logFile;
};
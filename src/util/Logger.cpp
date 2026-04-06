// src/util/Logger.cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Logger.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <cstdarg>

bool Logger::initialized_ = false;
bool Logger::earlyInitDone_ = false;
std::filesystem::path Logger::logDir_;
std::ofstream Logger::logFile_;
std::mutex Logger::mutex_;

/*static*/ void Logger::earlyInit() {
    earlyInitDone_ = true;
}

/*static*/ void Logger::fullInit(const std::filesystem::path& logDir) {
    std::lock_guard<std::mutex> lock(mutex_);

    logDir_ = logDir;
    std::filesystem::create_directories(logDir);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf;

#ifdef _WIN32
    localtime_s(&tmBuf, &time);
#else
    localtime_r(&time, &tmBuf);
#endif

    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S.log", &tmBuf);

    logFile_.open(logDir_ / buf, std::ios::app);
    if (logFile_.is_open()) {
        initialized_ = true;
    }
}

/*static*/ void Logger::debug(const std::string& format, const std::string& arg) {
    if (arg.empty()) {
        log(LogLevel::Debug, format);
    } else {
        std::string message = format;
        size_t pos = message.find("{}");
        if (pos != std::string::npos) {
            message.replace(pos, 2, arg);
        }
        log(LogLevel::Debug, message);
    }
}

/*static*/ void Logger::info(const std::string& format, const std::string& arg) {
    if (arg.empty()) {
        log(LogLevel::Info, format);
    } else {
        std::string message = format;
        size_t pos = message.find("{}");
        if (pos != std::string::npos) {
            message.replace(pos, 2, arg);
        }
        log(LogLevel::Info, message);
    }
}

/*static*/ void Logger::warning(const std::string& format, const std::string& arg) {
    if (arg.empty()) {
        log(LogLevel::Warning, format);
    } else {
        std::string message = format;
        size_t pos = message.find("{}");
        if (pos != std::string::npos) {
            message.replace(pos, 2, arg);
        }
        log(LogLevel::Warning, message);
    }
}

/*static*/ void Logger::error(const std::string& format, const std::string& arg) {
    if (arg.empty()) {
        log(LogLevel::Error, format);
    } else {
        std::string message = format;
        size_t pos = message.find("{}");
        if (pos != std::string::npos) {
            message.replace(pos, 2, arg);
        }
        log(LogLevel::Error, message);
    }
}

/*static*/ void Logger::fatal(const std::string& format, const std::string& arg) {
    if (arg.empty()) {
        log(LogLevel::Fatal, format);
    } else {
        std::string message = format;
        size_t pos = message.find("{}");
        if (pos != std::string::npos) {
            message.replace(pos, 2, arg);
        }
        log(LogLevel::Fatal, message);
    }
}

/*static*/ void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* levelStr = "";
    switch (level) {
        case LogLevel::Debug:   levelStr = "DEBUG";   break;
        case LogLevel::Info:    levelStr = "INFO";    break;
        case LogLevel::Warning: levelStr = "WARNING"; break;
        case LogLevel::Error:   levelStr = "ERROR";   break;
        case LogLevel::Fatal:   levelStr = "FATAL";   break;
    }

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &time);
#else
    localtime_r(&time, &tmBuf);
#endif

    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmBuf);

    std::string logLine = std::string(timeBuf) + " [" + levelStr + "] " + message;

    if (initialized_) {
        writeToFile(logLine);
    } else {
        writeToStderr(logLine);
    }
}

/*static*/ void Logger::writeToFile(const std::string& message) {
    if (logFile_.is_open()) {
        logFile_ << message << std::endl;
        logFile_.flush();
    }
}

/*static*/ void Logger::writeToStderr(const std::string& message) {
    std::cerr << message << std::endl;
}

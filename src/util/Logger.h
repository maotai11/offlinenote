// src/util/Logger.h
// 結構化本地日誌
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <filesystem>
#include <mutex>
#include <fstream>
#include <memory>

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

class Logger {
public:
    // 早期初始化（只寫 stderr）
    static void earlyInit();

    // 完整初始化（寫到檔案）
    static void fullInit(const std::filesystem::path& logDir);

    // 日誌函式
    static void debug(const std::string& format, const std::string& arg = "");
    static void info(const std::string& format, const std::string& arg = "");
    static void warning(const std::string& format, const std::string& arg = "");
    static void error(const std::string& format, const std::string& arg = "");
    static void fatal(const std::string& format, const std::string& arg = "");

private:
    static void log(LogLevel level, const std::string& message);
    static void writeToFile(const std::string& message);
    static void writeToStderr(const std::string& message);

    static bool initialized_;
    static std::filesystem::path logDir_;
    static std::ofstream logFile_;
    static std::mutex mutex_;
    static bool earlyInitDone_;
};

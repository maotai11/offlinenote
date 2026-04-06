// src/util/FileUtils.h
// 檔案操作工具
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>
#include <string>

namespace FileUtils {
    std::filesystem::path getExecutablePath();
    std::filesystem::path getExecutableDir();
    bool fileExists(const std::filesystem::path& path);
    bool directoryExists(const std::filesystem::path& path);
    bool createDirectories(const std::filesystem::path& path);
    std::string readFile(const std::filesystem::path& path);
    bool writeFile(const std::filesystem::path& path, const std::string& content);
}

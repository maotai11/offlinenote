// src/util/FileUtils.cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "FileUtils.h"
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace FileUtils {

std::filesystem::path getExecutablePath() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path);
#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        return std::filesystem::path(path);
    }
    return {};
#else
    char path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    if (count != -1) {
        path[count] = '\0';
        return std::filesystem::path(path);
    }
    return {};
#endif
}

std::filesystem::path getExecutableDir() {
    return getExecutablePath().parent_path();
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool directoryExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

bool createDirectories(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::create_directories(path, ec);
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
}

bool writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    return true;
}

} // namespace FileUtils

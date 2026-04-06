// src/application/PathManager.cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "PathManager.h"
#include "../util/FileUtils.h"
#include "../util/Logger.h"

#include <vector>
#include <string>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <sys/types.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

PathManager& PathManager::instance() {
    static PathManager instance;
    return instance;
}

void PathManager::initialize() {
    if (initialized_) return;

    exeDir_ = FileUtils::getExecutableDir();
    portableMode_ = detectPortableMode();

    Logger::info("PathManager initialized, portable=" + std::string(portableMode_ ? "true" : "false"));
    Logger::info("exeDir=" + exeDir_.string());

    initialized_ = true;
}

bool PathManager::isPortableMode() const {
    return portableMode_;
}

std::filesystem::path PathManager::getResourceDir() const {
    if (portableMode_) {
        return exeDir_ / "resources";
    }
#ifdef _WIN32
    return exeDir_ / "resources";
#elif defined(__APPLE__)
    return exeDir_ / ".." / "Resources" / "resources";
#else
    return "/usr/share/offlinenote/resources";
#endif
}

std::filesystem::path PathManager::getUserDataDir() const {
    if (portableMode_) {
        return exeDir_ / "data";
    }
#ifdef _WIN32
    const wchar_t* appData = _wgetenv(L"APPDATA");
    if (appData) return std::filesystem::path(appData) / "offlinenote";
    return exeDir_ / "data";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    if (home) return std::filesystem::path(home) / "Library" / "Application Support" / "offlinenote";
    return exeDir_ / "data";
#else
    const char* home = getenv("HOME");
    if (home) return std::filesystem::path(home) / ".local" / "share" / "offlinenote";
    return exeDir_ / "data";
#endif
}

std::filesystem::path PathManager::getConfigFile() const {
    return getUserDataDir() / "config.ini";
}

std::filesystem::path PathManager::getLogDir() const {
    if (portableMode_) {
        return exeDir_ / "data" / "logs";
    }
#ifdef _WIN32
    return getUserDataDir() / "logs";
#elif defined(__APPLE__)
    return getUserDataDir() / "logs";
#else
    return getUserDataDir() / "logs";
#endif
}

std::filesystem::path PathManager::getCacheDir() const {
    if (portableMode_) {
        return exeDir_ / "data" / "cache";
    }
#ifdef _WIN32
    const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
    if (localAppData) return std::filesystem::path(localAppData) / "offlinenote" / "cache";
    return getUserDataDir() / "cache";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    if (home) return std::filesystem::path(home) / "Library" / "Caches" / "offlinenote";
    return getUserDataDir() / "cache";
#else
    const char* cacheHome = getenv("XDG_CACHE_HOME");
    if (cacheHome) return std::filesystem::path(cacheHome) / "offlinenote";
    const char* home = getenv("HOME");
    if (home) return std::filesystem::path(home) / ".cache" / "offlinenote";
    return getUserDataDir() / "cache";
#endif
}

std::filesystem::path PathManager::getDocumentsDir() const {
    return getUserDataDir() / "documents";
}

bool PathManager::verifyResourceDirectory(std::string& outError) const {
    auto resDir = getResourceDir();
    if (!FileUtils::directoryExists(resDir)) {
        outError = "Resource directory not found: " + resDir.string();
        return false;
    }

    const std::vector<std::string> requiredSubdirs = {"fonts", "icons", "translations"};
    for (const auto& subdir : requiredSubdirs) {
        if (!FileUtils::directoryExists(resDir / subdir)) {
            outError = "Required resource subdirectory missing: " + (resDir / subdir).string();
            return false;
        }
    }

    return true;
}

std::filesystem::path PathManager::getExecutableDir() const {
    return exeDir_;
}

bool PathManager::detectPortableMode() const {
    return FileUtils::fileExists(exeDir_ / "portable.flag");
}

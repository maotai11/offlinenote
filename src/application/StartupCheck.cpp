// src/application/StartupCheck.cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "StartupCheck.h"
#include "PathManager.h"
#include "../util/Logger.h"
#include "../util/FileUtils.h"

#include <gtk/gtk.h>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <vector>
#include <utility>

namespace fs = std::filesystem;

void StartupCheck::run()
{
    Logger::info("Running startup checks...");
    checkGtkVersion();
    checkResourceDirectory();
    checkFontFallback();
    createUserDirectories();
    checkWritableUserDataDir();
    Logger::info("All startup checks passed.");
}

void StartupCheck::checkGtkVersion()
{
    if (gtk_get_major_version() < 3 ||
        (gtk_get_major_version() == 3 && gtk_get_minor_version() < 22))
    {
        throw std::runtime_error(
            "GTK version too old. OfflineNote requires GTK >= 3.22.\n"
            "Found: " + std::to_string(gtk_get_major_version()) + "." +
            std::to_string(gtk_get_minor_version()) + "\n"
            "Please reinstall OfflineNote."
        );
    }
}

void StartupCheck::checkResourceDirectory()
{
    auto resDir = PathManager::instance().getResourceDir();

    if (!FileUtils::directoryExists(resDir)) {
        Logger::error("Resource directory not found at: {}", resDir.string());
        throw std::runtime_error(
            "Application resources could not be found.\n\n"
            "This usually indicates an incomplete installation.\n"
            "Please reinstall OfflineNote."
        );
    }

    const std::vector<std::pair<fs::path, std::string>> requiredSubdirs = {
        { resDir / "fonts",        "Font resources" },
        { resDir / "icons",        "Icon resources" },
        { resDir / "translations", "Translation files" },
    };

    for (const auto& [dir, name] : requiredSubdirs) {
        if (!FileUtils::directoryExists(dir)) {
            Logger::error("Required resource subdirectory missing: {}", dir.string());
            throw std::runtime_error(
                name + " could not be found.\n\n"
                "This usually indicates an incomplete installation.\n"
                "Please reinstall OfflineNote."
            );
        }
    }
}

void StartupCheck::checkFontFallback()
{
    auto fontDir = PathManager::instance().getResourceDir() / "fonts" / "NotoSans";

    if (!FileUtils::directoryExists(fontDir)) {
        Logger::warning("Bundled fallback font not found at: {}", fontDir.string());
    }
}

void StartupCheck::createUserDirectories()
{
    const std::vector<fs::path> userDirs = {
        PathManager::instance().getUserDataDir(),
        PathManager::instance().getLogDir(),
        PathManager::instance().getCacheDir(),
    };

    for (const auto& dir : userDirs) {
        try {
            FileUtils::createDirectories(dir);
        }
        catch (const std::exception& e) {
            throw std::runtime_error(
                "Cannot create data directory.\n\n"
                "Please check that you have write permissions to your home directory."
            );
        }
    }
}

void StartupCheck::checkWritableUserDataDir()
{
    auto dataDir = PathManager::instance().getUserDataDir();
    auto testFile = dataDir / ".write_test";

    {
        std::ofstream f(testFile);
        if (!f.is_open()) {
            Logger::error("Cannot write to user data directory: {}", dataDir.string());
            throw std::runtime_error(
                "Cannot save data to your user data directory.\n\n"
                "Please check that you have write permissions and "
                "sufficient disk space."
            );
        }
    }

    std::error_code ec;
    fs::remove(testFile, ec);
}

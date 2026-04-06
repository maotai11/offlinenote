// src/application/PathManager.h
// 路徑統一管理（portable/installed）
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>
#include <string>

class PathManager {
public:
    static PathManager& instance();

    void initialize();
    bool isPortableMode() const;

    std::filesystem::path getResourceDir()    const;
    std::filesystem::path getUserDataDir()    const;
    std::filesystem::path getConfigFile()     const;
    std::filesystem::path getLogDir()         const;
    std::filesystem::path getCacheDir()       const;
    std::filesystem::path getDocumentsDir()   const;

    bool verifyResourceDirectory(std::string& outError) const;

private:
    PathManager() = default;

    std::filesystem::path getExecutableDir() const;
    bool detectPortableMode() const;

    std::filesystem::path exeDir_;
    bool portableMode_ = false;
    bool initialized_ = false;
};

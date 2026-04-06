// src/util/PathValidator.cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "PathValidator.h"
#include "Logger.h"
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

PathValidationResult PathValidator::validatePath(
    const fs::path& inputPath,
    PathValidationPolicy policy,
    const fs::path& allowedRoot)
{
    PathValidationResult result;
    const std::string pathStr = inputPath.string();

    // NULL byte 檢查
    if (containsNullByte(pathStr)) {
        Logger::warning("PathValidator: BLOCKED path with NULL byte");
        result.reason = "Path contains null bytes";
        return result;
    }

    // 路徑長度限制
    if (pathStr.size() > MAX_PATH_LENGTH) {
        Logger::warning("PathValidator: BLOCKED overly long path ({} chars)", std::to_string(pathStr.size()));
        result.reason = "Path too long";
        return result;
    }

    // 路徑遍歷元件偵測
    if (hasTraversalComponents(inputPath)) {
        Logger::warning("PathValidator: Suspicious traversal components in path");
    }

    // 解析 canonical 路徑
    std::error_code ec;
    fs::path canonical = fs::canonical(inputPath, ec);

    if (ec) {
        result.reason = "Cannot resolve path: " + ec.message();
        return result;
    }

    // 目錄限制檢查
    if (policy == PathValidationPolicy::RestrictToDirectory) {
        if (allowedRoot.empty()) {
            result.reason = "Internal error: RestrictToDirectory requires allowedRoot";
            return result;
        }

        fs::path canonicalRoot = fs::canonical(allowedRoot, ec);
        if (ec) {
            result.reason = "Cannot resolve allowed root: " + ec.message();
            return result;
        }

        const std::string canonStr = canonical.string();
        const std::string rootStr = canonicalRoot.string();

        if (canonStr.find(rootStr) != 0 ||
            (canonStr.size() > rootStr.size() &&
             canonStr[rootStr.size()] != fs::path::preferred_separator))
        {
            Logger::warning("PathValidator: BLOCKED path outside allowed root");
            result.reason = "Path is outside the allowed directory";
            return result;
        }
    }

    result.valid = true;
    result.canonical = canonical;
    return result;
}

PathValidationResult PathValidator::validatePdfPath(
    const fs::path& path,
    bool fromUserFileDialog)
{
    if (fromUserFileDialog) {
        return validatePath(path, PathValidationPolicy::AllowAnyAbsolute);
    }
    else {
        Logger::debug("PathValidator: Validating PDF path from .onote file");
        return validatePath(path, PathValidationPolicy::AllowAnyAbsolute);
    }
}

PathValidationResult PathValidator::validateEmbeddedResourcePath(
    const fs::path& path,
    const fs::path& resourceRoot)
{
    return validatePath(path, PathValidationPolicy::RestrictToDirectory, resourceRoot);
}

bool PathValidator::containsNullByte(const std::string& str) {
    return str.find('\0') != std::string::npos;
}

bool PathValidator::hasTraversalComponents(const fs::path& path) {
    for (const auto& component : path) {
        if (component == "..") return true;
    }
    return false;
}

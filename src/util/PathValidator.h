// src/util/PathValidator.h
// 路徑安全驗證工具
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>
#include <string>

enum class PathValidationPolicy {
    AllowAnyPath,          // Allow any path (will be canonicalized to absolute)
    RequireAbsolutePath,   // Require absolute path (deprecated, same as AllowAnyPath but clearer name)
    RestrictToDirectory,   // Must be within allowedRoot
};

struct PathValidationResult {
    bool valid = false;
    std::string reason;
    std::filesystem::path canonical;
};

class PathValidator {
public:
    static PathValidationResult validatePath(
        const std::filesystem::path& inputPath,
        PathValidationPolicy policy = PathValidationPolicy::AllowAnyPath,
        const std::filesystem::path& allowedRoot = {}
    );

    static PathValidationResult validatePdfPath(
        const std::filesystem::path& path,
        bool fromUserFileDialog
    );

    static PathValidationResult validateEmbeddedResourcePath(
        const std::filesystem::path& path,
        const std::filesystem::path& resourceRoot
    );

private:
    static bool containsNullByte(const std::string& str);
    static bool hasTraversalComponents(const std::filesystem::path& path);
    static constexpr size_t MAX_PATH_LENGTH = 4096;
};

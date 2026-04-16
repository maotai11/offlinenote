// SPDX-License-Identifier: GPL-2.0-or-later

#include "LegacyNoteResourceHelper.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace fs = std::filesystem;

namespace {

std::string sanitizeComponent(std::string_view component) {
    std::string out;
    out.reserve(component.size());
    for (char rawChar : component) {
        const unsigned char ch = static_cast<unsigned char>(rawChar);
        if (ch < 32) {
            continue;
        }
        switch (ch) {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            break;
        default:
            out.push_back(static_cast<char>(ch));
            break;
        }
    }
    return out;
}

bool isWithinRoot(const fs::path& candidate, const fs::path& root) {
    const std::string candidateStr = candidate.generic_u8string();
    const std::string rootStr = root.generic_u8string();
    if (candidateStr == rootStr) {
        return true;
    }
    return candidateStr.size() > rootStr.size() &&
           candidateStr.compare(0, rootStr.size(), rootStr) == 0 &&
           candidateStr[rootStr.size()] == '/';
}

fs::path canonicalOrEmpty(const fs::path& path) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(path, ec);
    if (ec) {
        return {};
    }
    return canonical;
}

std::string defaultResourceName(const fs::path& resourceRef) {
    const std::string fileName = sanitizeComponent(resourceRef.filename().generic_u8string());
    if (!fileName.empty()) {
        return fileName;
    }
    return "resource.bin";
}

fs::path uniqueAssetPath(const fs::path& assetDir, const fs::path& sourcePath) {
    const std::string stem = sanitizeComponent(sourcePath.stem().generic_u8string());
    const std::string ext = sanitizeComponent(sourcePath.extension().generic_u8string());
    const std::string baseStem = stem.empty() ? "resource" : stem;

    fs::path candidate = assetDir / (baseStem + ext);
    if (!fs::exists(candidate)) {
        return candidate;
    }

    for (int index = 1; index < 10000; ++index) {
        candidate = assetDir / (baseStem + "_" + std::to_string(index) + ext);
        if (!fs::exists(candidate)) {
            return candidate;
        }
    }

    return assetDir / (baseStem + "_overflow" + ext);
}

fs::path resolveSourcePath(const fs::path& resourceRef, const fs::path& sourceNotePath) {
    if (resourceRef.empty()) {
        return {};
    }

    if (resourceRef.is_absolute()) {
        return resourceRef;
    }

    if (!sourceNotePath.empty()) {
        return LegacyNoteResourceHelper::resolveForNote(sourceNotePath, resourceRef.generic_u8string());
    }

    return resourceRef;
}

bool hasUnsafeStoredPath(std::string_view rawPath) {
    const fs::path input{std::string(rawPath)};
    if (input.has_root_name() || input.has_root_directory()) {
        return true;
    }

    for (const fs::path& componentPath : input) {
        const std::string component = componentPath.generic_u8string();
        if (component == "." || component == "..") {
            return true;
        }
    }

    return false;
}

} // namespace

namespace LegacyNoteResourceHelper {

std::string sanitizeStoredPath(std::string_view rawPath) {
    fs::path normalized;
    const fs::path input{std::string(rawPath)};

    for (const fs::path& componentPath : input) {
        const std::string component = componentPath.generic_u8string();
        if (component.empty() || component == "/" || component == "\\" || component == "." || component == "..") {
            continue;
        }

        const std::string safeComponent = sanitizeComponent(component);
        if (safeComponent.empty()) {
            continue;
        }

        normalized /= fs::u8path(safeComponent);
    }

    if (normalized.empty()) {
        return sanitizeComponent(fs::path(std::string(rawPath)).filename().generic_u8string());
    }

    return normalized.generic_u8string();
}

fs::path resolveForNote(const fs::path& notePath, std::string_view storedPath) {
    if (hasUnsafeStoredPath(storedPath)) {
        return {};
    }

    const std::string normalized = sanitizeStoredPath(storedPath);
    if (normalized.empty()) {
        return {};
    }

    const fs::path noteDir = canonicalOrEmpty(notePath.parent_path());
    if (noteDir.empty()) {
        return {};
    }

    const fs::path candidate = canonicalOrEmpty(noteDir / fs::u8path(normalized));
    if (candidate.empty() || !isWithinRoot(candidate, noteDir)) {
        return {};
    }

    return candidate;
}

std::string stageForNote(const fs::path& targetNotePath,
                         const fs::path& resourceRef,
                         const fs::path& sourceNotePath) {
    if (resourceRef.empty()) {
        return {};
    }

    std::error_code ec;
    fs::create_directories(targetNotePath.parent_path(), ec);

    const fs::path targetDir = canonicalOrEmpty(targetNotePath.parent_path());
    if (targetDir.empty()) {
        return sanitizeStoredPath(resourceRef.generic_u8string());
    }

    const fs::path sourcePath = resolveSourcePath(resourceRef, sourceNotePath);
    const fs::path canonicalSource = canonicalOrEmpty(sourcePath);

    if (!canonicalSource.empty() && isWithinRoot(canonicalSource, targetDir)) {
        return sanitizeStoredPath(fs::relative(canonicalSource, targetDir, ec).generic_u8string());
    }

    if (!canonicalSource.empty() && fs::exists(canonicalSource)) {
        const std::string noteStem = sanitizeComponent(targetNotePath.stem().generic_u8string());
        fs::path assetDir = targetDir / fs::u8path((noteStem.empty() ? "note" : noteStem) + "_assets");
        fs::create_directories(assetDir, ec);
        if (ec) {
            return defaultResourceName(resourceRef);
        }

        fs::path destination = uniqueAssetPath(assetDir, canonicalSource);
        fs::copy_file(canonicalSource, destination, fs::copy_options::skip_existing, ec);
        if (ec && !fs::exists(destination)) {
            return defaultResourceName(resourceRef);
        }

        return sanitizeStoredPath(fs::relative(destination, targetDir, ec).generic_u8string());
    }

    if (resourceRef.is_relative()) {
        return sanitizeStoredPath(resourceRef.generic_u8string());
    }

    return defaultResourceName(resourceRef);
}

} // namespace LegacyNoteResourceHelper

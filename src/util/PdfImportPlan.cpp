// SPDX-License-Identifier: GPL-2.0-or-later

#include "PdfImportPlan.h"

#include <cmath>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string sanitizeStem(std::string_view rawStem) {
    std::string combined;
    const fs::path input{std::string(rawStem)};

    for (const fs::path& componentPath : input) {
        const std::string component = componentPath.generic_u8string();
        if (component.empty() || component == "." || component == ".." ||
            component == "/" || component == "\\") {
            continue;
        }

        std::string sanitizedComponent;
        sanitizedComponent.reserve(component.size());
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
                sanitizedComponent.push_back(static_cast<char>(ch));
                break;
            }
        }

        if (sanitizedComponent.empty()) {
            continue;
        }

        if (!combined.empty()) {
            combined.push_back('_');
        }
        combined += sanitizedComponent;
    }

    return combined.empty() ? "imported" : combined;
}

} // namespace

namespace PdfImportPlan {

FitRect fitContain(double containerWidth,
                   double containerHeight,
                   double contentWidth,
                   double contentHeight) {
    FitRect rect;
    if (!std::isfinite(containerWidth) || !std::isfinite(containerHeight) ||
        !std::isfinite(contentWidth) || !std::isfinite(contentHeight) ||
        containerWidth <= 0.0 || containerHeight <= 0.0 ||
        contentWidth <= 0.0 || contentHeight <= 0.0) {
        return rect;
    }

    rect.valid = true;
    rect.scale = std::min(containerWidth / contentWidth, containerHeight / contentHeight);
    rect.width = contentWidth * rect.scale;
    rect.height = contentHeight * rect.scale;
    rect.offsetX = (containerWidth - rect.width) / 2.0;
    rect.offsetY = (containerHeight - rect.height) / 2.0;
    return rect;
}

fs::path uniquePdfImportDirectory(const fs::path& rootDir, std::string_view preferredStem) {
    const std::string safeStem = sanitizeStem(preferredStem);
    const std::string baseName = safeStem + "_pdf";

    fs::path candidate = rootDir / fs::u8path(baseName);
    if (!fs::exists(candidate)) {
        return candidate;
    }

    for (int index = 1; index < 10000; ++index) {
        candidate = rootDir / fs::u8path(baseName + "_" + std::to_string(index));
        if (!fs::exists(candidate)) {
            return candidate;
        }
    }

    return rootDir / fs::u8path(baseName + "_overflow");
}

} // namespace PdfImportPlan

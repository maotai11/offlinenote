// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>
#include <string_view>

namespace PdfImportPlan {

struct FitRect {
    bool valid = false;
    double scale = 1.0;
    double width = 0.0;
    double height = 0.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
};

FitRect fitContain(double containerWidth,
                   double containerHeight,
                   double contentWidth,
                   double contentHeight);

std::filesystem::path uniquePdfImportDirectory(const std::filesystem::path& rootDir,
                                               std::string_view preferredStem);

} // namespace PdfImportPlan

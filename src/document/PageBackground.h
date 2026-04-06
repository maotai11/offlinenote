// src/document/PageBackground.h
// 背景描述
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "../util/SafeArithmetic.h"
#include <string>

enum class BackgroundType {
    Blank,
    Ruled,
    Grid,
    Dotted,
    PdfPage
};

inline std::string backgroundTypeToString(BackgroundType type) {
    switch (type) {
        case BackgroundType::Blank: return "blank";
        case BackgroundType::Ruled: return "ruled";
        case BackgroundType::Grid: return "grid";
        case BackgroundType::Dotted: return "dotted";
        case BackgroundType::PdfPage: return "pdf-page";
    }
    return "blank";
}

class PageBackground {
public:
    PageBackground() = default;
    explicit PageBackground(BackgroundType type, Color color = Color{255, 255, 255, 255})
        : type_(type), color_(color) {}

    BackgroundType type() const { return type_; }
    Color color() const { return color_; }

    bool isPdf() const { return type_ == BackgroundType::PdfPage; }

private:
    BackgroundType type_ = BackgroundType::Blank;
    Color color_ = Color{255, 255, 255, 255};
};

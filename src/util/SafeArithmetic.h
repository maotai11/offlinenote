// src/util/SafeArithmetic.h
// 安全型別轉換與算術工具
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <algorithm>

// ──────────────────────────────────────────────────────────────
// Cairo 相關座標上限
//
// cairo_image_surface_create 的最大維度
// 來源：cairo/src/cairo.h CAIRO_MAX_IMAGE_SIZE = 32767
// 此為 Cairo image surface 的硬性限制
constexpr double CAIRO_MAX_IMAGE_SURFACE_DIM = 32767.0;

// 頁面內容座標上限（以 pt 為單位）
// A0 紙張最大邊 ≈ 3370pt，加 50% 餘裕
constexpr double MAX_PAGE_COORDINATE_PT = 5000.0;

// 螢幕像素座標上限（含縮放）
constexpr double MAX_SCREEN_COORDINATE_PX = 32000.0;

// ──────────────────────────────────────────────────────────────
// 顏色值 (0-255) 的安全來源
// ──────────────────────────────────────────────────────────────
struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    // 從整數安全建構（clamp 到 0-255）
    static Color fromInt(int r, int g, int b, int a = 255) {
        auto clamp = [](int v) -> uint8_t {
            if (v < 0)   return 0;
            if (v > 255) return 255;
            return static_cast<uint8_t>(v);
        };
        return { clamp(r), clamp(g), clamp(b), clamp(a) };
    }

    // 從浮點數安全建構（0.0 - 1.0 → 0 - 255）
    static Color fromFloat(double r, double g, double b, double a = 1.0) {
        auto clamp = [](double v) -> uint8_t {
            if (std::isnan(v) || v < 0.0) return 0;
            if (v > 1.0) return 255;
            return static_cast<uint8_t>(std::round(v * 255.0));
        };
        return { clamp(r), clamp(g), clamp(b), clamp(a) };
    }

    // 從 CSS hex 字串解析（#RRGGBB 或 #RRGGBBAA）
    static Color fromHexString(const std::string& hex) {
        if (hex.empty() || hex[0] != '#') {
            return {};
        }

        auto parseHex2 = [](const char* s) -> uint8_t {
            char buf[3] = { s[0], s[1], '\0' };
            char* end = nullptr;
            long val = std::strtol(buf, &end, 16);
            if (end != buf + 2 || val < 0 || val > 255) return 0;
            return static_cast<uint8_t>(val);
        };

        const size_t len = hex.size();
        if (len == 7) { // #RRGGBB
            return {
                parseHex2(hex.c_str() + 1),
                parseHex2(hex.c_str() + 3),
                parseHex2(hex.c_str() + 5),
                255
            };
        }
        else if (len == 9) { // #RRGGBBAA
            return {
                parseHex2(hex.c_str() + 1),
                parseHex2(hex.c_str() + 3),
                parseHex2(hex.c_str() + 5),
                parseHex2(hex.c_str() + 7)
            };
        }

        return {};
    }

    // 轉為 hex 字串
    std::string toHexString() const {
        char buf[10];
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", r, g, b, a);
        return buf;
    }

    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
};

// ──────────────────────────────────────────────────────────────
// 安全的浮點數值驗證（用於座標、壓力等）
// ──────────────────────────────────────────────────────────────
namespace SafeFloat {

// 驗證頁面座標（筆跡、圖片位置等，pt 單位）
inline bool isValidPageCoordinate(double v) {
    return std::isfinite(v) && std::abs(v) <= MAX_PAGE_COORDINATE_PT;
}

// 驗證螢幕/渲染座標（px 單位，經過縮放後）
inline bool isValidScreenCoordinate(double v) {
    return std::isfinite(v) && std::abs(v) <= MAX_SCREEN_COORDINATE_PX;
}

// 驗證壓力值 (0.0 - 1.0)
inline bool isValidPressure(double p) {
    return std::isfinite(p) && p >= 0.0 && p <= 1.0;
}

// 向後相容介面
[[deprecated("Use isValidPageCoordinate() or isValidScreenCoordinate()")]]
inline bool isValidCoordinate(double v, double maxMagnitude = MAX_PAGE_COORDINATE_PT) {
    return std::isfinite(v) && std::abs(v) <= maxMagnitude;
}

// ── Clamp 函式

inline double clampPageCoordinate(double v) {
    if (std::isnan(v))  return 0.0;
    if (v < -MAX_PAGE_COORDINATE_PT) return -MAX_PAGE_COORDINATE_PT;
    if (v >  MAX_PAGE_COORDINATE_PT) return  MAX_PAGE_COORDINATE_PT;
    return v;
}

inline double clampScreenCoordinate(double v) {
    if (std::isnan(v))  return 0.0;
    if (v < -MAX_SCREEN_COORDINATE_PX) return -MAX_SCREEN_COORDINATE_PX;
    if (v >  MAX_SCREEN_COORDINATE_PX) return  MAX_SCREEN_COORDINATE_PX;
    return v;
}

inline double clampCoordinate(double v, double min, double max) {
    if (std::isnan(v)) return (min + max) / 2.0;
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

inline double clampPressure(double p) {
    if (std::isnan(p)) return 0.5;
    if (p < 0.0) return 0.0;
    if (p > 1.0) return 1.0;
    return p;
}

} // namespace SafeFloat

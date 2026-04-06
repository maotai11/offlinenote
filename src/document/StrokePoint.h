// src/document/StrokePoint.h
// 單點資料結構
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

struct StrokePoint {
    double x = 0.0;
    double y = 0.0;
    double pressure = 0.5;     // 0.0 - 1.0
    double tiltX = 0.0;        // radians
    double tiltY = 0.0;        // radians
    int64_t timestamp = 0;     // milliseconds
};

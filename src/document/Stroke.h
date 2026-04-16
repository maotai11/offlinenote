// src/document/Stroke.h
// 筆跡資料結構
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "StrokePoint.h"
#include "../util/SafeArithmetic.h"
#include <vector>
#include <memory>
#include <string>

enum class ToolType {
    Pen,
    Highlighter,
    Eraser
};

inline std::string toolTypeToString(ToolType type) {
    switch (type) {
        case ToolType::Pen: return "pen";
        case ToolType::Highlighter: return "highlighter";
        case ToolType::Eraser: return "eraser";
    }
    return "pen";
}

struct ToolProperties {
    Color color{0, 0, 0, 255};
    double width = 2.0;
    double opacity = 1.0;
    ToolType toolType = ToolType::Pen;
};

struct BoundingBox {
    double minX = 0, minY = 0, maxX = 0, maxY = 0;

    void update(double x, double y) {
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
    }

    bool contains(double x, double y) const {
        return x >= minX && x <= maxX && y >= minY && y <= maxY;
    }
};

class Stroke {
public:
    Stroke() = default;
    Stroke(const std::vector<StrokePoint>& pts, const ToolProperties& props)
        : points_(pts), properties_(props) {
        updateBoundingBox();
    }

    const std::vector<StrokePoint>& points() const { return points_; }
    const ToolProperties& properties() const { return properties_; }
    const BoundingBox& boundingBox() const { return bbox_; }

    void setProperties(const ToolProperties& props) { properties_ = props; }

    void addPoint(const StrokePoint& pt) {
        if (!SafeFloat::isValidPageCoordinate(pt.x) || !SafeFloat::isValidPageCoordinate(pt.y)) {
            return;
        }

        const bool firstPoint = points_.empty();
        points_.push_back(pt);
        if (firstPoint) {
            bbox_.minX = bbox_.maxX = pt.x;
            bbox_.minY = bbox_.maxY = pt.y;
            return;
        }
        bbox_.update(pt.x, pt.y);
    }

private:
    void updateBoundingBox() {
        if (points_.empty()) return;
        bbox_.minX = bbox_.maxX = points_[0].x;
        bbox_.minY = bbox_.maxY = points_[0].y;
        for (const auto& pt : points_) {
            bbox_.update(pt.x, pt.y);
        }
    }

    std::vector<StrokePoint> points_;
    ToolProperties properties_;
    BoundingBox bbox_;
};

// src/document/Layer.h
// 圖層資料結構
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "Stroke.h"
#include <vector>
#include <memory>
#include <string>

class Layer {
public:
    Layer() = default;
    explicit Layer(const std::string& name) : name_(name) {}

    const std::string& name() const { return name_; }
    bool isVisible() const { return visible_; }
    bool isLocked() const { return locked_; }

    void setVisible(bool v) { visible_ = v; }
    void setLocked(bool l) { locked_ = l; }

    void addStroke(std::shared_ptr<Stroke> stroke) {
        strokes_.push_back(std::move(stroke));
    }

    const std::vector<std::shared_ptr<Stroke>>& strokes() const { return strokes_; }
    std::vector<std::shared_ptr<Stroke>>& strokes() { return strokes_; }

    // TODO: imageElements for Phase 2

private:
    std::string name_ = "Default";
    bool visible_ = true;
    bool locked_ = false;
    std::vector<std::shared_ptr<Stroke>> strokes_;
};

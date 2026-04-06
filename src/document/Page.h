// src/document/Page.h
// 單一頁面
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "Layer.h"
#include "PageBackground.h"
#include "PdfBackground.h"
#include <vector>
#include <memory>
#include <string>

enum class Orientation {
    Portrait,
    Landscape
};

struct PageSize {
    double widthPt = 595.28;   // A4 寬度
    double heightPt = 841.89;  // A4 高度

    static PageSize a4Portrait() { return {595.28, 841.89}; }
    static PageSize a4Landscape() { return {841.89, 595.28}; }
};

class Page {
public:
    Page() = default;
    Page(PageSize size, Orientation orientation)
        : size_(size), orientation_(orientation) {
        // 預設加入一個空白圖層
        addLayer("Default");
    }

    double widthPt() const {
        return orientation_ == Orientation::Portrait ? size_.widthPt : size_.heightPt;
    }

    double heightPt() const {
        return orientation_ == Orientation::Portrait ? size_.heightPt : size_.widthPt;
    }

    PageSize size() const { return size_; }
    Orientation orientation() const { return orientation_; }
    const PageBackground& background() const { return background_; }
    PageBackground& background() { return background_; }

    std::shared_ptr<Layer> addLayer(const std::string& name) {
        layers_.push_back(std::make_shared<Layer>(name));
        return layers_.back();
    }

    const std::vector<std::shared_ptr<Layer>>& layers() const { return layers_; }
    std::vector<std::shared_ptr<Layer>>& layers() { return layers_; }

    // 取得目前圖層（最後一個）
    Layer& currentLayer() { return *layers_.back(); }

private:
    PageSize size_ = PageSize::a4Portrait();
    Orientation orientation_ = Orientation::Portrait;
    PageBackground background_;
    std::vector<std::shared_ptr<Layer>> layers_;
};

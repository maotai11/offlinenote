// src/document/Document.h
// 文件根物件
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "Page.h"
#include "DocumentMetadata.h"
#include <vector>
#include <memory>
#include <filesystem>
#include <functional>

class Document {
public:
    Document() = default;

    // 頁面管理
    std::shared_ptr<Page> addPage(PageSize size = PageSize::a4Portrait(),
                                   Orientation orientation = Orientation::Portrait);
    void deletePage(int index);
    std::shared_ptr<Page> getPage(int index);
    std::shared_ptr<const Page> getPage(int index) const;
    int pageCount() const;

    // 當前頁面
    void setCurrentPage(int index);
    int currentPageIndex() const;
    std::shared_ptr<Page> currentPage();

    // Dirty 狀態
    bool isDirty() const { return dirty_; }
    void markDirty();
    void clearDirty();

    // 檔案路徑
    const std::filesystem::path& filePath() const { return filePath_; }
    void setFilePath(const std::filesystem::path& path) { filePath_ = path; }
    bool hasFilePath() const { return !filePath_.empty(); }

    // 元資料
    DocumentMetadata& metadata() { return metadata_; }
    const DocumentMetadata& metadata() const { return metadata_; }

    // 變更通知
    using ChangeCallback = std::function<void()>;
    void addChangeListener(ChangeCallback cb);

private:
    void notifyChanged();

    std::vector<std::shared_ptr<Page>> pages_;
    int currentPageIndex_ = 0;
    bool dirty_ = false;
    std::filesystem::path filePath_;
    DocumentMetadata metadata_;
    std::vector<ChangeCallback> changeListeners_;
};

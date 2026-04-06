// src/document/Document.cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Document.h"

std::shared_ptr<Page> Document::addPage(PageSize size, Orientation orientation) {
    auto page = std::make_shared<Page>(size, orientation);
    pages_.push_back(page);
    markDirty();
    return page;
}

void Document::deletePage(int index) {
    if (index < 0 || index >= static_cast<int>(pages_.size())) return;
    pages_.erase(pages_.begin() + index);
    if (currentPageIndex_ >= static_cast<int>(pages_.size())) {
        currentPageIndex_ = std::max(0, static_cast<int>(pages_.size()) - 1);
    }
    markDirty();
}

std::shared_ptr<Page> Document::getPage(int index) {
    if (index < 0 || index >= static_cast<int>(pages_.size())) return nullptr;
    return pages_[index];
}

std::shared_ptr<const Page> Document::getPage(int index) const {
    if (index < 0 || index >= static_cast<int>(pages_.size())) return nullptr;
    return pages_[index];
}

int Document::pageCount() const {
    return static_cast<int>(pages_.size());
}

void Document::setCurrentPage(int index) {
    if (index >= 0 && index < static_cast<int>(pages_.size())) {
        currentPageIndex_ = index;
    }
}

int Document::currentPageIndex() const {
    return currentPageIndex_;
}

std::shared_ptr<Page> Document::currentPage() {
    if (pages_.empty()) return nullptr;
    return getPage(currentPageIndex_);
}

void Document::markDirty() {
    dirty_ = true;
    metadata_.markModified();
    notifyChanged();
}

void Document::clearDirty() {
    dirty_ = false;
}

void Document::addChangeListener(ChangeCallback cb) {
    changeListeners_.push_back(std::move(cb));
}

void Document::notifyChanged() {
    for (const auto& cb : changeListeners_) {
        cb();
    }
}

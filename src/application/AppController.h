// src/application/AppController.h
// 頂層業務協調者
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "../document/Document.h"
#include <memory>
#include <filesystem>

class AppController {
public:
    AppController();
    ~AppController();

    // 文件操作
    std::shared_ptr<Document> createNewDocument();
    bool openDocument(const std::filesystem::path& path);
    bool saveDocument(const std::filesystem::path& path);
    bool saveDocumentAs(const std::filesystem::path& path);

    // 目前文件
    std::shared_ptr<Document> currentDocument() const { return currentDocument_; }

    // UI 訊號處理
    void onNewDocument();
    void onOpenDocument(const std::filesystem::path& path);
    void onSaveDocument();

private:
    void showError(const std::string& title, const std::string& message);
    bool promptSaveIfDirty();

    std::shared_ptr<Document> currentDocument_;
    std::filesystem::path currentFilePath_;
    bool dirty_ = false;
};

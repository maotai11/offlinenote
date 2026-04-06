// src/application/AppController.cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "AppController.h"
#include "../util/Logger.h"

AppController::AppController() {
    Logger::info("AppController created");
}

AppController::~AppController() {
    Logger::info("AppController destroying");
    // TODO: 觸發自動儲存
}

std::shared_ptr<Document> AppController::createNewDocument() {
    currentDocument_ = std::make_shared<Document>();
    currentFilePath_.clear();
    dirty_ = false;
    Logger::info("New document created");
    return currentDocument_;
}

bool AppController::openDocument(const std::filesystem::path& path) {
    Logger::info("Opening document: {}", path.string());
    // TODO: 實作完整的開啟流程（含 FileLock、PathValidator、Deserializer）
    currentDocument_ = std::make_shared<Document>();
    currentFilePath_ = path;
    dirty_ = false;
    return true;
}

bool AppController::saveDocument(const std::filesystem::path& path) {
    Logger::info("Saving document to: {}", path.string());
    // TODO: 實作完整的儲存流程（含原子寫入）
    currentFilePath_ = path;
    dirty_ = false;
    return true;
}

bool AppController::saveDocumentAs(const std::filesystem::path& path) {
    return saveDocument(path);
}

void AppController::onNewDocument() {
    Logger::info("New document requested");
    createNewDocument();
}

void AppController::onOpenDocument(const std::filesystem::path& path) {
    Logger::info("Open document requested: {}", path.string());
    openDocument(path);
}

void AppController::onSaveDocument() {
    Logger::info("Save document requested");
    if (!currentFilePath_.empty()) {
        saveDocument(currentFilePath_);
    }
    // TODO: 顯示儲存對話框
}

void AppController::onExportPdf(const std::filesystem::path& path) {
    Logger::info("Export to PDF: {}", path.string());
    // TODO: 實作 PDF 匯出
}

void AppController::onExportPng(const std::filesystem::path& path) {
    Logger::info("Export to PNG: {}", path.string());
    // TODO: 實作 PNG 匯出
}

void AppController::showError(const std::string& title, const std::string& message) {
    Logger::error(title + ": " + message);
    // TODO: 顯示 GTK 錯誤對話框
}

bool AppController::promptSaveIfDirty() {
    if (!dirty_) return true;
    // TODO: 顯示對話框詢問使用者
    return true;
}

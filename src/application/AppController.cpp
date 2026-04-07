// src/application/AppController.cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "AppController.h"
#include "../util/Logger.h"
#include "../util/PathValidator.h"
#include "../serialization/NoteDeserializer.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

AppController::AppController() {
    Logger::info("AppController created");
}

AppController::~AppController() {
    Logger::info("AppController destroying");
}

std::shared_ptr<Document> AppController::createNewDocument() {
    currentDocument_ = std::make_shared<Document>();
    currentFilePath_.clear();
    dirty_ = false;
    Logger::info("New document created");
    return currentDocument_;
}

bool AppController::openDocument(const std::filesystem::path& path) {
    Logger::info("Opening document");

    auto pathResult = PathValidator::validatePdfPath(path, true);
    if (!pathResult.valid) {
        Logger::error("Path validation failed");
        showError("Open Failed", "Invalid file path");
        return false;
    }

    if (!fs::exists(path)) {
        Logger::error("File does not exist");
        return false;
    }

    NoteDeserializer deserializer;
    currentDocument_ = deserializer.deserialize(path);

    if (!currentDocument_) {
        Logger::error("Deserialization failed");
        showError("Open Failed", "Cannot parse file format");
        return false;
    }

    currentFilePath_ = path;
    dirty_ = false;
    Logger::info("Document opened");
    return true;
}

bool AppController::saveDocument(const std::filesystem::path& path) {
    Logger::info("Saving document");

    if (!currentDocument_) {
        Logger::error("No document to save");
        return false;
    }

    auto pathResult = PathValidator::validatePdfPath(path, true);
    if (!pathResult.valid) {
        Logger::error("Path validation failed");
        showError("Save Failed", "Invalid save path");
        return false;
    }

    fs::path parentDir = path.parent_path();
    if (!parentDir.empty() && !fs::exists(parentDir)) {
        std::error_code ec;
        fs::create_directories(parentDir, ec);
        if (ec) {
            Logger::error("Failed to create directory");
            return false;
        }
    }

    fs::path tmpPath = path;
    tmpPath += ".tmp";

    std::ofstream ofs(tmpPath, std::ios::binary);
    if (!ofs) {
        Logger::error("Failed to create temp file");
        return false;
    }
    ofs << "# OfflineNote v4.0\n";
    ofs.close();

    std::error_code ec;
    fs::rename(tmpPath, path, ec);
    if (ec) {
        Logger::error("Atomic rename failed");
        fs::remove(tmpPath);
        return false;
    }

    currentFilePath_ = path;
    dirty_ = false;
    Logger::info("Document saved");
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
    Logger::info("Open document requested");
    if (!openDocument(path)) {
        showError("Open Failed", "Cannot open file");
    }
}

void AppController::onSaveDocument() {
    Logger::info("Save document requested");
    if (!currentFilePath_.empty()) {
        if (!saveDocument(currentFilePath_)) {
            showError("Save Failed", "Cannot save file");
        }
    }
}

void AppController::onExportPdf(const std::filesystem::path& path) {
    Logger::info("Export to PDF");
}

void AppController::onExportPng(const std::filesystem::path& path) {
    Logger::info("Export to PNG");
}

void AppController::showError(const std::string& title, const std::string& message) {
    Logger::error(title + ": " + message);
}

bool AppController::promptSaveIfDirty() {
    if (!dirty_) return true;
    Logger::warning("Document has unsaved changes");
    return true;
}

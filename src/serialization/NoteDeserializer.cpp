// src/serialization/NoteDeserializer.cpp
// 筆記反序列化器 — 串接安全管線
// SPDX-License-Identifier: GPL-2.0-or-later

#include "NoteDeserializer.h"
#include "SecureXmlParser.h"
#include "SafeDecompressor.h"
#include "../util/PathValidator.h"
#include "../util/Logger.h"
#include "../document/Document.h"
#include <fstream>
#include <sstream>

std::shared_ptr<Document> NoteDeserializer::deserialize(const std::filesystem::path& path) {
    // Step 1: 路徑驗證
    auto pathResult = PathValidator::validatePdfPath(path, true);
    if (!pathResult.valid) {
        Logger::warning("NoteDeserializer: Invalid path");
        return nullptr;
    }

    // Step 2: 檢查檔案存在
    if (!std::filesystem::exists(path)) {
        Logger::warning("NoteDeserializer: File not found");
        return nullptr;
    }

    // Step 3: 檢查是否為壓縮格式
    std::shared_ptr<Document> doc = std::make_shared<Document>();
    
    if (path.extension() == ".gz" || path.extension() == ".z") {
        auto decompressResult = SafeDecompressor::decompress(path);
        if (!decompressResult.success) {
            Logger::warning("NoteDeserializer: Decompression failed");
            return nullptr;
        }
        Logger::info("NoteDeserializer: Decompressed successfully");
    } else if (path.extension() == ".xml") {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            Logger::warning("NoteDeserializer: Cannot open XML file");
            return nullptr;
        }
        
        std::string xmlData((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();
        
        auto parseResult = SecureXmlParser::parseFromBuffer(xmlData.data(), xmlData.size());
        if (!parseResult.success()) {
            Logger::warning("NoteDeserializer: XML parse failed");
            return nullptr;
        }
        Logger::info("NoteDeserializer: Parsed XML successfully");
    } else {
        Logger::debug("NoteDeserializer: Legacy text format");
    }

    return doc;
}

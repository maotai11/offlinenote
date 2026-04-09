// src/serialization/NoteDeserializer.cpp
// 筆記反序列化器 — 串接安全管線
// SPDX-License-Identifier: GPL-2.0-or-later

#include "NoteDeserializer.h"
#include "SecureXmlParser.h"
#include "SafeDecompressor.h"
#include "../document/Document.h"
#include "../document/Page.h"
#include "../document/Stroke.h"
#include "../document/StrokePoint.h"
#include "../util/PathValidator.h"
#include "../util/Logger.h"
#include <fstream>
#include <sstream>
#include <cstdlib>

// Helper: read attribute value from XML node
static std::string xmlGetPropStr(xmlNodePtr node, const char* name) {
    xmlChar* val = xmlGetProp(node, BAD_CAST name);
    if (!val) return "";
    std::string s(reinterpret_cast<const char*>(val));
    xmlFree(val);
    return s;
}

static double xmlGetPropDouble(xmlNodePtr node, const char* name, double def = 0.0) {
    std::string s = xmlGetPropStr(node, name);
    if (s.empty()) return def;
    return std::atof(s.c_str());
}

static int xmlGetPropInt(xmlNodePtr node, const char* name, int def = 0) {
    std::string s = xmlGetPropStr(node, name);
    if (s.empty()) return def;
    return std::atoi(s.c_str());
}

// Build Document from parsed XML doc
static std::shared_ptr<Document> buildDocumentFromXml(XmlDocHolder& docHolder) {
    auto doc = std::make_shared<Document>();
    xmlDocPtr xml = docHolder.get();
    xmlNodePtr root = xmlDocGetRootElement(xml);
    if (!root) return nullptr;

    // Read document metadata
    doc->metadata().setTitle(xmlGetPropStr(root, "title"));
    doc->metadata().setAuthor(xmlGetPropStr(root, "author"));

    // Iterate over page nodes
    for (xmlNodePtr pageNode = root->children; pageNode; pageNode = pageNode->next) {
        if (pageNode->type != XML_ELEMENT_NODE) continue;
        if (xmlStrcmp(pageNode->name, BAD_CAST "page") != 0) continue;

        double pw = xmlGetPropDouble(pageNode, "width", 595.28);
        double ph = xmlGetPropDouble(pageNode, "height", 841.89);
        PageSize sz{pw, ph};
        Orientation orient = (ph > pw) ? Orientation::Portrait : Orientation::Landscape;
        auto page = std::make_shared<Page>(sz, orient);

        // Iterate over elements in page
        for (xmlNodePtr elem = pageNode->children; elem; elem = elem->next) {
            if (elem->type != XML_ELEMENT_NODE) continue;

            if (xmlStrcmp(elem->name, BAD_CAST "stroke") == 0) {
                ToolProperties props;
                props.width = xmlGetPropDouble(elem, "width", 2.0);
                props.color = Color::fromFloat(
                    xmlGetPropDouble(elem, "r", 0.0),
                    xmlGetPropDouble(elem, "g", 0.0),
                    xmlGetPropDouble(elem, "b", 0.0),
                    xmlGetPropDouble(elem, "a", 1.0)
                );
                Stroke s;
                // Parse points from text content: "x1,y1 x2,y2 ..."
                xmlChar* content = xmlNodeGetContent(elem);
                if (content) {
                    std::istringstream iss(reinterpret_cast<const char*>(content));
                    double x, y;
                    char comma;
                    while (iss >> x >> comma >> y) {
                        s.addPoint(StrokePoint{x, y});
                    }
                    xmlFree(content);
                }
                // Add stroke to page's default layer
                auto layer = page->currentLayer();
                // TODO: add stroke to layer (API depends on implementation)
            }
            // TODO: handle image, text elements
        }

        doc->addPage(sz, orient);
    }

    return doc;
}

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

        // Step 4: 解壓後內容送進 XML 安全解析
        auto parseResult = SecureXmlParser::parseFromBuffer(decompressResult.data.data(), decompressResult.data.size());
        if (!parseResult.success()) {
            Logger::warning("NoteDeserializer: XML parse failed after decompression: %s", parseResult.errorMessage.c_str());
            return nullptr;
        }
        Logger::info("NoteDeserializer: Parsed XML from compressed file successfully");

        // Step 5: Build Document from XML
        doc = buildDocumentFromXml(parseResult.doc);
        if (!doc) {
            Logger::warning("NoteDeserializer: Failed to build Document from XML");
            return nullptr;
        }
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

        // Build Document from XML
        doc = buildDocumentFromXml(parseResult.doc);
        if (!doc) {
            Logger::warning("NoteDeserializer: Failed to build Document from XML");
            return nullptr;
        }
    } else {
        Logger::debug("NoteDeserializer: Legacy text format (not supported)");
        return nullptr;
    }

    doc->setFilePath(path);
    doc->clearDirty();
    return doc;
}

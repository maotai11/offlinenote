// SPDX-License-Identifier: GPL-2.0-or-later

#include "NoteDeserializer.h"
#include "SafeDecompressor.h"
#include "SecureXmlParser.h"
#include "../document/Document.h"
#include "../document/Page.h"
#include "../document/Stroke.h"
#include "../document/StrokePoint.h"
#include "../util/Logger.h"
#include "../util/PathValidator.h"
#include "../util/SafeArithmetic.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::string normalizedExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

}

static std::string xmlGetPropStr(xmlNodePtr node, const char* name) {
    xmlChar* val = xmlGetProp(node, BAD_CAST name);
    if (!val) return "";
    std::string s(reinterpret_cast<const char*>(val));
    xmlFree(val);
    return s;
}

static bool tryParseDouble(const std::string& text, double& out) {
    if (text.empty()) return false;

    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
        return false;
    }

    out = value;
    return true;
}

static double xmlGetPropDouble(xmlNodePtr node, const char* name, double def = 0.0) {
    double value = def;
    if (tryParseDouble(xmlGetPropStr(node, name), value)) {
        return value;
    }
    return def;
}

static ToolType parseToolType(const std::string& value) {
    if (value == "highlighter") return ToolType::Highlighter;
    if (value == "eraser") return ToolType::Eraser;
    return ToolType::Pen;
}

static bool isValidPageSize(double width, double height) {
    return SafeFloat::isValidPageCoordinate(width) &&
           SafeFloat::isValidPageCoordinate(height) &&
           width > 0.0 &&
           height > 0.0;
}

static std::shared_ptr<Document> buildDocumentFromXml(XmlDocHolder& docHolder, std::string* errorMessage) {
    auto doc = std::make_shared<Document>();
    xmlDocPtr xml = docHolder.get();
    xmlNodePtr root = xmlDocGetRootElement(xml);
    if (!root) {
        if (errorMessage) *errorMessage = "Missing root element";
        return nullptr;
    }

    const std::string title = xmlGetPropStr(root, "title");
    const std::string author = xmlGetPropStr(root, "author");
    const std::string formatVersion = xmlGetPropStr(root, "formatVersion");
    const std::string createdAt = xmlGetPropStr(root, "createdAt");
    const std::string modifiedAt = xmlGetPropStr(root, "modifiedAt");

    for (xmlNodePtr pageNode = root->children; pageNode; pageNode = pageNode->next) {
        if (pageNode->type != XML_ELEMENT_NODE) continue;
        if (xmlStrcmp(pageNode->name, BAD_CAST "page") != 0) continue;

        const double width = xmlGetPropDouble(pageNode, "width", 595.28);
        const double height = xmlGetPropDouble(pageNode, "height", 841.89);
        if (!isValidPageSize(width, height)) {
            if (errorMessage) *errorMessage = "Invalid page size in serialized note";
            return nullptr;
        }

        PageSize size{width, height};
        const Orientation orientation = (height > width) ? Orientation::Portrait : Orientation::Landscape;
        auto page = doc->addPage(size, orientation);

        for (xmlNodePtr elem = pageNode->children; elem; elem = elem->next) {
            if (elem->type != XML_ELEMENT_NODE) continue;

            if (xmlStrcmp(elem->name, BAD_CAST "stroke") == 0) {
                ToolProperties props;
                props.width = xmlGetPropDouble(elem, "width", 2.0);
                if (!std::isfinite(props.width) || props.width <= 0.0 || props.width > MAX_PAGE_COORDINATE_PT) {
                    props.width = 2.0;
                }

                props.opacity = SafeFloat::clampCoordinate(xmlGetPropDouble(elem, "opacity", 1.0), 0.0, 1.0);
                props.color = Color::fromFloat(
                    xmlGetPropDouble(elem, "r", 0.0),
                    xmlGetPropDouble(elem, "g", 0.0),
                    xmlGetPropDouble(elem, "b", 0.0),
                    xmlGetPropDouble(elem, "a", 1.0)
                );

                const std::string toolName = xmlGetPropStr(elem, "tool");
                props.toolType = parseToolType(toolName.empty() ? xmlGetPropStr(elem, "toolType") : toolName);

                auto stroke = std::make_shared<Stroke>();
                xmlChar* content = xmlNodeGetContent(elem);
                if (content) {
                    std::istringstream iss(reinterpret_cast<const char*>(content));
                    double x = 0.0;
                    double y = 0.0;
                    char comma = '\0';
                    while (iss >> x >> comma >> y) {
                        if (comma != ',') continue;
                        if (!SafeFloat::isValidPageCoordinate(x) || !SafeFloat::isValidPageCoordinate(y)) {
                            continue;
                        }
                        stroke->addPoint(StrokePoint{x, y});
                    }
                    xmlFree(content);
                }

                stroke->setProperties(props);
                if (!stroke->points().empty()) {
                    page->currentLayer().addStroke(stroke);
                }
                continue;
            }

            if (errorMessage) {
                *errorMessage = "Unsupported page element: " +
                    std::string(reinterpret_cast<const char*>(elem->name));
            }
            return nullptr;
        }
    }

    doc->metadata().setTitle(title);
    doc->metadata().setAuthor(author);
    if (!formatVersion.empty()) {
        doc->metadata().formatVersion = formatVersion;
    }
    if (!createdAt.empty()) {
        doc->metadata().createdAt = static_cast<std::time_t>(std::atoll(createdAt.c_str()));
    }
    if (!modifiedAt.empty()) {
        doc->metadata().modifiedAt = static_cast<std::time_t>(std::atoll(modifiedAt.c_str()));
    }

    return doc;
}

std::shared_ptr<Document> NoteDeserializer::deserialize(const std::filesystem::path& path) {
    auto pathResult = PathValidator::validatePdfPath(path, true);
    if (!pathResult.valid) {
        Logger::warning("NoteDeserializer: Invalid path");
        return nullptr;
    }

    if (!std::filesystem::exists(path)) {
        Logger::warning("NoteDeserializer: File not found");
        return nullptr;
    }

    std::shared_ptr<Document> doc = std::make_shared<Document>();

    const std::string extension = normalizedExtension(path);

    if (extension == ".gz" || extension == ".z") {
        auto decompressResult = SafeDecompressor::decompress(path);
        if (!decompressResult.success) {
            Logger::warning("NoteDeserializer: Decompression failed");
            return nullptr;
        }
        Logger::info("NoteDeserializer: Decompressed successfully");

        auto parseResult = SecureXmlParser::parseFromBuffer(
            decompressResult.data.data(),
            decompressResult.data.size()
        );
        if (!parseResult.success()) {
            Logger::warning("NoteDeserializer: XML parse failed after decompression: {}", parseResult.errorMessage.c_str());
            return nullptr;
        }
        Logger::info("NoteDeserializer: Parsed XML from compressed file successfully");

        std::string buildError;
        doc = buildDocumentFromXml(parseResult.doc, &buildError);
        if (!doc) {
            Logger::warning("NoteDeserializer: Failed to build Document from XML: {}", buildError.c_str());
            return nullptr;
        }
    } else if (extension == ".xml" || extension == ".onote") {
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

        std::string buildError;
        doc = buildDocumentFromXml(parseResult.doc, &buildError);
        if (!doc) {
            Logger::warning("NoteDeserializer: Failed to build Document from XML: {}", buildError.c_str());
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

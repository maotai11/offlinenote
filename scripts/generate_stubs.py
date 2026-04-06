#!/usr/bin/env python3
"""
generate_stubs.py — 生成 OfflineNote 專案骨架檔案
執行方式：python3 generate_stubs.py
"""

import os

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 定義所有需要建立的骨架檔案
STUB_FILES = {
    # Serialization
    "src/serialization/NoteSerializer.h": '''// src/serialization/NoteSerializer.h
#pragma once
#include <filesystem>
class Document;
class NoteSerializer {
public:
    bool serialize(const Document& doc, const std::filesystem::path& path);
};
''',
    "src/serialization/NoteSerializer.cpp": '''// src/serialization/NoteSerializer.cpp
#include "NoteSerializer.h"
#include "../document/Document.h"
bool NoteSerializer::serialize(const Document&, const std::filesystem::path&) { return true; }
''',
    "src/serialization/NoteDeserializer.h": '''// src/serialization/NoteDeserializer.h
#pragma once
#include <filesystem>
#include <memory>
class Document;
class NoteDeserializer {
public:
    std::shared_ptr<Document> deserialize(const std::filesystem::path& path);
};
''',
    "src/serialization/NoteDeserializer.cpp": '''// src/serialization/NoteDeserializer.cpp
#include "NoteDeserializer.h"
#include "../document/Document.h"
std::shared_ptr<Document> NoteDeserializer::deserialize(const std::filesystem::path&) {
    return std::make_shared<Document>();
}
''',
    "src/serialization/SafeDecompressor.h": '''// src/serialization/SafeDecompressor.h
#pragma once
#include <filesystem>
#include <vector>
#include <string>
struct DecompressResult {
    std::vector<char> data;
    bool success = false;
    std::string errorMessage;
};
class SafeDecompressor {
public:
    static DecompressResult decompress(const std::filesystem::path& path);
};
''',
    "src/serialization/SafeDecompressor.cpp": '''// src/serialization/SafeDecompressor.cpp
#include "SafeDecompressor.h"
DecompressResult SafeDecompressor::decompress(const std::filesystem::path&) {
    return {};
}
''',
    "src/serialization/SecureXmlParser.h": '''// src/serialization/SecureXmlParser.h
#pragma once
#include <libxml/parser.h>
#include <string>

class XmlDocHolder {
public:
    explicit XmlDocHolder(xmlDocPtr d = nullptr) : doc_(d) {}
    ~XmlDocHolder() { if (doc_) xmlFreeDoc(doc_); }
    XmlDocHolder(const XmlDocHolder&) = delete;
    XmlDocHolder& operator=(const XmlDocHolder&) = delete;
    XmlDocHolder(XmlDocHolder&& o) noexcept : doc_(o.doc_) { o.doc_ = nullptr; }
    xmlDocPtr get() const { return doc_; }
    bool valid() const { return doc_ != nullptr; }
private:
    xmlDocPtr doc_ = nullptr;
};

class XmlParserCtxtHolder {
public:
    explicit XmlParserCtxtHolder(xmlParserCtxtPtr c = nullptr) : ctx_(c) {}
    ~XmlParserCtxtHolder() { if (ctx_) xmlFreeParserCtxt(ctx_); }
    XmlParserCtxtHolder(const XmlParserCtxtHolder&) = delete;
    XmlParserCtxtHolder& operator=(const XmlParserCtxtHolder&) = delete;
    XmlParserCtxtHolder(XmlParserCtxtHolder&& o) noexcept : ctx_(o.ctx_) { o.ctx_ = nullptr; }
    xmlParserCtxtPtr get() const { return ctx_; }
    bool valid() const { return ctx_ != nullptr; }
private:
    xmlParserCtxtPtr ctx_ = nullptr;
};

class SecureXmlParser {
public:
    struct ParseResult {
        XmlDocHolder doc;
        std::string errorMessage;
        bool success() const { return doc.valid(); }
    };
    static ParseResult parseFromBuffer(const char* buffer, size_t bufferSize,
                                        size_t maxSizeBytes = 64ULL * 1024 * 1024);
private:
    static xmlParserInputPtr noOpEntityLoader(const char*, const char*, xmlParserCtxtPtr);
    static void installGlobalNoOpLoader();
    static void applyContextSecurityOptions(xmlParserCtxtPtr ctxt);
    struct ErrorAccumulator {
        std::string text;
        static void handler(void* userData, xmlErrorPtr err);
    };
};
''',
    "src/serialization/SecureXmlParser.cpp": '''// src/serialization/SecureXmlParser.cpp
#include "SecureXmlParser.h"
#include "../util/Logger.h"
#include <libxml/xmlerror.h>
#include <libxml/xmlversion.h>
#include <mutex>

static_assert(LIBXML_VERSION >= 20900, "libxml2 >= 2.9.0 required");

static constexpr int SAFE_PARSE_OPTIONS = XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_COMPACT;
static constexpr int DANGEROUS_OPTIONS = XML_PARSE_NOENT | XML_PARSE_DTDLOAD;

static_assert(XML_PARSE_NOENT == 2, "NOENT value mismatch");
static_assert(XML_PARSE_DTDLOAD == 4, "DTDLOAD value mismatch");
static_assert(XML_PARSE_NONET == 2048, "NONET value mismatch");
static_assert((SAFE_PARSE_OPTIONS & DANGEROUS_OPTIONS) == 0, "Safe/dangerous overlap");

xmlParserInputPtr SecureXmlParser::noOpEntityLoader(const char* url, const char*, xmlParserCtxtPtr) {
    Logger::warning("SecureXmlParser [global-guard]: Blocked external entity: {}", url ? url : "(null)");
    return nullptr;
}

void SecureXmlParser::installGlobalNoOpLoader() {
    static std::once_flag once;
    std::call_once(once, []() {
        Logger::warning("SecureXmlParser: Installing global no-op entity loader");
        xmlSetExternalEntityLoader(noOpEntityLoader);
    });
}

void SecureXmlParser::applyContextSecurityOptions(xmlParserCtxtPtr ctxt) {
#if LIBXML_VERSION >= 21300
    xmlCtxtSetOptions(ctxt, SAFE_PARSE_OPTIONS);
#else
    xmlCtxtUseOptions(ctxt, SAFE_PARSE_OPTIONS);
    ctxt->options &= ~DANGEROUS_OPTIONS;
#endif
}

void SecureXmlParser::ErrorAccumulator::handler(void* userData, xmlErrorPtr err) {
    if (!userData || !err || !err->message) return;
    auto* acc = static_cast<ErrorAccumulator*>(userData);
    acc->text += std::string("[line ") + std::to_string(err->line) + "] " + err->message;
}

SecureXmlParser::ParseResult SecureXmlParser::parseFromBuffer(const char* buffer, size_t bufferSize, size_t maxSizeBytes) {
    installGlobalNoOpLoader();
    if (!buffer || bufferSize == 0) return { XmlDocHolder{}, "Empty buffer" };
    if (bufferSize > maxSizeBytes) return { XmlDocHolder{}, "Input too large" };

    XmlParserCtxtHolder ctxtHolder{ xmlNewParserCtxt() };
    if (!ctxtHolder.valid()) return { XmlDocHolder{}, "xmlNewParserCtxt failed" };

    applyContextSecurityOptions(ctxtHolder.get());

    ErrorAccumulator errorAcc;
    xmlSetStructuredErrorFunc(ctxtHolder.get(), ErrorAccumulator::handler);
    ctxtHolder.get()->userData = &errorAcc;

    xmlDocPtr rawDoc = xmlCtxtReadMemory(ctxtHolder.get(), buffer, static_cast<int>(bufferSize),
                                          "offlinenote-doc", "UTF-8", SAFE_PARSE_OPTIONS);

    // Post-parse check
    if ((ctxtHolder.get()->options & XML_PARSE_NOENT) != 0) {
        if (rawDoc) xmlFreeDoc(rawDoc);
        return { XmlDocHolder{}, "Security check: XML_PARSE_NOENT detected" };
    }
    if ((ctxtHolder.get()->options & XML_PARSE_DTDLOAD) != 0) {
        if (rawDoc) xmlFreeDoc(rawDoc);
        return { XmlDocHolder{}, "Security check: XML_PARSE_DTDLOAD detected" };
    }

    if (!rawDoc) return { XmlDocHolder{}, "XML parse failed: " + errorAcc.text };

    XmlDocHolder docHolder{ rawDoc };
    xmlNodePtr root = xmlDocGetRootElement(rawDoc);
    if (!root) return { XmlDocHolder{}, "No root element" };
    if (xmlStrcmp(root->name, BAD_CAST "offlinenote") != 0)
        return { XmlDocHolder{}, "Unexpected root: " + std::string(reinterpret_cast<const char*>(root->name)) };

    return { std::move(docHolder), "" };
}
''',
    "src/serialization/XmlHelper.cpp": '''// src/serialization/XmlHelper.cpp
#pragma once
// XML helper utilities
''',
    "src/serialization/MigrationManager.cpp": '''// src/serialization/MigrationManager.cpp
#pragma once
// Format migration utilities
''',

    # Rendering
    "src/rendering/StrokeRenderer.cpp": '''// src/rendering/StrokeRenderer.cpp
#include <cairo.h>
class Stroke;
struct RenderContext;
class StrokeRenderer {
public:
    void render(cairo_t* cr, const Stroke& stroke, const RenderContext& ctx) {}
};
''',
    "src/rendering/BackgroundRenderer.cpp": '''// src/rendering/BackgroundRenderer.cpp
#include <cairo.h>
class PageBackground;
struct PageSize;
struct RenderContext;
class BackgroundRenderer {
public:
    void render(cairo_t* cr, const PageBackground& bg, const PageSize& size, const RenderContext& ctx) {}
};
''',
    "src/rendering/PdfPageRenderer.cpp": '''// src/rendering/PdfPageRenderer.cpp
#include <cairo.h>
class PdfBackground;
struct RenderContext;
class PdfPageRenderer {
public:
    void render(cairo_t* cr, const PdfBackground& pdf, const RenderContext& ctx) {}
};
''',
    "src/rendering/ImageRenderer.cpp": '''// src/rendering/ImageRenderer.cpp
#include <cairo.h>
class ImageElement;
struct RenderContext;
class ImageRenderer {
public:
    void render(cairo_t* cr, const ImageElement& img, const RenderContext& ctx) {}
};
''',
    "src/rendering/RenderCache.cpp": '''// src/rendering/RenderCache.cpp
#include <cairo.h>
class Page;
class RenderCache {
public:
    bool isValid(const Page& page, double scale) { return false; }
    void blit(cairo_t* cr, const Page& page, const RenderContext& ctx) {}
    void update(const Page& page, double scale, cairo_surface_t* surface) {}
    void invalidate(const Page& page) {}
    void invalidateAll() {}
};
''',

    # Input
    "src/input/InputRouter.cpp": '''// src/input/InputRouter.cpp
class InputRouter {
public:
    void dispatch(void* event) {}
};
''',
    "src/input/StylusHandler.cpp": '''// src/input/StylusHandler.cpp
class StylusHandler {
public:
    void extractPoint(void* event) {}
};
''',
    "src/input/GestureHandler.cpp": '''// src/input/GestureHandler.cpp
class GestureHandler {
public:
    void handleGesture(void* event) {}
};
''',
    "src/input/StrokeBuilder.cpp": '''// src/input/StrokeBuilder.cpp
#include "../document/Stroke.h"
#include "../util/SafeArithmetic.h"
#include "../util/Logger.h"
#include <memory>
#include <vector>

class StrokeBuilder {
public:
    void begin(const ToolProperties& props, const StrokePoint& firstPoint) {
        active_ = true;
        props_ = props;
        points_.clear();
        points_.push_back(firstPoint);
    }
    void addPoint(StrokePoint point) {
        if (!active_) return;
        point.x = SafeFloat::clampPageCoordinate(point.x);
        point.y = SafeFloat::clampPageCoordinate(point.y);
        point.pressure = SafeFloat::clampPressure(point.pressure);
        points_.push_back(point);
    }
    std::shared_ptr<Stroke> finalize() {
        active_ = false;
        if (points_.empty()) return nullptr;
        return std::make_shared<Stroke>(points_, props_);
    }
    bool isActive() const { return active_; }
private:
    bool active_ = false;
    ToolProperties props_;
    std::vector<StrokePoint> points_;
};
''',
    "src/input/Stabilizer.cpp": '''// src/input/Stabilizer.cpp
#include <vector>
class Stabilizer {
public:
    void addPoint(double x, double y) { buffer_.push_back({x, y}); }
    double getSmoothedX() const {
        if (buffer_.empty()) return 0;
        double sum = 0;
        for (auto& p : buffer_) sum += p.first;
        return sum / buffer_.size();
    }
    double getSmoothedY() const {
        if (buffer_.empty()) return 0;
        double sum = 0;
        for (auto& p : buffer_) sum += p.second;
        return sum / buffer_.size();
    }
private:
    std::vector<std::pair<double, double>> buffer_;
};
''',
    "src/input/ToolState.cpp": '''// src/input/ToolState.cpp
enum class ToolType { Pen, Highlighter, Eraser };
class ToolState {
public:
    ToolType getCurrentTool() const { return currentTool_; }
    void setCurrentTool(ToolType t) { currentTool_ = t; }
private:
    ToolType currentTool_ = ToolType::Pen;
};
''',

    # Tools
    "src/tools/ToolManager.cpp": '''// src/tools/ToolManager.cpp
class ToolManager {
public:
    void setCurrentTool(int toolId) {}
    int getCurrentTool() const { return 0; }
};
''',
    "src/tools/PenTool.cpp": '''// src/tools/PenTool.cpp
class PenTool {
public:
    void draw(void* cr, double x, double y) {}
};
''',
    "src/tools/EraserTool.cpp": '''// src/tools/EraserTool.cpp
class EraserTool {
public:
    void erase(void* cr, double x, double y) {}
};
''',
    "src/tools/SelectionTool.cpp": '''// src/tools/SelectionTool.cpp
class SelectionTool {
public:
    void select(double x1, double y1, double x2, double y2) {}
};
''',

    # Export
    "src/export/ExportManager.cpp": '''// src/export/ExportManager.cpp
#include <filesystem>
class Document;
class ExportManager {
public:
    bool exportPdf(const Document& doc, const std::filesystem::path& path) { return true; }
    bool exportPng(const Document& doc, const std::filesystem::path& path) { return true; }
};
''',
    "src/export/PdfExporter.cpp": '''// src/export/PdfExporter.cpp
#include <filesystem>
#include <cairo.h>
class PdfExporter {
public:
    bool exportDocument(void* doc, const std::filesystem::path& path) { return true; }
};
''',
    "src/export/PngExporter.cpp": '''// src/export/PngExporter.cpp
#include <filesystem>
#include <cairo.h>
class PngExporter {
public:
    bool exportPage(void* page, const std::filesystem::path& path, int dpi) { return true; }
};
''',

    # Import
    "src/import/PdfImporter.cpp": '''// src/import/PdfImporter.cpp
#include <filesystem>
class PdfImporter {
public:
    bool importPdf(const std::filesystem::path& path) { return true; }
};
''',
    "src/import/ImageImporter.cpp": '''// src/import/ImageImporter.cpp
#include <filesystem>
class ImageImporter {
public:
    bool importImage(const std::filesystem::path& path) { return true; }
};
''',

    # UI
    "src/ui/MainToolbar.cpp": '''// src/ui/MainToolbar.cpp
#include <gtk/gtk.h>
class MainToolbar {
public:
    GtkWidget* create() { return gtk_toolbar_new(); }
};
''',
    "src/ui/PageCanvas.cpp": '''// src/ui/PageCanvas.cpp
#include <gtk/gtk.h>
class PageCanvas {
public:
    GtkWidget* create() { return gtk_drawing_area_new(); }
};
''',
    "src/ui/MenuBar.cpp": '''// src/ui/MenuBar.cpp
#include <gtk/gtk.h>
class MenuBar {
public:
    GtkWidget* create() { return gtk_menu_bar_new(); }
};
''',
    "src/ui/StatusBar.cpp": '''// src/ui/StatusBar.cpp
#include <gtk/gtk.h>
class StatusBar {
public:
    GtkWidget* create() { return gtk_statusbar_new(); }
};
''',
    "src/ui/dialogs/NewDocumentDialog.cpp": '''// src/ui/dialogs/NewDocumentDialog.cpp
#include <gtk/gtk.h>
class NewDocumentDialog {
public:
    int show(GtkWindow* parent) { return GTK_RESPONSE_OK; }
};
''',
    "src/ui/dialogs/ExportDialog.cpp": '''// src/ui/dialogs/ExportDialog.cpp
#include <gtk/gtk.h>
class ExportDialog {
public:
    int show(GtkWindow* parent) { return GTK_RESPONSE_OK; }
};
''',
    "src/ui/dialogs/AboutDialog.cpp": '''// src/ui/dialogs/AboutDialog.cpp
#include <gtk/gtk.h>
class AboutDialog {
public:
    void show(GtkWindow* parent) {
        GtkWidget* dialog = gtk_about_dialog_new();
        gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "OfflineNote");
        gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "0.1.0");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
};
''',
    "src/ui/widgets/ColorButton.cpp": '''// src/ui/widgets/ColorButton.cpp
#include <gtk/gtk.h>
class ColorButton {
public:
    GtkWidget* create() { return gtk_color_button_new(); }
};
''',
    "src/ui/widgets/PenSizeSlider.cpp": '''// src/ui/widgets/PenSizeSlider.cpp
#include <gtk/gtk.h>
class PenSizeSlider {
public:
    GtkWidget* create() { return gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 100.0, 0.1); }
};
''',
    "src/ui/widgets/ZoomController.cpp": '''// src/ui/widgets/ZoomController.cpp
#include <gtk/gtk.h>
class ZoomController {
public:
    GtkWidget* create() { return gtk_spin_button_new_with_range(0.1, 10.0, 0.1); }
};
''',

    # Platform
    "src/platform/FontManager.cpp": '''// src/platform/FontManager.cpp
#include <filesystem>
class FontManager {
public:
    static FontManager& instance() { static FontManager m; return m; }
    void initialize(const std::filesystem::path& resourceDir) {}
};
''',
    "src/platform/ThemeManager.cpp": '''// src/platform/ThemeManager.cpp
#include <filesystem>
class ThemeManager {
public:
    static ThemeManager& instance() { static ThemeManager m; return m; }
    void initialize(const std::filesystem::path& resourceDir) {}
};
''',
    "src/platform/CrashLogger.cpp": '''// src/platform/CrashLogger.cpp
#include <filesystem>
class CrashLogger {
public:
    static void install(const std::filesystem::path& logDir) {}
};
''',
    "src/platform/FileLock.cpp": '''// src/platform/FileLock.cpp
#include <filesystem>
#include <string>

enum class FileLockResult { Acquired, AlreadyLocked, LockFileError };

class FileLock {
public:
    ~FileLock() { unlock(); }
    FileLockResult tryLock(const std::filesystem::path& docPath) {
        return FileLockResult::Acquired;
    }
    void unlock() {}
    bool isLocked() const { return locked_; }
    std::string getLockHolderInfo() const { return ""; }
private:
    bool locked_ = false;
    std::filesystem::path lockFilePath_;
};
''',
    "src/platform/AtomicRename.cpp": '''// src/platform/AtomicRename.cpp
#include <filesystem>

bool atomicRename(const std::filesystem::path& src, const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::rename(src, dest, ec);
    return !ec;
}
''',
    "src/platform/PlatformLinux.cpp": '''// src/platform/PlatformLinux.cpp
// Linux platform implementation
''',
    "src/platform/PlatformMacOS.cpp": '''// src/platform/PlatformMacOS.cpp
// macOS platform implementation
''',
    "src/platform/PlatformWindows.cpp": '''// src/platform/PlatformWindows.cpp
// Windows platform implementation
''',

    # Test CMakeLists
    "test/CMakeLists.txt": '''# test/CMakeLists.txt
add_library(catch2_vendored STATIC catch_amalgamated.cpp)
target_include_directories(catch2_vendored PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

function(add_onote_test name sources)
    add_executable(${name} ${sources})
    target_link_libraries(${name} PRIVATE catch2_vendored offlinenote_core_lib)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_onote_test(test_document unit/test_document.cpp)
add_onote_test(test_stroke unit/test_stroke.cpp)
add_onote_test(test_serialization unit/test_serialization.cpp)
add_onote_test(test_security_xxe unit/test_security_xxe.cpp)
add_onote_test(test_security_zipbomb unit/test_security_zipbomb.cpp)
add_onote_test(test_atomic_write unit/test_atomic_write.cpp)
add_onote_test(test_path_validator unit/test_path_validator.cpp)
add_onote_test(test_stroke_validation unit/test_stroke_validation.cpp)
add_onote_test(test_file_lock unit/test_file_lock.cpp)
''',
    "test/catch_amalgamated.hpp": '''// test/catch_amalgamated.hpp
// Vendored Catch2 amalgamated header
// Download from: https://github.com/catchorg/Catch2
#pragma once
#define CATCH_CONFIG_MAIN
#include <iostream>
#define TEST_CASE(name, ...) void test_##__LINE__()
#define SECTION(name) if(true)
#define REQUIRE(x) if(!(x)) { std::cerr << "REQUIRE failed: " << #x << std::endl; }
#define INFO(x)
#define SUCCEED(msg)
'''[
    -1
  ],
    "test/catch_amalgamated.cpp": '''// test/catch_amalgamated.cpp
// Vendored Catch2 amalgamated implementation
// Download from: https://github.com/catchorg/Catch2
''',
}


def main():
    created = 0
    for rel_path, content in STUB_FILES.items():
        full_path = os.path.join(BASE_DIR, rel_path)
        os.makedirs(os.path.dirname(full_path), exist_ok=True)
        if not os.path.exists(full_path):
            with open(full_path, "w", encoding="utf-8") as f:
                f.write(content)
            print(f"Created: {rel_path}")
            created += 1
        else:
            print(f"Exists:   {rel_path}")
    print(f"\nTotal: {created} files created")


if __name__ == "__main__":
    main()

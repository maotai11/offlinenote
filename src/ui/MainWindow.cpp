// src/ui/MainWindow.cpp - Complete working implementation
// SPDX-License-Identifier: GPL-2.0-or-later

#include "MainWindow.h"
#include "../application/AppController.h"
#include "../application/PathManager.h"
#include "../util/CrashRecovery.h"
#include "../util/LegacyNoteResourceHelper.h"
#include "../util/LegacyNoteTextCodec.h"
#include "../util/Logger.h"
#include "../util/FileUtils.h"
#include "../util/PathValidator.h"
#include "../util/PdfImportPlan.h"
#include "Version.h"

#include <cairo-pdf.h>
#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pango/pangocairo.h>
#include <windows.h>
#include <cmath>
#include <vector>
#include <string>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <climits>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// ============================================================
// Data structures
// ============================================================
struct StrokeData {
    std::vector<double> x, y;
    double w = 2, r = 0, g = 0, b = 0, a = 1;
    int tool = 0; // 0=pen 1=highlight 2=eraser
    void addPt(double px, double py) { x.push_back(px); y.push_back(py); }
};

struct ImgEl {
    cairo_surface_t* surf = nullptr;
    double x = 50, y = 50, w = 200, h = 150;
    int rotateAngle = 0;  // 0, 90, 180, 270
    std::string srcFile; // keep source for reload
    ~ImgEl() { if(surf) cairo_surface_destroy(surf); }
    ImgEl() = default;
    ImgEl(ImgEl&& o) noexcept : surf(o.surf),x(o.x),y(o.y),w(o.w),h(o.h),rotateAngle(o.rotateAngle),srcFile(std::move(o.srcFile)) { o.surf=nullptr; }
    ImgEl& operator=(ImgEl&& o) noexcept {
        if(surf)cairo_surface_destroy(surf);
        surf=o.surf;x=o.x;y=o.y;w=o.w;h=o.h;rotateAngle=o.rotateAngle;srcFile=std::move(o.srcFile);o.surf=nullptr;return *this;
    }
    ImgEl(const ImgEl&) = delete;
    ImgEl& operator=(const ImgEl&) = delete;
};

struct TxtEl {
    std::string text;
    double x = 50, y = 100, fontSize = 14, r = 0, g = 0, b = 0;
};

struct PageData {
    std::vector<StrokeData> strokes;
    std::vector<ImgEl> images;
    std::vector<TxtEl> texts;
    cairo_surface_t* bgSurf = nullptr;
    double bgW = 0, bgH = 0;
    std::string bgFile;
    double pw = 595, ph = 842;
    int pdfPageNum = -1;         // 若來自 PDF，記錄原始頁碼
    ~PageData() { if(bgSurf) cairo_surface_destroy(bgSurf); }
    PageData() = default;
    PageData(PageData&& o) noexcept
        : strokes(std::move(o.strokes)), images(std::move(o.images)),
          texts(std::move(o.texts)), bgSurf(o.bgSurf), bgW(o.bgW), bgH(o.bgH),
          bgFile(std::move(o.bgFile)), pw(o.pw), ph(o.ph), pdfPageNum(o.pdfPageNum) { o.bgSurf = nullptr; }
    PageData& operator=(PageData&& o) noexcept {
        if(bgSurf)cairo_surface_destroy(bgSurf);
        strokes=std::move(o.strokes);images=std::move(o.images);
        texts=std::move(o.texts);bgSurf=o.bgSurf;bgW=o.bgW;bgH=o.bgH;
        bgFile=std::move(o.bgFile);pw=o.pw;ph=o.ph;pdfPageNum=o.pdfPageNum;o.bgSurf=nullptr;return *this;
    }
    PageData(const PageData&) = delete;
    PageData& operator=(const PageData&) = delete;
};

struct NoteData {
    std::string name;
    std::vector<PageData> pages;
    int dirty = 0;
};

// ============================================================
// Global state
// ============================================================
static struct {
    GtkWidget* window = nullptr;
    GtkWidget* drawingArea = nullptr;
    cairo_surface_t* canvasSurf = nullptr;
    GtkWidget* noteList = nullptr;
    GtkWidget* searchEntry = nullptr;      // 搜尋框
    std::string searchTerm;               // 當前搜尋关键词
    GtkWidget* pageThumbs = nullptr;     // 縮圖側欄
    std::vector<cairo_surface_t*> pageThumbSurf;  // 縮圖 surface
    GtkWidget* lblZoom = nullptr;
    GtkWidget* lblPage = nullptr;
    GtkWidget* lblStatus = nullptr;
    GtkWidget* propPanel = nullptr;  // Properties panel for selected item
    GtkWidget* propFontSize = nullptr;
    GtkWidget* propColorBtn = nullptr;
    GtkWidget* propLabel = nullptr;

    // Overlay for text entry (GtkOverlay wrapping drawingArea + text widgets)
    GtkWidget* overlay = nullptr;
    GtkWidget* textEntry = nullptr;       // GtkEntry for single-line
    GtkWidget* textSizeSpin = nullptr;    // Font size spinner
    GtkWidget* textMultiView = nullptr;   // GtkTextView for multi-line
    GtkWidget* textMultiScrolled = nullptr;
    GtkWidget* textOverlayBox = nullptr;  // Box holding entry+spin
    GtkWidget* textMultiBox = nullptr;    // Box for multi-line mode (view + buttons)
    int textEditMode = 0;                 // 0=new text, 1=edit existing
    int textEditIdx = -1;                 // Index of text being edited
    double textEntryX = 0, textEntryY = 0;

    int drawing = 0;
    double lastX = 0, lastY = 0;
    int tool = 0; // 0=pen,1=hl,2=eraser,3=text,4=select
    double penW = 2.0, penR = 0, penG = 0, penB = 0;
    double zoom = 1.0;
    int margins[4] = {25, 15, 25, 15};

    std::vector<NoteData> notes;
    int selNote = -1, selPage = 0;
    int selImg = -1, selTxt = -1, selStroke = -1;
    int selBg = 0; // 1 = background selected
    int dragging = 0, resizing = 0;
    int groupDragging = 0; // 1 = dragging multiple selected objects together
    double dragOffX = 0, dragOffY = 0, selResizeW = 0, selResizeH = 0;
    double selResizeOrigW = 0, selResizeOrigH = 0; // For bg resize
    double imgRotateAngle = 0;  // 圖片旋轉角度（0, 90, 180, 270）
    double pageScrollY = 0; // Vertical scroll offset when zoomed
    double pageScrollX = 0; // Horizontal scroll offset when zoomed

    // Rubber band selection
    int selBoxActive = 0;     // 1 = drawing selection box
    double selBoxX = 0, selBoxY = 0, selBoxW = 0, selBoxH = 0;
    std::vector<int> selTexts;    // multiple selected text indices
    std::vector<int> selStrokes;  // multiple selected stroke indices
    std::vector<int> selImages;   // multiple selected image indices
    int pendingMultiToggle = 0;
    double multiPressX = 0, multiPressY = 0;
    int mouseUndoActive = 0;

    // Auto-save
    guint autoSaveTimer = 0;
    bool shuttingDown = false;

    // Undo/Redo stacks
    std::vector<std::string> undoStack;
    std::vector<std::string> redoStack;

    // Crash recovery
    std::string crashRecoveryFile;
    std::string crashRecoveryMarkerFile;

    AppController* ctrl = nullptr;
} G;

static const char* TOOL_NAMES[] = {
    "\xe2\x9c\x8f\xe7\xad\x86",       // ✏筆
    "\xf0\x9f\x96\x8d\xe6\xa9\x99",   // 🖍螢
    "\xe2\x9c\x96\xe6\xa9\xa1",       // ✖橡
    "\xf0\x9f\x94\xa4\xe6\x96\x87",   // 🔤文
    "\xe2\x98\x9e\xe9\x81\xb8",       // ☞選
};

// ============================================================
// Forward declarations
// ============================================================
static void renderCanvas();
static void rebuildThumbs();
static void updateWindowTitle();
static std::string serializeNote(const NoteData* nd);
static void deserializeNoteToCurrent(const std::string& data);
static void on_undo(GtkButton*, gpointer);
static void on_redo(GtkButton*, gpointer);
static void pushUndo();
static PageData* curPage();
static FILE* wfopen_utf8(const std::string& path_utf8, const wchar_t* mode);
static void hideTextEntry();
static void on_note_rename(GtkMenuItem*, gpointer);
static void on_note_delete(GtkMenuItem*, gpointer);
static void on_save(GtkButton*, gpointer);
static void updateStatus();
static std::string get_save_dir();
static std::string get_exe_dir();
static fs::path utf8_to_path(const std::string& utf8);
static std::string note_filename(NoteData* nd);
static cairo_surface_t* load_image_surface(const char* filename);
static bool save_note_to_file(NoteData* nd);
static bool write_crash_recovery_snapshot(NoteData* nd);
static void logInfo(const char* fmt, ...);

struct DirtySaveSummary {
    int attempted = 0;
    int saved = 0;
    int failed = 0;
};

// ============================================================
// Helpers
// ============================================================
static NoteData* curNote() {
    return (G.selNote >= 0 && G.selNote < (int)G.notes.size()) ? &G.notes[static_cast<size_t>(G.selNote)] : nullptr;
}

static std::string serializeCurrentNote() { return serializeNote(curNote()); }

static fs::path crash_recovery_snapshot_path() {
    return utf8_to_path(G.crashRecoveryFile);
}

static fs::path crash_recovery_marker_path() {
    return utf8_to_path(G.crashRecoveryMarkerFile);
}

static NoteData* note_for_recovery_snapshot() {
    if (NoteData* current = curNote(); current && current->dirty) {
        return current;
    }
    for (auto& note : G.notes) {
        if (note.dirty) {
            return &note;
        }
    }
    return curNote();
}

static bool write_session_marker() {
    if (G.crashRecoveryMarkerFile.empty()) {
        return false;
    }

    std::error_code ec;
    const fs::path markerPath = crash_recovery_marker_path();
    if (!markerPath.parent_path().empty()) {
        fs::create_directories(markerPath.parent_path(), ec);
        if (ec) {
            logInfo("Failed to prepare crash recovery marker dir: %s", ec.message().c_str());
            return false;
        }
    }

    std::ofstream marker(markerPath, std::ios::trunc);
    if (!marker) {
        logInfo("Failed to create crash recovery session marker");
        return false;
    }
    marker << "active\n";
    return true;
}

static void remove_file_if_exists(const fs::path& path, const char* description) {
    if (path.empty()) return;

    std::error_code ec;
    if (fs::exists(path, ec) && !ec) {
        fs::remove(path, ec);
        if (ec) {
            logInfo("Failed to remove %s: %s", description, ec.message().c_str());
        }
    }
}

static void clear_crash_recovery_artifacts() {
    remove_file_if_exists(crash_recovery_snapshot_path(), "crash recovery snapshot");
    remove_file_if_exists(crash_recovery_marker_path(), "crash recovery session marker");
}

static DirtySaveSummary save_dirty_notes(const char* reason) {
    DirtySaveSummary summary;
    for (auto& note : G.notes) {
        if (!note.dirty) {
            continue;
        }

        summary.attempted++;
        if (save_note_to_file(&note)) {
            summary.saved++;
            logInfo("%s succeeded: %s", reason, note.name.c_str());
        } else {
            summary.failed++;
            logInfo("%s failed: %s", reason, note.name.c_str());
        }
    }
    return summary;
}

static void show_shutdown_save_warning(const DirtySaveSummary& summary, bool snapshotSaved) {
    char msg[512];
    snprintf(msg, sizeof(msg),
             "Some notes could not be saved before exit.\n\n"
             "Saved: %d\nFailed: %d\n"
             "Crash recovery snapshot: %s\n\n"
             "The next launch will keep a recovery copy instead of pretending the shutdown completed cleanly.",
             summary.saved, summary.failed, snapshotSaved ? "updated" : "failed");

    GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void persist_notes_for_shutdown() {
    const DirtySaveSummary summary = save_dirty_notes("Shutdown save");
    if (summary.failed == 0) {
        clear_crash_recovery_artifacts();
        return;
    }

    const bool snapshotSaved = write_crash_recovery_snapshot(note_for_recovery_snapshot());
    show_shutdown_save_warning(summary, snapshotSaved);
}

static bool is_page_empty_for_pdf_import(const PageData& page) {
    return page.strokes.empty() &&
           page.images.empty() &&
           page.texts.empty() &&
           page.bgFile.empty() &&
           page.bgSurf == nullptr;
}

static bool is_blank_note_for_pdf_import(const NoteData& note) {
    return note.pages.size() == 1 && is_page_empty_for_pdf_import(note.pages.front());
}

static std::string pdf_import_note_name(const std::string& pdfPath) {
    const std::string stem = utf8_to_path(pdfPath).stem().u8string();
    return stem.empty() ? "Imported PDF" : stem;
}

static PdfImportPlan::FitRect fitted_background_rect(const PageData* pg,
                                                     double containerWidth,
                                                     double containerHeight) {
    if (!pg) {
        return {};
    }

    double bgWidth = pg->bgW;
    double bgHeight = pg->bgH;
    if ((bgWidth <= 0.0 || bgHeight <= 0.0) &&
        pg->bgSurf &&
        cairo_surface_status(pg->bgSurf) == CAIRO_STATUS_SUCCESS) {
        bgWidth = cairo_image_surface_get_width(pg->bgSurf);
        bgHeight = cairo_image_surface_get_height(pg->bgSurf);
    }

    return PdfImportPlan::fitContain(containerWidth, containerHeight, bgWidth, bgHeight);
}

static bool draw_page_background(cairo_t* cr,
                                 const PageData* pg,
                                 double containerX,
                                 double containerY,
                                 double containerWidth,
                                 double containerHeight,
                                 PdfImportPlan::FitRect* fittedRect = nullptr) {
    if (!cr || !pg || !pg->bgSurf || cairo_surface_status(pg->bgSurf) != CAIRO_STATUS_SUCCESS) {
        if (fittedRect) *fittedRect = {};
        return false;
    }

    const PdfImportPlan::FitRect fit = fitted_background_rect(pg, containerWidth, containerHeight);
    if (fittedRect) *fittedRect = fit;
    if (!fit.valid) {
        return false;
    }

    cairo_save(cr);
    cairo_translate(cr, containerX + fit.offsetX, containerY + fit.offsetY);
    cairo_scale(cr, fit.scale, fit.scale);
    cairo_set_source_surface(cr, pg->bgSurf, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
    return true;
}

static void clamp_page_scroll(const PageData* pg) {
    if (!pg || !G.drawingArea) {
        G.pageScrollX = 0;
        G.pageScrollY = 0;
        return;
    }

    const int viewportWidth = gtk_widget_get_allocated_width(G.drawingArea);
    const int viewportHeight = gtk_widget_get_allocated_height(G.drawingArea);
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return;
    }

    const double contentWidth = pg->pw * G.zoom + G.margins[0] + G.margins[2];
    const double contentHeight = pg->ph * G.zoom + G.margins[1] + G.margins[3];
    const double minScrollX = std::min(0.0, static_cast<double>(viewportWidth) - contentWidth);
    const double minScrollY = std::min(0.0, static_cast<double>(viewportHeight) - contentHeight);

    G.pageScrollX = std::min(0.0, std::max(minScrollX, G.pageScrollX));
    G.pageScrollY = std::min(0.0, std::max(minScrollY, G.pageScrollY));
}

static void invalidate_canvas_and_refresh(bool refreshStatus = true) {
    if (G.canvasSurf) {
        cairo_surface_destroy(G.canvasSurf);
        G.canvasSurf = nullptr;
    }
    renderCanvas();
    if (refreshStatus) {
        updateStatus();
    }
}

static void apply_zoom_factor(double factor, double anchorX = -1.0, double anchorY = -1.0) {
    PageData* pg = curPage();
    if (!pg) {
        return;
    }

    const double oldZoom = G.zoom;
    const double newZoom = fmax(0.1, fmin(5.0, oldZoom * factor));
    if (fabs(newZoom - oldZoom) < 1e-9) {
        return;
    }

    if (!G.drawingArea) {
        G.zoom = newZoom;
        G.pageScrollX = 0;
        G.pageScrollY = 0;
        return;
    }

    const int viewportWidth = gtk_widget_get_allocated_width(G.drawingArea);
    const int viewportHeight = gtk_widget_get_allocated_height(G.drawingArea);
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        G.zoom = newZoom;
        G.pageScrollX = 0;
        G.pageScrollY = 0;
        return;
    }

    if (anchorX < 0.0) anchorX = viewportWidth / 2.0;
    if (anchorY < 0.0) anchorY = viewportHeight / 2.0;

    const double docX = (anchorX - G.margins[0] - G.pageScrollX) / oldZoom;
    const double docY = (anchorY - G.margins[1] - G.pageScrollY) / oldZoom;

    G.zoom = newZoom;
    G.pageScrollX = anchorX - G.margins[0] - docX * G.zoom;
    G.pageScrollY = anchorY - G.margins[1] - docY * G.zoom;
    clamp_page_scroll(pg);
}

static void pushUndo() {
    std::string snap = serializeCurrentNote();
    if (!snap.empty()) {
        G.undoStack.push_back(snap);
        if (G.undoStack.size() > 50) G.undoStack.erase(G.undoStack.begin());
        G.redoStack.clear();
    }
}

static void beginMouseUndo() {
    if (G.mouseUndoActive) return;
    pushUndo();
    G.mouseUndoActive = 1;
}

static PageData* curPage() {
    NoteData* n = curNote();
    return (n && G.selPage >= 0 && G.selPage < (int)n->pages.size()) ? &n->pages[static_cast<size_t>(G.selPage)] : nullptr;
}

static bool indexInSelection(const std::vector<int>& indices, int value) {
    return std::find(indices.begin(), indices.end(), value) != indices.end();
}

static bool removeSelectedIndex(std::vector<int>& indices, int value) {
    auto it = std::find(indices.begin(), indices.end(), value);
    if (it == indices.end()) return false;
    indices.erase(it);
    return true;
}

static bool hasMultiSelection() {
    return !G.selTexts.empty() || !G.selStrokes.empty() || !G.selImages.empty();
}

template <typename T>
static bool validIndex(const std::vector<T>& items, int index) {
    return index >= 0 && static_cast<size_t>(index) < items.size();
}

template <typename T>
static T* itemAt(std::vector<T>& items, int index) {
    return validIndex(items, index) ? &items[static_cast<size_t>(index)] : nullptr;
}

static int boundedIntCount(size_t value) {
    return value > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(value);
}

template <typename T>
static bool eraseSelectedIndices(std::vector<T>& items, std::vector<int>& selected) {
    std::sort(selected.begin(), selected.end(), std::greater<int>());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());

    bool deleted = false;
    for (int index : selected) {
        if (!validIndex(items, index)) continue;
        items.erase(items.begin() + static_cast<typename std::vector<T>::difference_type>(index));
        deleted = true;
    }
    selected.clear();
    return deleted;
}

static bool eraseSelectedItems(PageData* pg) {
    if (!pg) return false;

    bool deleted = eraseSelectedIndices(pg->texts, G.selTexts);
    deleted = eraseSelectedIndices(pg->strokes, G.selStrokes) || deleted;
    deleted = eraseSelectedIndices(pg->images, G.selImages) || deleted;

    if (deleted) {
        G.selTxt = -1;
        G.selStroke = -1;
        G.selImg = -1;
        G.groupDragging = 0;
    }
    return deleted;
}

static bool toggleHitFromMultiSelection(PageData* pg, double px, double py) {
    if (!pg || !hasMultiSelection()) return false;

    const double strokeHitDist = 15.0 / G.zoom;
    for (int si : G.selStrokes) {
        StrokeData* s = itemAt(pg->strokes, si);
        if (!s) continue;
        for (size_t j = 0; j < s->x.size(); j++) {
            const double dx = px - s->x[j];
            const double dy = py - s->y[j];
            if (sqrt(dx * dx + dy * dy) < strokeHitDist) {
                removeSelectedIndex(G.selStrokes, si);
                return true;
            }
        }
    }

    for (int ti : G.selTexts) {
        TxtEl* t = itemAt(pg->texts, ti);
        if (!t) continue;
        const double pad = 15.0;
        if (px >= t->x - pad && py >= t->y - t->fontSize * 1.2 - pad &&
            px <= t->x + 200 && py <= t->y + pad) {
            removeSelectedIndex(G.selTexts, ti);
            return true;
        }
    }

    for (int ii : G.selImages) {
        ImgEl* img = itemAt(pg->images, ii);
        if (!img) continue;
        if (px >= img->x && px <= img->x + img->w &&
            py >= img->y && py <= img->y + img->h) {
            removeSelectedIndex(G.selImages, ii);
            return true;
        }
    }

    return false;
}

static bool hitMultiSelection(PageData* pg, double px, double py) {
    if (!pg || !hasMultiSelection()) return false;

    const double strokeHitDist = 15.0 / G.zoom;
    for (int si : G.selStrokes) {
        StrokeData* s = itemAt(pg->strokes, si);
        if (!s) continue;
        for (size_t j = 0; j < s->x.size(); j++) {
            const double dx = px - s->x[j];
            const double dy = py - s->y[j];
            if (sqrt(dx * dx + dy * dy) < strokeHitDist) return true;
        }
    }

    for (int ti : G.selTexts) {
        TxtEl* t = itemAt(pg->texts, ti);
        if (!t) continue;
        const double pad = 15.0;
        if (px >= t->x - pad && py >= t->y - t->fontSize * 1.2 - pad &&
            px <= t->x + 200 && py <= t->y + pad) {
            return true;
        }
    }

    for (int ii : G.selImages) {
        ImgEl* img = itemAt(pg->images, ii);
        if (!img) continue;
        if (px >= img->x && px <= img->x + img->w &&
            py >= img->y && py <= img->y + img->h) {
            return true;
        }
    }

    return false;
}

static void setDashedLine(cairo_t* cr, double first, double second) {
    const double dash[] = { first, second };
    cairo_set_dash(cr, dash, 2, 0);
}

static char lowerByte(char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

static void destroyChild(GtkWidget* child, gpointer) {
    gtk_widget_destroy(child);
}

static void logInfo(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Logger::info(std::string(buf));
}

static void updateStatus() {
    if (!G.lblStatus) return;
    NoteData* n = curNote(); PageData* pg = curPage();
    char buf[256];
    const char* nn = (n && !n->name.empty()) ? n->name.c_str() : "(無)";
    const char* ori = (pg && pg->pw > pg->ph) ? "橫" : "直";
    int npg = n ? (int)n->pages.size() : 0;
    int nst = pg ? (int)pg->strokes.size() : 0;
    int ntx = pg ? (int)pg->texts.size() : 0;
    int nim = pg ? (int)pg->images.size() : 0;

    // Show multi-selection count
    char selInfo[64] = "";
    int totalSel = boundedIntCount(G.selTexts.size() + G.selStrokes.size() + G.selImages.size());
    if (totalSel > 0) {
        snprintf(selInfo, sizeof(selInfo), " [已選%d]", totalSel);
    }

    snprintf(buf, sizeof(buf), "%s %s %d/%d 筆:%d 文:%d 圖:%d%s | %s",
             nn, ori, G.selPage+1, npg, nst, ntx, nim, selInfo, TOOL_NAMES[G.tool]);
    gtk_label_set_text(GTK_LABEL(G.lblStatus), buf);
    updateWindowTitle();
    if (G.lblZoom) {
        char z[64];
        snprintf(z, sizeof(z), "%d%%", (int)(G.zoom*100));
        gtk_label_set_text(GTK_LABEL(G.lblZoom), z);
        gtk_widget_set_tooltip_text(G.lblZoom, "Ctrl+滾輪: 放大/縮小\nShift+滾輪: 水平平移\n滾輪: 垂直平移\nShift+點擊頁面: 上一頁/下一頁");
    }
    if (G.lblPage) {
        char p[16];
        snprintf(p,sizeof(p),"%d/%d",G.selPage+1,npg);
        gtk_label_set_text(GTK_LABEL(G.lblPage), p);
        gtk_widget_set_tooltip_text(G.lblPage, "點擊側邊欄筆記旁的 ✎ 可重命名\n點擊 ✖ 可刪除筆記");
    }
}

static void updateWindowTitle() {
    NoteData* n = curNote();
    char title[512];
    const char* nn = (n && !n->name.empty()) ? n->name.c_str() : "未命名";
    int pageIdx = G.selPage + 1;
    int totalPages = n ? (int)n->pages.size() : 0;
    snprintf(title, sizeof(title), "OfflineNote - %s (第 %d/%d 頁)", nn, pageIdx, totalPages);
    gtk_window_set_title(GTK_WINDOW(G.window), title);
}

// Auto-save callback (called every 30 seconds)
static gboolean auto_save_callback(gpointer) {
    if (G.shuttingDown) return FALSE;
    const DirtySaveSummary summary = save_dirty_notes("Auto-save");
    if (summary.saved > 0) {
        logInfo("Auto-saved %d notes", summary.saved);
    }
    if (summary.failed > 0) {
        logInfo("Auto-save failed for %d notes", summary.failed);
    }
    if (summary.attempted > 0) {
        if (write_crash_recovery_snapshot(note_for_recovery_snapshot())) {
            logInfo("Crash recovery snapshot updated");
        } else {
            logInfo("Crash recovery snapshot update failed");
        }
    }
    return TRUE; // keep running
}

// Update properties panel based on current selection
static void updatePropPanel() {
    if (!G.propPanel) return;
    PageData* pg = curPage();
    if (!pg) { gtk_widget_hide(G.propPanel); return; }

    if (TxtEl* t = itemAt(pg->texts, G.selTxt)) {
        // Text selected - show text properties
        gtk_label_set_text(GTK_LABEL(G.propLabel), "文字屬性");
        if (G.propFontSize) gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.propFontSize), t->fontSize);
        if (G.propColorBtn) {
            GdkRGBA c = {t->r, t->g, t->b, 1.0};
            gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(G.propColorBtn), &c);
        }
        gtk_widget_show_all(G.propPanel);
    } else if (ImgEl* img = itemAt(pg->images, G.selImg)) {
        // Image selected
        char info[64];
        snprintf(info, sizeof(info), "圖片: %.0f x %.0f", img->w, img->h);
        gtk_label_set_text(GTK_LABEL(G.propLabel), info);
        if (G.propFontSize) gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.propFontSize), 0);
        gtk_widget_show_all(G.propPanel);
    } else {
        gtk_widget_hide(G.propPanel);
    }
}

static void on_prop_font_changed(GtkSpinButton*, gpointer) {
    PageData* pg = curPage();
    TxtEl* text = pg ? itemAt(pg->texts, G.selTxt) : nullptr;
    if (!text) return;
    text->fontSize = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.propFontSize));
    NoteData* n = curNote(); if(n) n->dirty = 1;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas();
}

static void on_prop_color_changed(GtkWidget* btn, gpointer) {
    PageData* pg = curPage();
    TxtEl* text = pg ? itemAt(pg->texts, G.selTxt) : nullptr;
    if (!text) return;
    GdkRGBA c;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &c);
    text->r = c.red;
    text->g = c.green;
    text->b = c.blue;
    NoteData* n = curNote(); if(n) n->dirty = 1;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas();
}

// ============================================================
// Text entry - multi-line support with edit capability
// ============================================================
static void commitTextEntry() {
    // Get text from multi-line view
    GtkTextBuffer* buf = G.textMultiView ? gtk_text_view_get_buffer(GTK_TEXT_VIEW(G.textMultiView)) : nullptr;
    if (!buf) return;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    gchar* txt = gtk_text_buffer_get_text(buf, &start, &end, FALSE);

    PageData* pg = curPage();
    if (pg && txt && strlen(txt) > 0) {
        int fs = 14;
        if (G.textSizeSpin) fs = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.textSizeSpin));

        double px = (G.textEntryX - G.margins[0] - G.pageScrollX) / G.zoom;
        double py = (G.textEntryY - G.margins[1] - G.pageScrollY) / G.zoom;

        bool changed = false;
        if (G.textEditMode) {
            // Edit existing text
            TxtEl* text = itemAt(pg->texts, G.textEditIdx);
            if (text) {
                pushUndo();
                text->text = txt;
                text->fontSize = fs;
                changed = true;
            }
            G.textEditMode = 0;
            G.textEditIdx = -1;
        } else {
            // New text
            pushUndo();
            pg->texts.push_back(TxtEl());
            TxtEl* t = &pg->texts.back();
            t->text = txt;
            t->x = fmax(0.0, px);
            t->y = fmax(0.0, py);
            t->fontSize = fs;
            t->r = G.penR; t->g = G.penG; t->b = G.penB;
            changed = true;
        }
        if (changed) {
            NoteData* nd = curNote(); if(nd) nd->dirty = 1;
        }
    }

    g_free(txt);

    // Clear and hide
    if (buf) gtk_text_buffer_set_text(buf, "", -1);
    if (G.textMultiBox) gtk_widget_hide(G.textMultiBox);
    G.textEditMode = 0;
    G.textEditIdx = -1;

    // Force full canvas redraw
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas();
}

static void showTextEditor(double sx, double sy, const char* existingText = nullptr) {
    if (!G.drawingArea || !G.overlay) return;

    G.textEntryX = sx;
    G.textEntryY = sy;

    if (!G.textMultiBox) {
        // Create multi-line text editor
        G.textMultiBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_valign(G.textMultiBox, GTK_ALIGN_START);
        gtk_widget_set_halign(G.textMultiBox, GTK_ALIGN_START);
        gtk_widget_set_visible(G.textMultiBox, FALSE);

        // Scrolled window for text view
        G.textMultiScrolled = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(G.textMultiScrolled),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_size_request(G.textMultiScrolled, 300, 120);

        G.textMultiView = gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(G.textMultiView), GTK_WRAP_WORD_CHAR);
        gtk_container_add(GTK_CONTAINER(G.textMultiScrolled), G.textMultiView);

        gtk_box_pack_start(GTK_BOX(G.textMultiBox), G.textMultiScrolled, TRUE, TRUE, 0);

        // Button bar: 字型大小 + 換行 + 完成 + 取消
        GtkWidget* btnBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

        G.textSizeSpin = gtk_spin_button_new_with_range(8, 72, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.textSizeSpin), 14);
        gtk_widget_set_size_request(G.textSizeSpin, 50, -1);
        gtk_box_pack_start(GTK_BOX(btnBar), G.textSizeSpin, FALSE, FALSE, 0);

        // 換行 button - inserts newline at cursor
        GtkWidget* btnNewline = gtk_button_new_with_label("↵ 換行");
        gtk_widget_set_size_request(btnNewline, 60, -1);
        g_signal_connect(btnNewline, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
            GtkTextBuffer* buf = G.textMultiView ? gtk_text_view_get_buffer(GTK_TEXT_VIEW(G.textMultiView)) : nullptr;
            if (!buf) return;
            GtkTextIter iter;
            gtk_text_buffer_get_iter_at_mark(buf, &iter, gtk_text_buffer_get_insert(buf));
            gtk_text_buffer_insert(buf, &iter, "\n", -1);
            // Re-focus the text view
            gtk_widget_grab_focus(G.textMultiView);
        }), nullptr);
        gtk_box_pack_start(GTK_BOX(btnBar), btnNewline, FALSE, FALSE, 0);

        // 完成 button
        GtkWidget* btnDone = gtk_button_new_with_label("✓ 完成");
        gtk_widget_set_size_request(btnDone, 60, -1);
        g_signal_connect(btnDone, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
            commitTextEntry();
        }), nullptr);
        gtk_box_pack_start(GTK_BOX(btnBar), btnDone, FALSE, FALSE, 0);

        // 取消 button
        GtkWidget* btnCancel = gtk_button_new_with_label("✗ 取消");
        gtk_widget_set_size_request(btnCancel, 60, -1);
        g_signal_connect(btnCancel, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
            if (G.textMultiBox) gtk_widget_hide(G.textMultiBox);
            G.textEditMode = 0;
            G.textEditIdx = -1;
            if (G.textMultiView) {
                GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(G.textMultiView));
                gtk_text_buffer_set_text(buf, "", -1);
            }
        }), nullptr);
        gtk_box_pack_start(GTK_BOX(btnBar), btnCancel, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(G.textMultiBox), btnBar, FALSE, FALSE, 0);

        gtk_overlay_add_overlay(GTK_OVERLAY(G.overlay), G.textMultiBox);
    }

    // Set existing text if editing
    if (existingText && strlen(existingText) > 0) {
        GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(G.textMultiView));
        gtk_text_buffer_set_text(buf, existingText, -1);
    } else {
        GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(G.textMultiView));
        gtk_text_buffer_set_text(buf, "", -1);
    }

    // Position and show
    gtk_widget_set_margin_start(G.textMultiBox, (int)sx);
    gtk_widget_set_margin_top(G.textMultiBox, (int)sy);
    gtk_widget_show_all(G.textMultiBox);
    gtk_widget_grab_focus(G.textMultiView);
}

static void hideTextEntry() {
    // Legacy compatibility - hide multi-line editor
    if (G.textMultiBox) gtk_widget_hide(G.textMultiBox);
    if (G.textOverlayBox) gtk_widget_hide(G.textOverlayBox);
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas();
}

[[maybe_unused]] static void ensureTextOverlay() {
    // Legacy: kept for compatibility, actual work done in showTextEditor
    if (G.textOverlayBox) return;
    G.textOverlayBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_valign(G.textOverlayBox, GTK_ALIGN_START);
    gtk_widget_set_halign(G.textOverlayBox, GTK_ALIGN_START);
    gtk_widget_set_visible(G.textOverlayBox, FALSE);
    G.textEntry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(G.textOverlayBox), G.textEntry, TRUE, TRUE, 0);
    gtk_overlay_add_overlay(GTK_OVERLAY(G.overlay), G.textOverlayBox);
}

static void showTextEntry(double sx, double sy) {
    showTextEditor(sx, sy);
}

// ============================================================
// Canvas rendering - always fresh
// ============================================================
static void renderCanvas() {
    PageData* pg = curPage();
    if (!pg || !G.drawingArea) return;

    int allocW = gtk_widget_get_allocated_width(G.drawingArea);
    int allocH = gtk_widget_get_allocated_height(G.drawingArea);
    if (allocW <= 0 || allocH <= 0) return;

    clamp_page_scroll(pg);

    int pw = (int)(pg->pw * G.zoom), ph = (int)(pg->ph * G.zoom);
    int sw = pw + G.margins[0] + G.margins[2], sh = ph + G.margins[1] + G.margins[3];

    // Apply scroll offsets
    int offsetY = (int)G.pageScrollY;
    int offsetX = (int)G.pageScrollX;
    int topMargin = G.margins[1] + offsetY;
    int leftMargin = G.margins[0] + offsetX;

    // Always recreate with extra space for scrolling
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    int surfW = sw + abs(offsetX) + 100;
    int surfH = sh + abs(offsetY) + 100;
    G.canvasSurf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surfW, surfH);
    if (cairo_surface_status(G.canvasSurf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; return;
    }

    cairo_t* cr = cairo_create(G.canvasSurf);

    // Background
    cairo_set_source_rgb(cr, 0.82, 0.82, 0.82); cairo_paint(cr);

    // Page shadow
    cairo_set_source_rgba(cr, 0, 0, 0, 0.08);
    cairo_rectangle(cr, leftMargin+4, topMargin+4, pw, ph); cairo_fill(cr);

    // Page
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, leftMargin, topMargin, pw, ph); cairo_fill(cr);

    // Border
    cairo_set_source_rgb(cr, 0.4, 0.4, 0.4); cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, leftMargin+0.5, topMargin+0.5, pw, ph); cairo_stroke(cr);

    // Background image
    if (pg->bgSurf && cairo_surface_status(pg->bgSurf)==CAIRO_STATUS_SUCCESS) {
        PdfImportPlan::FitRect fit;
        draw_page_background(cr, pg, leftMargin, topMargin, pw, ph, &fit);
        const double bx = leftMargin + fit.offsetX;
        const double by = topMargin + fit.offsetY;
        const double bw = fit.width;
        const double bh = fit.height;

        // Selection indicator for background
        if (G.selBg && fit.valid) {
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.7);
            cairo_set_line_width(cr, 2);
            setDashedLine(cr, 6, 3);
            cairo_rectangle(cr, bx, by, bw, bh);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0);

            // Resize handle
            cairo_set_source_rgb(cr, 0.1, 0.4, 0.9);
            cairo_rectangle(cr, bx+bw-16, by+bh-16, 16, 16); cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_rectangle(cr, bx+bw-13, by+bh-13, 10, 10); cairo_fill(cr);
        }
    }

    // Images
    for (size_t i = 0; i < pg->images.size(); i++) {
        ImgEl* img = &pg->images[i];
        if (!img->surf || cairo_surface_status(img->surf)!=CAIRO_STATUS_SUCCESS) continue;
        double ix=leftMargin+img->x*G.zoom, iy=topMargin+img->y*G.zoom;
        double iw=img->w*G.zoom, ih=img->h*G.zoom;
        
        // Get original image dimensions
        int ow = cairo_image_surface_get_width(img->surf);
        int oh = cairo_image_surface_get_height(img->surf);
        
        // Scale image to fit the specified size
        double sx = iw / fmax(1, ow);
        double sy = ih / fmax(1, oh);

        cairo_save(cr);
        cairo_translate(cr, ix, iy);

        // Apply rotation if set
        if (img->rotateAngle != 0) {
            cairo_translate(cr, iw/2.0, ih/2.0);  // Move to center
            cairo_rotate(cr, img->rotateAngle * G_PI / 180.0);
            cairo_translate(cr, -iw/2.0, -ih/2.0);  // Move back
        }

        cairo_scale(cr, sx, sy);
        cairo_set_source_surface(cr, img->surf, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);

        const int imageIndex = i > static_cast<size_t>(INT_MAX) ? -1 : static_cast<int>(i);
        if (imageIndex == G.selImg || indexInSelection(G.selImages, imageIndex)) {
            // Selection border - dashed, more visible
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.8);
            cairo_set_line_width(cr, 3);
            setDashedLine(cr, 6, 3);
            cairo_rectangle(cr, ix, iy, iw, ih);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0);

            // Corner handles for resize
            cairo_set_source_rgb(cr, 0.1, 0.4, 0.9);
            cairo_rectangle(cr, ix+iw-16, iy+ih-16, 16, 16); cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_rectangle(cr, ix+iw-13, iy+ih-13, 10, 10); cairo_fill(cr);

            // Resize arrows inside handle
            cairo_set_source_rgb(cr, 0.1, 0.4, 0.9);
            cairo_set_line_width(cr, 2);
            // Arrow pointing down-right
            cairo_move_to(cr, ix+iw-11, iy+ih-11);
            cairo_line_to(cr, ix+iw-5, iy+ih-5);
            cairo_stroke(cr);
            cairo_move_to(cr, ix+iw-5, iy+ih-5);
            cairo_line_to(cr, ix+iw-9, iy+ih-5);
            cairo_move_to(cr, ix+iw-5, iy+ih-5);
            cairo_line_to(cr, ix+iw-5, iy+ih-9);
            cairo_stroke(cr);

            // Dimension text
            char dimTxt[32];
            snprintf(dimTxt, sizeof(dimTxt), "%.0fx%.0f", iw, ih);
            cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 11);
            cairo_text_extents_t te;
            cairo_text_extents(cr, dimTxt, &te);
            double tx = ix + (iw - te.width) / 2;
            double ty = iy + te.height + 4;
            cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
            cairo_rectangle(cr, tx-2, ty-te.height-1, te.width+4, te.height+3);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.1, 0.3, 0.7);
            cairo_move_to(cr, tx, ty);
            cairo_show_text(cr, dimTxt);
        }
    }

    // Texts (using Pango for proper Chinese/Unicode rendering)
    for (size_t i = 0; i < pg->texts.size(); i++) {
        TxtEl* t = &pg->texts[i];
        if (t->text.empty()) continue;
        double tx=leftMargin+t->x*G.zoom, ty=topMargin+t->y*G.zoom;

        // Use Pango for proper Unicode/Chinese rendering
        PangoLayout* layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, t->text.c_str(), -1);
        PangoFontDescription* desc = pango_font_description_from_string(
            "Microsoft JhengHei, Noto Sans CJK TC, PMingLiU, Arial, Sans 10");
        pango_font_description_set_size(desc, (int)(t->fontSize * G.zoom * PANGO_SCALE));
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);

        cairo_set_source_rgb(cr, t->r, t->g, t->b);
        pango_cairo_update_layout(cr, layout);

        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);

        // Highlight if in multiple selection
        int isInMultiSel = 0;
        const int textIndex = i > static_cast<size_t>(INT_MAX) ? -1 : static_cast<int>(i);
        for (int si : G.selTexts) { if (si == textIndex) { isInMultiSel = 1; break; } }

        if (textIndex == G.selTxt || isInMultiSel) {
            // Dashed border for selected text
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.7);
            cairo_set_line_width(cr, 2);
            setDashedLine(cr, 4, 3);
            cairo_rectangle(cr, tx-3, ty-3, tw+6, th+6);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0);

            // Corner handles
            cairo_set_source_rgb(cr, 0.1, 0.4, 0.9);
            double hs = 6;
            cairo_rectangle(cr, tx-hs, ty-hs, hs*2, hs*2); cairo_fill(cr);
            cairo_rectangle(cr, tx+tw-hs, ty-hs, hs*2, hs*2); cairo_fill(cr);
            cairo_rectangle(cr, tx-hs, ty+th-hs, hs*2, hs*2); cairo_fill(cr);
            cairo_rectangle(cr, tx+tw-hs, ty+th-hs, hs*2, hs*2); cairo_fill(cr);
        }

        cairo_move_to(cr, tx, ty);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }

    // Strokes
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (auto& s : pg->strokes) {
        if (s.x.size() < 2) continue;
        if (s.tool == 2) {
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_set_line_width(cr, s.w*G.zoom*3);
        } else {
            cairo_set_source_rgba(cr, s.r, s.g, s.b, s.a);
            cairo_set_line_width(cr, s.w*G.zoom);
        }
        cairo_move_to(cr, leftMargin+s.x[0]*G.zoom, topMargin+s.y[0]*G.zoom);
        for (size_t pi = 1; pi < s.x.size(); pi++)
            cairo_line_to(cr, leftMargin+s.x[pi]*G.zoom, topMargin+s.y[pi]*G.zoom);
        cairo_stroke(cr);
    }

    // Highlight selected strokes with glow effect
    for (size_t strokeOffset = 0; strokeOffset < pg->strokes.size(); strokeOffset++) {
        if (strokeOffset > static_cast<size_t>(INT_MAX)) continue;
        const int strokeIndex = static_cast<int>(strokeOffset);
        if (strokeIndex != G.selStroke && !indexInSelection(G.selStrokes, strokeIndex)) continue;
        StrokeData* s = &pg->strokes[strokeOffset];
        if (s->x.size() >= 2) {
            // Glow
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.25);
            cairo_set_line_width(cr, (s->w + 8) * G.zoom);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_move_to(cr, leftMargin+s->x[0]*G.zoom, topMargin+s->y[0]*G.zoom);
            for (size_t pi = 1; pi < s->x.size(); pi++)
                cairo_line_to(cr, leftMargin+s->x[pi]*G.zoom, topMargin+s->y[pi]*G.zoom);
            cairo_stroke(cr);

            // Bounding box
            double minX=s->x[0], maxX=s->x[0], minY=s->y[0], maxY=s->y[0];
            for (auto px : s->x) { if(px<minX)minX=px; if(px>maxX)maxX=px; }
            for (auto py : s->y) { if(py<minY)minY=py; if(py>maxY)maxY=py; }
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.7);
            cairo_set_line_width(cr, 2);
            setDashedLine(cr, 4, 3);
            cairo_rectangle(cr,
                leftMargin+minX*G.zoom-4, topMargin+minY*G.zoom-4,
                (maxX-minX)*G.zoom+8, (maxY-minY)*G.zoom+8);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0);
        }
    }

    // Draw rubber band selection box
    if (G.selBoxActive) {
        cairo_save(cr);
        double bx = G.selBoxX * G.zoom + leftMargin;
        double by = G.selBoxY * G.zoom + topMargin;
        double bw = G.selBoxW * G.zoom;
        double bh = G.selBoxH * G.zoom;
        cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.15);
        cairo_rectangle(cr, bx, by, bw, bh);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.8);
        cairo_set_line_width(cr, 1.5);
        setDashedLine(cr, 5, 3);
        cairo_rectangle(cr, bx, by, bw, bh);
        cairo_stroke(cr);
        cairo_set_dash(cr, nullptr, 0, 0);
        cairo_restore(cr);
    }

    cairo_destroy(cr);

    // Queue redraw
    gtk_widget_queue_draw(G.drawingArea);
}

// ============================================================
// GTK Callbacks
// ============================================================
static gboolean on_draw(GtkWidget*, cairo_t* cr, gpointer) {
    if (!G.canvasSurf) return TRUE;
    cairo_set_source_surface(cr, G.canvasSurf, 0, 0); cairo_paint(cr);
    return TRUE;
}

static gboolean on_btnpress(GtkWidget*, GdkEventButton* ev, gpointer) {
    if (ev->button != 1) return TRUE;
    PageData* pg = curPage(); if (!pg) return TRUE;

    // Account for scroll offsets in all coordinate calculations
    double px = (ev->x - G.margins[0] - G.pageScrollX) / G.zoom;
    double py = (ev->y - G.margins[1] - G.pageScrollY) / G.zoom;

    // Select tool - layered selection: strokes > texts > images
    if (G.tool == 4) {
        G.selImg = -1; G.selTxt = -1; G.selStroke = -1;
        G.dragging = 0; G.resizing = 0;
        G.pendingMultiToggle = 0;

        // Shift+Click = Start rubber band selection box
        if (ev->state & GDK_SHIFT_MASK) {
            G.selBoxActive = 1;
            G.selBoxX = px; G.selBoxY = py;
            G.selBoxW = 0; G.selBoxH = 0;
            G.selTexts.clear(); G.selStrokes.clear(); G.selImages.clear();
            return TRUE;
        }

        // 0. Multi-select refinement: click a selected object to remove it from the batch.
        if (hasMultiSelection()) {
            if (hitMultiSelection(pg, px, py)) {
                G.pendingMultiToggle = 1;
                G.dragging = 1;
                G.groupDragging = 0;
                G.dragOffX = px; G.dragOffY = py;
                G.multiPressX = px; G.multiPressY = py;
                renderCanvas(); updateStatus(); updatePropPanel();
                return TRUE;
            }

            // Click missed multi-select — clear it and fall through to single-select.
            G.selTexts.clear(); G.selStrokes.clear(); G.selImages.clear();
            G.groupDragging = 0;
        }

        // 1. Check strokes first (highest priority for content)
        double strokeHitDist = 15.0 / G.zoom;
        for (size_t offset = pg->strokes.size(); offset-- > 0;) {
            if (offset > static_cast<size_t>(INT_MAX)) continue;
            const int i = static_cast<int>(offset);
            StrokeData* s = &pg->strokes[offset];
            if (s->x.size() < 2) continue;
            for (size_t j = 0; j < s->x.size(); j++) {
                double dx = px - s->x[j], dy = py - s->y[j];
                double d = sqrt(dx*dx + dy*dy);
                if (d < strokeHitDist) {
                    G.selStroke = i;
                    G.dragging = 1;
                    G.dragOffX = px; G.dragOffY = py;
                    renderCanvas(); updateStatus(); updatePropPanel();
                    return TRUE;
                }
            }
        }

        // 2. Check texts (using Pango for precise bounding box)
        for (size_t offset = pg->texts.size(); offset-- > 0;) {
            if (offset > static_cast<size_t>(INT_MAX)) continue;
            const int i = static_cast<int>(offset);
            TxtEl* t = &pg->texts[offset];
            if (t->text.empty()) continue;

            // Use Pango to get exact bounding box
            cairo_surface_t* tmpSurf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
            cairo_t* tmpCr = cairo_create(tmpSurf);
            PangoLayout* layout = pango_cairo_create_layout(tmpCr);
            pango_layout_set_text(layout, t->text.c_str(), -1);
            PangoFontDescription* desc = pango_font_description_from_string(
                "Microsoft JhengHei, Noto Sans CJK TC, PMingLiU, Arial, Sans 10");
            pango_font_description_set_size(desc, (int)(t->fontSize * PANGO_SCALE));
            pango_layout_set_font_description(layout, desc);
            pango_font_description_free(desc);

            int tw = 0, th = 0;
            pango_layout_get_pixel_size(layout, &tw, &th);
            g_object_unref(layout);
            cairo_destroy(tmpCr);
            cairo_surface_destroy(tmpSurf);

            // Text: t->x is left edge, t->y is baseline
            // Text extends from (t->y - th) to (t->y + fontSize*0.3) for descenders
            double tx = t->x;
            double topY = t->y - th;
            double bottomY = t->y + t->fontSize * 0.3;

            // Very generous hit area
            double pad = 15.0;
            if (px >= tx-pad && px <= tx+tw+pad && py >= topY-pad && py <= bottomY+pad) {
                // Double-click = Edit existing text
                if (ev->type == GDK_2BUTTON_PRESS) {
                    G.textEditMode = 1;
                    G.textEditIdx = i;
                    double sx = ev->x, sy = ev->y;
                    showTextEditor(sx, sy, t->text.c_str());
                    return TRUE;
                }

                G.selTxt = i;
                G.dragging = 1;
                G.dragOffX = t->x - px;  // offset from click to text origin
                G.dragOffY = t->y - py;
                renderCanvas(); updateStatus(); updatePropPanel();
                return TRUE;
            }
        }

        // 3. Check images (lowest priority)
        for (size_t offset = pg->images.size(); offset-- > 0;) {
            if (offset > static_cast<size_t>(INT_MAX)) continue;
            const int i = static_cast<int>(offset);
            ImgEl* img = &pg->images[offset];
            double ix = img->x, iy = img->y, iw = img->w, ih = img->h;
            // Resize handle
            if (px >= ix+iw-18/G.zoom && px <= ix+iw && py >= iy+ih-18/G.zoom && py <= iy+ih) {
                G.selImg = i;
                G.resizing = 1;
                G.dragOffX = px; G.dragOffY = py;
                G.selResizeW = iw; G.selResizeH = ih;
                renderCanvas(); updateStatus(); updatePropPanel();
                return TRUE;
            }
            // Image body
            if (px >= ix && px <= ix+iw && py >= iy && py <= iy+ih) {
                G.selImg = i;
                G.dragging = 1;
                G.dragOffX = px-ix; G.dragOffY = py-iy;
                renderCanvas(); updateStatus(); updatePropPanel();
                return TRUE;
            }
        }

        // Clicked on empty space - check if background exists
        if (pg->bgSurf && cairo_surface_status(pg->bgSurf) == CAIRO_STATUS_SUCCESS) {
            const PdfImportPlan::FitRect fit = fitted_background_rect(pg, pg->pw, pg->ph);
            const double dispW = fit.width;
            const double dispH = fit.height;
            const double bx = fit.offsetX;
            const double by = fit.offsetY;

            if (fit.valid && px >= bx && px <= bx+dispW && py >= by && py <= by+dispH) {
                G.selBg = 1;
                // Check resize handle (bottom-right corner, 20px)
                if (px >= bx+dispW-20/G.zoom && px <= bx+dispW && py >= by+dispH-20/G.zoom && py <= by+dispH) {
                    G.resizing = 1;
                    G.dragOffX = px; G.dragOffY = py;
                    G.selResizeW = dispW; G.selResizeH = dispH;
                    G.selResizeOrigW = pg->bgW > 0.0 ? pg->bgW : dispW;
                    G.selResizeOrigH = pg->bgH > 0.0 ? pg->bgH : dispH;
                }
                renderCanvas(); updateStatus(); updatePropPanel();
                return TRUE;
            }
        }

        renderCanvas(); updateStatus(); updatePropPanel();
        return TRUE;
    }

    // Text tool
    if (G.tool == 3) {
        showTextEntry(ev->x, ev->y);
        return TRUE;
    }

    // Drawing - create new stroke on button press, add points on motion
    G.drawing = 1;
    G.lastX = px;
    G.lastY = py;

    // Create a new stroke on initial press (only if we're not already drawing)
    beginMouseUndo();
    pg->strokes.push_back(StrokeData());
    StrokeData* s = &pg->strokes.back();
    s->w = G.penW;
    s->r = G.penR; s->g = G.penG; s->b = G.penB;
    s->a = (G.tool == 1) ? 0.35 : 1.0;
    s->tool = G.tool;
    s->addPt(px, py); // Start point

    NoteData* n = curNote(); if(n) n->dirty = 1;
    updateStatus();
    return TRUE;
}

static gboolean on_btnrelease(GtkWidget*, GdkEventButton*, gpointer) {
    if (G.pendingMultiToggle) {
        PageData* pg = curPage();
        if (pg) {
            toggleHitFromMultiSelection(pg, G.multiPressX, G.multiPressY);
        }
        G.pendingMultiToggle = 0;
        G.dragging = 0;
        G.groupDragging = 0;
        G.mouseUndoActive = 0;
        renderCanvas(); updateStatus(); updatePropPanel();
        return TRUE;
    }

    // Finalize rubber band selection
    if (G.selBoxActive) {
        PageData* pg = curPage();
        if (pg) {
            // Normalize box coordinates
            double bx = G.selBoxX, by = G.selBoxY;
            double bw = G.selBoxW, bh = G.selBoxH;
            if (bw < 0) { bx += bw; bw = -bw; }
            if (bh < 0) { by += bh; bh = -bh; }

            // Select all texts in box
            for (size_t offset = 0; offset < pg->texts.size(); offset++) {
                if (offset > static_cast<size_t>(INT_MAX)) continue;
                TxtEl* t = &pg->texts[offset];
                if (t->x >= bx && t->x <= bx+bw && t->y >= by && t->y <= by+bh) {
                    G.selTexts.push_back(static_cast<int>(offset));
                }
            }
            // Select all strokes in box (any point inside)
            for (size_t offset = 0; offset < pg->strokes.size(); offset++) {
                if (offset > static_cast<size_t>(INT_MAX)) continue;
                StrokeData* s = &pg->strokes[offset];
                for (size_t j = 0; j < s->x.size(); j++) {
                    if (s->x[j] >= bx && s->x[j] <= bx+bw && s->y[j] >= by && s->y[j] <= by+bh) {
                        G.selStrokes.push_back(static_cast<int>(offset));
                        break;
                    }
                }
            }
            // Select all images in box (any part inside)
            for (size_t offset = 0; offset < pg->images.size(); offset++) {
                if (offset > static_cast<size_t>(INT_MAX)) continue;
                ImgEl* img = &pg->images[offset];
                double ix1 = img->x, iy1 = img->y;
                double ix2 = img->x + img->w, iy2 = img->y + img->h;
                // Check if any corner is inside the box, or if box overlaps image
                bool overlap = !(ix2 < bx || ix1 > bx+bw || iy2 < by || iy1 > by+bh);
                if (overlap) {
                    G.selImages.push_back(static_cast<int>(offset));
                }
            }
        }
        G.selBoxActive = 0;
        G.selBoxW = 0; G.selBoxH = 0;
        renderCanvas(); updateStatus();
        return TRUE;
    }

    G.drawing = 0;
    G.dragging = 0;
    G.resizing = 0;
    G.groupDragging = 0;
    G.mouseUndoActive = 0;
    return TRUE;
}

static gboolean on_motion(GtkWidget*, GdkEventMotion* ev, gpointer) {
    PageData* pg = curPage(); if (!pg) return TRUE;

    // Account for scroll offsets
    double px = (ev->x - G.margins[0] - G.pageScrollX) / G.zoom;
    double py = (ev->y - G.margins[1] - G.pageScrollY) / G.zoom;

    if (G.selBoxActive) {
        // Update selection box
        G.selBoxW = px - G.selBoxX;
        G.selBoxH = py - G.selBoxY;
        renderCanvas();  // Will draw selection box overlay
        return TRUE;
    }

    if (G.pendingMultiToggle) {
        const double dx = px - G.multiPressX;
        const double dy = py - G.multiPressY;
        if (sqrt(dx * dx + dy * dy) > 4.0 / G.zoom) {
            beginMouseUndo();
            G.pendingMultiToggle = 0;
            G.groupDragging = 1;
            G.dragging = 1;
        } else {
            return TRUE;
        }
    }

    if (G.dragging && G.groupDragging) {
        beginMouseUndo();
        double dx = px - G.dragOffX, dy = py - G.dragOffY;
        for (int i : G.selTexts) {
            TxtEl* text = itemAt(pg->texts, i);
            if (!text) continue;
            text->x += dx;
            text->y += dy;
        }
        for (int i : G.selStrokes) {
            StrokeData* stroke = itemAt(pg->strokes, i);
            if (!stroke) continue;
            for (size_t j = 0; j < stroke->x.size(); j++) {
                stroke->x[j] += dx;
                stroke->y[j] += dy;
            }
        }
        for (int i : G.selImages) {
            ImgEl* image = itemAt(pg->images, i);
            if (!image) continue;
            image->x += dx;
            image->y += dy;
        }
        G.dragOffX = px; G.dragOffY = py;
        NoteData* n = curNote(); if(n) n->dirty = 1;
        renderCanvas();
        return TRUE;
    }

    if (G.dragging) {
        if (StrokeData* s = itemAt(pg->strokes, G.selStroke)) {
            beginMouseUndo();
            double dx = px - G.dragOffX, dy = py - G.dragOffY;
            for (size_t j = 0; j < s->x.size(); j++) { s->x[j] += dx; s->y[j] += dy; }
            G.dragOffX = px; G.dragOffY = py;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            renderCanvas();
            return TRUE;
        }
        if (ImgEl* img = itemAt(pg->images, G.selImg)) {
            beginMouseUndo();
            img->x = px - G.dragOffX;
            img->y = py - G.dragOffY;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            renderCanvas();
            return TRUE;
        }
        if (TxtEl* text = itemAt(pg->texts, G.selTxt)) {
            beginMouseUndo();
            text->x = px + G.dragOffX;
            text->y = py + G.dragOffY;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            renderCanvas();
            return TRUE;
        }
        return TRUE;
    }

    if (G.resizing && G.selBg && pg->bgSurf && G.selResizeOrigW > 0) {
        beginMouseUndo();
        // Resize background - scale based on horizontal drag
        double dx = px - G.dragOffX;
        double scale = 1.0 + dx / fmax(1, G.selResizeW);
        pg->bgW = G.selResizeOrigW * scale;
        pg->bgH = G.selResizeOrigH * scale;
        if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
        renderCanvas();
        return TRUE;
    }

    if (G.resizing) {
        ImgEl* img = itemAt(pg->images, G.selImg);
        if (!img) return TRUE;
        beginMouseUndo();
        double aspect = G.selResizeW / fmax(1, G.selResizeH);
        double newW = fmax(20, G.selResizeW + (px - G.dragOffX));
        img->w = newW;
        img->h = newW / aspect;
        NoteData* n = curNote(); if(n) n->dirty = 1;
        renderCanvas();
        return TRUE;
    }

    if (G.drawing) {
        // Add point to the current stroke (the last one in the vector)
        if (!pg->strokes.empty()) {
            beginMouseUndo();
            StrokeData* s = &pg->strokes.back();
            s->addPt(px, py);
            G.lastX = px; G.lastY = py;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            renderCanvas();
        }
    }
    return TRUE;
}

static gboolean on_scroll(GtkWidget*, GdkEventScroll* ev, gpointer) {
    PageData* pg = curPage(); if (!pg) return TRUE;

    // Ctrl+scroll = zoom
    bool ctrlHeld = (ev->state & GDK_CONTROL_MASK) != 0;
    if (ctrlHeld) {
        double f = 1.0;
        if (ev->direction == GDK_SCROLL_UP || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y < 0)) f = 1.1;
        else if (ev->direction == GDK_SCROLL_DOWN || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y > 0)) f = 0.9;
        apply_zoom_factor(f, ev->x, ev->y);
        invalidate_canvas_and_refresh();
        return TRUE;
    }

    const double scrollAmt = 40.0;
    bool changed = false;

    // Shift+scroll = horizontal pan
    bool shiftHeld = (ev->state & GDK_SHIFT_MASK) != 0;
    if (shiftHeld) {
        if (ev->direction == GDK_SCROLL_LEFT || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_x < 0)) {
            G.pageScrollX = fmin(0.0, G.pageScrollX + scrollAmt);
            changed = true;
        } else if (ev->direction == GDK_SCROLL_RIGHT || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_x > 0)) {
            G.pageScrollX -= scrollAmt;
            changed = true;
        } else if (ev->direction == GDK_SCROLL_UP || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y < 0)) {
            G.pageScrollX = fmin(0.0, G.pageScrollX + scrollAmt);
            changed = true;
        } else if (ev->direction == GDK_SCROLL_DOWN || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y > 0)) {
            G.pageScrollX -= scrollAmt;
            changed = true;
        }
        clamp_page_scroll(pg);
        if (changed) {
            invalidate_canvas_and_refresh(false);
        }
        return TRUE;
    }

    // Normal scroll = pan. Zoom should only happen with Ctrl.
    if (ev->direction == GDK_SCROLL_UP || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y < 0)) {
        G.pageScrollY = fmin(0.0, G.pageScrollY + scrollAmt);
        changed = true;
    } else if (ev->direction == GDK_SCROLL_DOWN || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y > 0)) {
        G.pageScrollY -= scrollAmt;
        changed = true;
    } else if (ev->direction == GDK_SCROLL_LEFT || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_x < 0)) {
        G.pageScrollX = fmin(0.0, G.pageScrollX + scrollAmt);
        changed = true;
    } else if (ev->direction == GDK_SCROLL_RIGHT || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_x > 0)) {
        G.pageScrollX -= scrollAmt;
        changed = true;
    }

    clamp_page_scroll(pg);
    if (changed) {
        invalidate_canvas_and_refresh(false);
    }
    return TRUE;
}

static gboolean on_keypress(GtkWidget*, GdkEventKey* ev, gpointer) {
    PageData* pg = curPage(); if (!pg) return FALSE;

    // Ctrl+V = Paste from clipboard (GTK3)
    if ((ev->keyval == GDK_KEY_v || ev->keyval == GDK_KEY_V) && (ev->state & GDK_CONTROL_MASK)) {
        GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        if (!clipboard) return FALSE;

        // Try multiple clipboard content types for better compatibility
        // 1. Try image first (for screenshots, copied images)
        GdkPixbuf* pb = gtk_clipboard_wait_for_image(clipboard);

        // 2. If no image, try GTK selection (for some Windows clipboard formats)
        if (!pb) {
            GtkSelectionData* sel = gtk_clipboard_wait_for_contents(clipboard,
                gdk_atom_intern("image/png", FALSE));
            if (sel) {
                const guchar* data = gtk_selection_data_get_data(sel);
                gint len = gtk_selection_data_get_length(sel);
                if (data && len > 0) {
                    GError* err = nullptr;
                    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
                    if (gdk_pixbuf_loader_write(loader, data, static_cast<gsize>(len), &err)) {
                        gdk_pixbuf_loader_close(loader, nullptr);
                        pb = gdk_pixbuf_loader_get_pixbuf(loader);
                        if (pb) g_object_ref(pb);
                    } else {
                        if (err) { logInfo("貼上 PNG 失敗: %s", err->message); g_error_free(err); }
                        g_object_unref(loader);
                    }
                }
                gtk_selection_data_free(sel);
            }
        }

        // 3. Try URI list (for file copies)
        if (!pb) {
            gchar** uris = gtk_clipboard_wait_for_uris(clipboard);
            if (uris && uris[0]) {
                // Load first URI as image
                GError* err = nullptr;
                gchar* filename = g_filename_from_uri(uris[0], nullptr, &err);
                if (filename) {
                    pb = gdk_pixbuf_new_from_file(filename, &err);
                    if (!pb && err) { logInfo("貼上檔案失敗: %s", err->message); g_error_free(err); }
                    g_free(filename);
                } else if (err) {
                    g_error_free(err);
                }
            }
        }

        if (pb) {
            int tw = gdk_pixbuf_get_width(pb);
            int th = gdk_pixbuf_get_height(pb);

            // Scale down if too large
            double sc = 1.0;
            if (tw > 600 || th > 600) sc = fmin(600.0/tw, 600.0/th);

            // Create cairo surface from pixbuf
            cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
            cairo_t* cr = cairo_create(surf);
            gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
            cairo_paint(cr);
            cairo_destroy(cr);
            g_object_unref(pb);

            pg->images.push_back(ImgEl());
            ImgEl* ie = &pg->images.back();
            ie->surf = surf;
            ie->x = 50; ie->y = 50;
            ie->w = tw * sc; ie->h = th * sc;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            renderCanvas(); updateStatus();
            logInfo("已從剪貼簿貼上圖片 (%dx%d)", tw, th);
            return TRUE;
        }

        // 4. Try text
        char* txt = gtk_clipboard_wait_for_text(clipboard);
        if (txt && strlen(txt) > 0) {
            pg->texts.push_back(TxtEl());
            TxtEl* t = &pg->texts.back();
            t->text = txt;
            t->x = 50; t->y = 100;
            t->fontSize = 14;
            t->r = G.penR; t->g = G.penG; t->b = G.penB;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            renderCanvas(); updateStatus();
            logInfo("已從剪貼簿貼上文字");
            g_free(txt);
            return TRUE;
        }

        logInfo("剪貼簿沒有可貼上的內容");
        return FALSE;
    }

    // Ctrl+Z = Undo
    if ((ev->keyval == GDK_KEY_z || ev->keyval == GDK_KEY_Z) && (ev->state & GDK_CONTROL_MASK)) {
        on_undo(nullptr, nullptr);
        return TRUE;
    }
    // Ctrl+Y = Redo
    if ((ev->keyval == GDK_KEY_y || ev->keyval == GDK_KEY_Y) && (ev->state & GDK_CONTROL_MASK)) {
        on_redo(nullptr, nullptr);
        return TRUE;
    }

    // Ctrl+S = Save
    if ((ev->keyval == GDK_KEY_s || ev->keyval == GDK_KEY_S) && (ev->state & GDK_CONTROL_MASK)) {
        on_save(nullptr, nullptr);
        return TRUE;
    }

    // R = Rotate selected image 90 degrees clockwise
    if (ev->keyval == GDK_KEY_r || ev->keyval == GDK_KEY_R) {
        PageData* pg = curPage();
        if (pg) {
            ImgEl* img = itemAt(pg->images, G.selImg);
            if (!img) return FALSE;
            pushUndo();
            img->rotateAngle = (img->rotateAngle + 90) % 360;
            // Swap w/h on 90/270 degree rotation
            if (img->rotateAngle == 90 || img->rotateAngle == 270) {
                double tmp = img->w; img->w = img->h; img->h = tmp;
            }
            NoteData* n = curNote(); if(n) n->dirty = 1;
            if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
            renderCanvas(); updateStatus(); rebuildThumbs();
            return TRUE;
        }
    }

    // C = Crop selected image (simple: halve width and height)
    if (ev->keyval == GDK_KEY_c || ev->keyval == GDK_KEY_C) {
        PageData* pg = curPage();
        if (pg) {
            ImgEl* img = itemAt(pg->images, G.selImg);
            if (!img) return FALSE;
            pushUndo();
            // Simple crop: keep center half
            double newW = img->w / 2.0;
            double newH = img->h / 2.0;
            img->x += (img->w - newW) / 2.0;
            img->y += (img->h - newH) / 2.0;
            img->w = newW;
            img->h = newH;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
            renderCanvas(); updateStatus(); rebuildThumbs();
            return TRUE;
        }
    }

    // Delete — handles multi-select and single-select
    if (ev->keyval == GDK_KEY_Delete || ev->keyval == GDK_KEY_BackSpace) {
        bool deletedAny = false;
        // 1. Multi-select batch delete
        if (hasMultiSelection()) {
            pushUndo();
            deletedAny = eraseSelectedItems(pg);
        }
        // 2. Single-item delete
        if (!deletedAny && G.selImg >= 0 && G.selImg < (int)pg->images.size()) {
            pushUndo();
            pg->images.erase(pg->images.begin()+G.selImg);
            G.selImg=-1; deletedAny = true;
        }
        if (!deletedAny && G.selTxt >= 0 && G.selTxt < (int)pg->texts.size()) {
            pushUndo();
            pg->texts.erase(pg->texts.begin()+G.selTxt);
            G.selTxt=-1; deletedAny = true;
        }
        if (!deletedAny && G.selStroke >= 0 && G.selStroke < (int)pg->strokes.size()) {
            pushUndo();
            pg->strokes.erase(pg->strokes.begin()+G.selStroke);
            G.selStroke=-1; deletedAny = true;
        }
        if (deletedAny) {
            NoteData* n=curNote(); if(n)n->dirty=1;
            renderCanvas(); updateStatus();
            return TRUE;
        }
    }
    return FALSE;
}

// ============================================================
// Toolbar callbacks
// ============================================================
static void on_tool_pen(GtkButton*, gpointer) { hideTextEntry(); G.tool=0; updateStatus(); }
static void on_tool_hl(GtkButton*, gpointer) { hideTextEntry(); G.tool=1; updateStatus(); }
static void on_tool_eraser(GtkButton*, gpointer) { hideTextEntry(); G.tool=2; updateStatus(); }
static void on_tool_text(GtkButton*, gpointer) { G.tool=3; updateStatus(); }
static void on_tool_select(GtkButton*, gpointer) {
    hideTextEntry();
    G.tool=4;
    G.selImg=-1; G.selTxt=-1; G.selStroke=-1; G.selBg=0;
    G.selTexts.clear(); G.selStrokes.clear(); G.selImages.clear();
    G.pendingMultiToggle=0; G.groupDragging=0; G.dragging=0; G.resizing=0;
    updateStatus(); updatePropPanel();
}

static void on_color(GtkButton*, gpointer) {
    GtkWidget* dlg = gtk_color_chooser_dialog_new("\xe9\x81\xb8\xe6\x93\x87\xe9\xa1\x8f\xe8\x89\xb2", GTK_WINDOW(G.window));
    GdkRGBA cur = {G.penR, G.penG, G.penB, 1.0};
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dlg), &cur);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        GdkRGBA c;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dlg), &c);
        G.penR = c.red; G.penG = c.green; G.penB = c.blue;
    }
    gtk_widget_destroy(dlg);
}
static void on_pensize(GtkRange* r, gpointer) { G.penW = gtk_range_get_value(r); }

static void on_zoomin(GtkButton*, gpointer) {
    apply_zoom_factor(1.25);
    invalidate_canvas_and_refresh();
}
static void on_zoomout(GtkButton*, gpointer) {
    apply_zoom_factor(1.0 / 1.25);
    invalidate_canvas_and_refresh();
}
static void on_zoomfit(GtkButton*, gpointer) {
    PageData* pg=curPage(); if(!pg||!G.drawingArea) return;
    int aw=gtk_widget_get_allocated_width(G.drawingArea);
    int ah=gtk_widget_get_allocated_height(G.drawingArea);
    const PdfImportPlan::FitRect fit = PdfImportPlan::fitContain(
        aw - G.margins[0] - G.margins[2] - 10.0,
        ah - G.margins[1] - G.margins[3] - 10.0,
        pg->pw,
        pg->ph);
    if (!fit.valid) return;
    G.zoom=fmax(0.1,fmin(5.0,fit.scale));
    G.pageScrollX = 0;
    G.pageScrollY = 0;
    clamp_page_scroll(pg);
    invalidate_canvas_and_refresh();
}
static void on_undo(GtkButton*, gpointer) {
    NoteData* n = curNote();
    if (!n || n->pages.empty()) return;
    if (G.undoStack.empty()) return;

    // Save current state for redo
    std::string redoData = serializeCurrentNote();
    G.redoStack.push_back(redoData);

    // Restore from undo stack
    std::string prevState = G.undoStack.back();
    G.undoStack.pop_back();

    deserializeNoteToCurrent(prevState);
    n->dirty = 1;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas(); updateStatus(); rebuildThumbs();
}

static void on_redo(GtkButton*, gpointer) {
    NoteData* n = curNote();
    if (!n || n->pages.empty()) return;
    if (G.redoStack.empty()) return;

    // Save current state for undo
    std::string undoData = serializeCurrentNote();
    G.undoStack.push_back(undoData);

    // Restore from redo stack
    std::string prevState = G.redoStack.back();
    G.redoStack.pop_back();

    deserializeNoteToCurrent(prevState);
    n->dirty = 1;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas(); updateStatus(); rebuildThumbs();
}
static void on_del(GtkButton*, gpointer) {
    PageData* pg=curPage(); if(!pg) return;

    bool deletedAny = false;

    if (hasMultiSelection()) {
        pushUndo();
        deletedAny = eraseSelectedItems(pg);
    }
    else if (G.selImg>=0 && G.selImg<(int)pg->images.size()){
        pushUndo();
        pg->images.erase(pg->images.begin()+G.selImg);G.selImg=-1;
        deletedAny = true;
    }
    else if(G.selTxt>=0 && G.selTxt<(int)pg->texts.size()){
        pushUndo();
        pg->texts.erase(pg->texts.begin()+G.selTxt);G.selTxt=-1;
        deletedAny = true;
    }
    else if(G.selStroke>=0 && G.selStroke<(int)pg->strokes.size()){
        pushUndo();
        pg->strokes.erase(pg->strokes.begin()+G.selStroke);G.selStroke=-1;
        deletedAny = true;
    }

    if (!deletedAny) return;

    NoteData* n=curNote();if(n)n->dirty=1;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas(); updateStatus(); rebuildThumbs();
}

// ============================================================
// Note management
// ============================================================
static void on_row_rename_clicked(GtkButton*, gpointer user_data) {
    int idx = GPOINTER_TO_INT(user_data);
    if (idx < 0 || idx >= (int)G.notes.size()) return;
    on_note_rename(nullptr, user_data);
}

static void on_row_delete_clicked(GtkButton*, gpointer user_data) {
    int idx = GPOINTER_TO_INT(user_data);
    if (idx < 0 || idx >= (int)G.notes.size()) return;
    on_note_delete(nullptr, user_data);
}

static void rebuildNoteList() {
    if (!G.noteList) return;
    gtk_container_foreach(GTK_CONTAINER(G.noteList), destroyChild, nullptr);
    for (size_t i = 0; i < G.notes.size(); i++) {
        if (i > static_cast<size_t>(INT_MAX)) continue;
        const int rowIndex = static_cast<int>(i);
        // 搜尋過濾
        if (!G.searchTerm.empty()) {
            std::string nameLower = G.notes[i].name;
            std::string searchLower = G.searchTerm;
            // 轉小寫比較
            for (auto& c : nameLower) c = lowerByte(c);
            for (auto& c : searchLower) c = lowerByte(c);
            bool match = nameLower.find(searchLower) != std::string::npos;
            // 也搜尋內容
            if (!match) {
                for (auto& pg : G.notes[i].pages) {
                    for (auto& t : pg.texts) {
                        std::string txtLower = t.text;
                        for (auto& c : txtLower) c = lowerByte(c);
                        if (txtLower.find(searchLower) != std::string::npos) { match = true; break; }
                    }
                    if (match) break;
                }
            }
            if (!match) continue;  // 跳過不符合的筆記
        }

        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);

        // Name label (expandable)
        GtkWidget* lbl = gtk_label_new(G.notes[i].name.c_str());
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_label_set_max_width_chars(GTK_LABEL(lbl), 10);
        gtk_box_pack_start(GTK_BOX(hbox), lbl, TRUE, TRUE, 0);

        // Rename button
        GtkWidget* renBtn = gtk_button_new_with_label("\xe2\x9c\x8e");
        gtk_widget_set_tooltip_text(renBtn, "\xe9\x87\x8d\xe6\x96\xb0\xe5\x91\xbd\xe5\x90\x8d");
        gtk_widget_set_size_request(renBtn, 24, 24);
        g_signal_connect(renBtn, "clicked", G_CALLBACK(on_row_rename_clicked), GINT_TO_POINTER(rowIndex));
        gtk_box_pack_start(GTK_BOX(hbox), renBtn, FALSE, FALSE, 0);

        // Delete button
        GtkWidget* delBtn = gtk_button_new_with_label("\xe2\x9c\x96");
        gtk_widget_set_tooltip_text(delBtn, "\xe5\x88\xaa\xe9\x99\xa4");
        gtk_widget_set_size_request(delBtn, 24, 24);
        GtkStyleContext* dsc = gtk_widget_get_style_context(delBtn);
        gtk_style_context_add_class(dsc, "destructive-action");
        g_signal_connect(delBtn, "clicked", G_CALLBACK(on_row_delete_clicked), GINT_TO_POINTER(rowIndex));
        gtk_box_pack_start(GTK_BOX(hbox), delBtn, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(row), hbox);
        gtk_widget_show_all(row);
        if (rowIndex == G.selNote) {
            GtkStyleContext* rsc = gtk_widget_get_style_context(row);
            gtk_style_context_add_class(rsc, "note-selected");
        }
        g_object_set_data(G_OBJECT(row), "idx", GINT_TO_POINTER(rowIndex));
        gtk_list_box_insert(GTK_LIST_BOX(G.noteList), row, -1);
    }
}

// ── Page Thumbnails ──
static void on_thumb_activated(GtkListBox*, GtkListBoxRow* row, gpointer) {
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "idx"));
    if (idx >= 0 && curNote()) {
        G.selPage = idx;
        if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
        renderCanvas(); updateStatus(); rebuildThumbs();
    }
}

// ── Search ──
static void on_search_changed(GtkSearchEntry*, gpointer) {
    const char* text = gtk_entry_get_text(GTK_ENTRY(G.searchEntry));
    G.searchTerm = text ? text : "";
    rebuildNoteList();
}

static cairo_surface_t* buildThumbSurface(PageData* pg, int thumbW) {
    if (!pg) return nullptr;
    double scale = (double)thumbW / pg->pw;
    int thumbH = (int)(pg->ph * scale);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, thumbW, thumbH);
    cairo_t* cr = cairo_create(surf);

    // 白色背景
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, 0, 0, thumbW, thumbH);
    cairo_fill(cr);

    cairo_scale(cr, scale, scale);

    // 渲染背景
    if (pg->bgSurf) {
        cairo_save(cr);
        cairo_set_source_surface(cr, pg->bgSurf, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    // 簡化渲染：只渲染筆劃
    for (auto& s : pg->strokes) {
        if (s.x.size() < 2) continue;
        cairo_save(cr);
        cairo_set_source_rgba(cr, s.r, s.g, s.b, s.a);
        cairo_set_line_width(cr, s.w);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_move_to(cr, s.x[0], s.y[0]);
        for (size_t i = 1; i < s.x.size(); i++)
            cairo_line_to(cr, s.x[i], s.y[i]);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    // 邊框
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_set_line_width(cr, 1.0 / scale);
    cairo_rectangle(cr, 0, 0, pg->pw, pg->ph);
    cairo_stroke(cr);

    cairo_destroy(cr);
    return surf;
}

static void rebuildThumbs() {
    if (!G.pageThumbs) return;
    gtk_container_foreach(GTK_CONTAINER(G.pageThumbs), destroyChild, nullptr);

    // 清理舊縮圖 surface
    for (auto* s : G.pageThumbSurf) { if (s) cairo_surface_destroy(s); }
    G.pageThumbSurf.clear();

    NoteData* n = curNote();
    if (!n) return;

    int thumbW = 130;
    for (size_t i = 0; i < n->pages.size(); i++) {
        if (i > static_cast<size_t>(INT_MAX)) continue;
        const int pageIndex = static_cast<int>(i);
        PageData* pg = &n->pages[i];
        cairo_surface_t* thumbSurf = buildThumbSurface(pg, thumbW);
        G.pageThumbSurf.push_back(thumbSurf);

        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_container_set_border_width(GTK_CONTAINER(box), 4);

        if (thumbSurf) {
            int w = cairo_image_surface_get_width(thumbSurf);
            int h = cairo_image_surface_get_height(thumbSurf);
            GtkWidget* da = gtk_drawing_area_new();
            gtk_widget_set_size_request(da, w, h);

            g_object_set_data(G_OBJECT(da), "thumb-surf", thumbSurf);
            g_signal_connect(da, "draw", G_CALLBACK(+[](GtkWidget* widget, cairo_t* cr, gpointer) -> gboolean {
                cairo_surface_t* surf = (cairo_surface_t*)g_object_get_data(G_OBJECT(widget), "thumb-surf");
                if (surf) {
                    cairo_set_source_surface(cr, surf, 0, 0);
                    cairo_paint(cr);
                }
                return TRUE;
            }), nullptr);

            gtk_box_pack_start(GTK_BOX(box), da, FALSE, FALSE, 0);
        }

        GtkWidget* lbl = gtk_label_new("");
        char pageLbl[32];
        snprintf(pageLbl, sizeof(pageLbl), "P%zu%s", i + 1,
            pageIndex == G.selPage ? " ◀" : "");
        gtk_label_set_text(GTK_LABEL(lbl), pageLbl);
        gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_widget_show_all(row);

        if (pageIndex == G.selPage) {
            GtkStyleContext* rsc = gtk_widget_get_style_context(row);
            gtk_style_context_add_class(rsc, GTK_STYLE_CLASS_SUGGESTED_ACTION);
        }
        g_object_set_data(G_OBJECT(row), "idx", GINT_TO_POINTER(pageIndex));
        gtk_list_box_insert(GTK_LIST_BOX(G.pageThumbs), row, -1);
    }
}

static void on_note_activated(GtkListBox*, GtkListBoxRow* row, gpointer) {
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "idx"));
    if (!itemAt(G.notes, idx)) return;
    hideTextEntry();
    if (NoteData* selected = itemAt(G.notes, G.selNote); selected && selected->dirty) {
        logInfo("Auto-save: %s", selected->name.c_str());
        selected->dirty = 0;
    }
    G.selNote = idx; G.selPage = 0;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    G.selImg = -1; G.selTxt = -1;
    rebuildNoteList(); renderCanvas(); updateStatus();
}

// Note context menu
static void on_note_rename(GtkMenuItem*, gpointer user_data) {
    int idx = GPOINTER_TO_INT(user_data);
    NoteData* nd = itemAt(G.notes, idx);
    if (!nd) return;

    GtkWidget* dlg = gtk_dialog_new_with_buttons("\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d\xe7\xad\x86\xe8\xa8\x98",
        GTK_WINDOW(G.window), (GtkDialogFlags)(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
        "\xe7\xa2\xba\xe5\xae\x9a", GTK_RESPONSE_ACCEPT, "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), nd->name.c_str());
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(content);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        const char* newname = gtk_entry_get_text(GTK_ENTRY(entry));
        if (newname && strlen(newname) > 0) {
            nd->name = newname;
            nd->dirty = 1;
            rebuildNoteList(); updateStatus();
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_note_delete(GtkMenuItem*, gpointer user_data) {
    int idx = GPOINTER_TO_INT(user_data);
    NoteData* nd = itemAt(G.notes, idx);
    if (!nd) return;
    if (G.notes.size() <= 1) {
        GtkWidget* info = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "至少要保留一筆筆記");
        gtk_dialog_run(GTK_DIALOG(info)); gtk_widget_destroy(info);
        return;
    }

    // Confirmation dialog
    GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "確定刪除筆記「%s」嗎?\n此操作無法復原",
        nd->name.c_str());
    gtk_dialog_add_buttons(GTK_DIALOG(dlg),
        "取消", GTK_RESPONSE_CANCEL,
        "刪除", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_CANCEL);

    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (resp == GTK_RESPONSE_ACCEPT) {
        // Delete the .onote file from disk as well
        std::string fn = note_filename(nd);
        std::error_code ec;
        fs::remove(fn, ec);
        if (ec) {
            logInfo("刪除檔案失敗: %s: %s", fn.c_str(), ec.message().c_str());
        }

        G.notes.erase(G.notes.begin() + static_cast<std::vector<NoteData>::difference_type>(idx));
        if (G.selNote == idx) {
            G.selNote = (idx < (int)G.notes.size()) ? idx : (int)G.notes.size() - 1;
            G.selPage = 0;
        } else if (G.selNote > idx) {
            G.selNote--;
        }
        if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
        rebuildNoteList(); renderCanvas(); updateStatus(); rebuildThumbs();
        logInfo("已刪除筆記");
    }
}

// Note list right-click context menu (using button-press-event for reliability)
static gboolean on_note_list_button_press(GtkWidget*, GdkEventButton* ev, gpointer) {
    if (ev->button != 3) return FALSE; // Only right-click

    // Get row at position
    GtkListBoxRow* row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(G.noteList), (int)ev->y);
    if (!row) return FALSE;

    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "idx"));
    if (!itemAt(G.notes, idx)) return FALSE;

    // Build context menu
    GtkWidget* menu = gtk_menu_new();

    // Rename item
    GtkWidget* renameItem = gtk_menu_item_new_with_label("重新命名");
    g_signal_connect(renameItem, "activate", G_CALLBACK(on_note_rename), GINT_TO_POINTER(idx));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), renameItem);

    // Delete item
    GtkWidget* delItem = gtk_menu_item_new_with_label("刪除");
    g_signal_connect(delItem, "activate", G_CALLBACK(on_note_delete), GINT_TO_POINTER(idx));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), delItem);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)ev);
    return TRUE;
}

static void on_newnote(GtkButton*, gpointer) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons("\xe6\x96\xb0\xe5\xbb\xba\xe7\xad\x86\xe8\xa8\x98",
        GTK_WINDOW(G.window), (GtkDialogFlags)(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
        "\xe5\xbb\xba\xe7\xab\x8b", GTK_RESPONSE_ACCEPT, "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    GtkWidget* entry = gtk_entry_new();
    char name[64]; snprintf(name,sizeof(name),"\xe7\xad\x86\xe8\xa8\x98 %d",(int)G.notes.size()+1);
    gtk_entry_set_text(GTK_ENTRY(entry), name);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);

    GtkWidget* combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "\xe7\x9b\xb4\xe5\xbc\x8f A4");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "\xe6\xa8\xaa\xe5\xbc\x8f A4");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    GtkWidget* hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hb), gtk_label_new("\xe6\x96\xb9\xe5\x90\x91"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), hb, FALSE, FALSE, 8);
    gtk_widget_show_all(content);

    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if (resp == GTK_RESPONSE_ACCEPT) {
        const char* ntxt = gtk_entry_get_text(GTK_ENTRY(entry));
        int landscape = (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)) == 1);
        G.notes.push_back(NoteData());
        NoteData* nd = &G.notes.back();
        nd->name = ntxt ? ntxt : name;
        nd->pages.push_back(PageData());
        nd->pages.back().pw = landscape ? 842 : 595;
        nd->pages.back().ph = landscape ? 595 : 842;
        G.selNote = (int)G.notes.size() - 1; G.selPage = 0;
        if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
        rebuildNoteList(); renderCanvas(); updateStatus(); rebuildThumbs();
    }
    gtk_widget_destroy(dlg);
}

// ============================================================
// Page operations
// ============================================================
static void on_page_prev(GtkButton*, gpointer) {
    hideTextEntry();
    if (G.selPage > 0) { G.selPage--; if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;} renderCanvas(); updateStatus(); }
}
static void on_page_next(GtkButton*, gpointer) {
    hideTextEntry();
    NoteData* n=curNote();
    if(n && G.selPage < (int)n->pages.size()-1) { G.selPage++; if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;} renderCanvas(); updateStatus(); }
}
static void on_page_add(GtkButton*, gpointer) {
    hideTextEntry();
    NoteData* n=curNote(); if(!n) return;
    PageData* curPg = curPage();
    n->pages.push_back(PageData());
    if (curPg) { n->pages.back().pw = curPg->pw; n->pages.back().ph = curPg->ph; }
    G.selPage = (int)n->pages.size()-1; n->dirty=1;
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    renderCanvas(); updateStatus();
}
static void on_page_clear(GtkButton*, gpointer) {
    hideTextEntry();
    PageData* pg=curPage(); if(!pg) return;
    pg->strokes.clear(); pg->images.clear(); pg->texts.clear();
    if(pg->bgSurf){cairo_surface_destroy(pg->bgSurf);pg->bgSurf=nullptr;pg->bgW=0;pg->bgH=0;pg->bgFile.clear();}
    NoteData* n=curNote(); if(n)n->dirty=1;
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    renderCanvas(); updateStatus();
}

static void on_page_delete(GtkButton*, gpointer) {
    hideTextEntry();
    NoteData* n=curNote(); if(!n || n->pages.size()<=1) return;

    GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "\xe7\xa2\xba\xe5\xae\x9a\xe5\x88\xaa\xe9\x99\xa4\xe7\xac\xac %d \xe9\xa0\x81?", G.selPage+1);
    gtk_dialog_add_buttons(GTK_DIALOG(dlg), "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL, "\xe5\x88\xaa\xe9\x99\xa4", GTK_RESPONSE_ACCEPT, nullptr);
    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (resp == GTK_RESPONSE_ACCEPT) {
        n->pages.erase(n->pages.begin()+G.selPage);
        if(G.selPage>=(int)n->pages.size()) G.selPage=(int)n->pages.size()-1;
        n->dirty=1;
        if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
        renderCanvas(); updateStatus();
    }
}

static void on_page_orientation(GtkButton*, gpointer) {
    hideTextEntry();
    PageData* pg=curPage(); if(!pg) return;
    double tmp = pg->pw; pg->pw = pg->ph; pg->ph = tmp;
    NoteData* n=curNote(); if(n)n->dirty=1;
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    renderCanvas(); updateStatus();
}

// ============================================================
// Save/Load - actual .onote file serialization
// ============================================================
static std::string get_save_dir() {
    // Use exe directory - most reliable for portable apps
    std::string exeDir = get_exe_dir();
    logInfo("get_save_dir: exeDir=[%s]", exeDir.c_str());

    // Use forward slashes for cross-platform compatibility (Windows supports them)
    std::string dir = exeDir;
    for (auto& c : dir) if (c == '\\') c = '/';
    dir += "/data/notes";
    logInfo("get_save_dir: final=[%s]", dir.c_str());

    // Ensure directory exists
    std::error_code ec;
    fs::create_directories(fs::path(dir), ec);
    if (ec) {
        logInfo("get_save_dir: create_directories failed, falling back to home");
        // Fallback to user home
        dir = std::string(g_get_home_dir()) + "/OfflineNote/notes";
        fs::create_directories(fs::path(dir), ec);
    }
    return dir;
}

static std::string note_filename(NoteData* nd) {
    std::string fn = nd->name;
    if (fn.empty()) fn = "unnamed";
    // Sanitize filename - remove dangerous characters
    for (auto& c : fn) {
        if (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    return get_save_dir() + "/" + fn + ".onote";
}

// Helper: Convert wide string to UTF-8
[[maybe_unused]] static std::string wstring_to_utf8(const std::wstring& wstr) {
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string result(static_cast<size_t>(len), 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], len, NULL, NULL);
    return result;
}

// Helper: Convert UTF-8 string to wide string (for Windows file paths)
static std::wstring utf8_to_wide(const std::string& utf8) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(static_cast<size_t>(len), 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], len);
    if (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

static fs::path utf8_to_path(const std::string& utf8) {
    return fs::path(utf8_to_wide(utf8));
}

// Helper: Open file with wide string path, returns FILE* or nullptr
static FILE* wfopen_utf8(const std::string& path_utf8, const wchar_t* mode) {
    std::wstring wpath = utf8_to_wide(path_utf8);
    
    // Debug: ensure directory exists first
    try {
        fs::path parentDir = fs::path(wpath).parent_path();
        std::error_code ec;
        if (!fs::exists(parentDir, ec)) {
            logInfo("wfopen_utf8: creating dir %s", parentDir.u8string().c_str());
            fs::create_directories(parentDir, ec);
            if (ec) {
                logInfo("wfopen_utf8: create_directories failed: %s", ec.message().c_str());
            }
        }
    } catch (...) {
        logInfo("wfopen_utf8: exception checking/creating parent dir");
    }
    
    FILE* f = _wfopen(wpath.c_str(), mode);
    if (!f) {
        logInfo("wfopen_utf8: _wfopen failed for %s (errno=%d)", path_utf8.c_str(), errno);
    }
    return f;
}

// Helper: Get exe directory using std::filesystem (most reliable, no encoding issues)
static std::string get_exe_dir() {
    // Method 1: Try to find offlinenote.exe in current directory
    try {
        fs::path cwd = fs::current_path();
        fs::path exePath = cwd / "offlinenote.exe";
        if (fs::exists(exePath)) {
            return cwd.u8string();
        }
    } catch (...) {}

    // Method 2: Use Windows API with proper null-termination
    try {
        wchar_t wPath[MAX_PATH] = {0};  // Zero-initialize buffer
        DWORD len = GetModuleFileNameW(nullptr, wPath, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            // Create wstring with correct length only (no trailing garbage)
            std::wstring ws(wPath, len);
            size_t pos = ws.find_last_of(L"\\/");
            if (pos != std::wstring::npos && pos > 0) {
                std::wstring wDir = ws.substr(0, pos);
                // Convert using fs::path for safety
                return fs::path(wDir).u8string();
            }
        }
    } catch (...) {}

    // Fallback
    try {
        return fs::current_path().u8string();
    } catch (...) {
        return ".";
    }
}

// ── Undo/Redo: Serialize/Deserialize to string ──
static std::string serializeNote(const NoteData* nd) {
    if (!nd) return "";
    std::string out;
    char buf[4096];
    out += "# OfflineNote undo\n";
    snprintf(buf, sizeof(buf), "name=%s\n", nd->name.c_str()); out += buf;
    snprintf(buf, sizeof(buf), "pages=%zu\n", nd->pages.size()); out += buf;

    for (size_t pi = 0; pi < nd->pages.size(); pi++) {
        const PageData* pg = &nd->pages[pi];
        snprintf(buf, sizeof(buf), "[page %zu]\n", pi); out += buf;
        snprintf(buf, sizeof(buf), "pw=%g\nph=%g\n", pg->pw, pg->ph); out += buf;

        for (size_t si = 0; si < pg->strokes.size(); si++) {
            const StrokeData* s = &pg->strokes[si];
            snprintf(buf, sizeof(buf), "s %zu w=%g r=%g g=%g b=%g a=%g t=%d\n", si, s->w, s->r, s->g, s->b, s->a, s->tool);
            out += buf;
            for (size_t pt = 0; pt < s->x.size(); pt++) {
                snprintf(buf, sizeof(buf), "p %g %g\n", s->x[pt], s->y[pt]); out += buf;
            }
        }

        for (size_t ti = 0; ti < pg->texts.size(); ti++) {
            const TxtEl* t = &pg->texts[ti];
            std::string esc = LegacyNoteTextCodec::encode(t->text);
            snprintf(buf, sizeof(buf), "t %zu x=%g y=%g fs=%g r=%g g=%g b=%g txt=%s\n", ti, t->x, t->y, t->fontSize, t->r, t->g, t->b, esc.c_str());
            out += buf;
        }

        for (size_t ii = 0; ii < pg->images.size(); ii++) {
            const ImgEl* img = &pg->images[ii];
            if (!img->srcFile.empty()) {
                snprintf(buf, sizeof(buf), "img %zu x=%g y=%g w=%g h=%g src=%s\n", ii, img->x, img->y, img->w, img->h, img->srcFile.c_str());
                out += buf;
            }
        }

        if (!pg->bgFile.empty()) {
            snprintf(buf, sizeof(buf), "bg src=%s w=%g h=%g\n", pg->bgFile.c_str(), pg->bgW, pg->bgH);
            out += buf;
        }
    }
    return out;
}

static void deserializeNoteToCurrent(const std::string& data) {
    NoteData* nd = curNote();
    if (!nd || data.empty()) return;

    // Parse the serialized data back into the current note
    nd->pages.clear();
    nd->name = "";

    std::istringstream ss(data);
    std::string line;
    int curPage = -1;
    StrokeData* curStroke = nullptr;

    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#' || line.substr(0, 5) == "name=") {
            if (line.substr(0, 5) == "name=") nd->name = line.substr(5);
            continue;
        }
        if (line.substr(0, 6) == "pages=") continue;

        if (line.substr(0, 6) == "[page ") {
            curPage++;
            nd->pages.push_back(PageData());
            continue;
        }

        if (PageData* pg = itemAt(nd->pages, curPage)) {
            if (line.substr(0, 3) == "pw=") { pg->pw = atof(line.c_str()+3); continue; }
            if (line.substr(0, 3) == "ph=") { pg->ph = atof(line.c_str()+3); continue; }

            if (line.substr(0, 2) == "s ") {
                pg->strokes.push_back(StrokeData());
                curStroke = &pg->strokes.back();
                const char* str = line.c_str();
                const char* tok;
                tok = strstr(str, "w="); if(tok) curStroke->w = atof(tok+2);
                tok = strstr(str, "r="); if(tok) curStroke->r = atof(tok+2);
                tok = strstr(str, "g="); if(tok) curStroke->g = atof(tok+2);
                tok = strstr(str, "b="); if(tok) curStroke->b = atof(tok+2);
                tok = strstr(str, "a="); if(tok) curStroke->a = atof(tok+2);
                tok = strstr(str, "t="); if(tok) curStroke->tool = atoi(tok+2);
                continue;
            }

            if (line[0] == 'p' && line[1] == ' ' && curStroke) {
                double x, y;
                if (sscanf(line.c_str()+2, "%lf %lf", &x, &y) == 2)
                    curStroke->addPt(x, y);
                continue;
            }

            if (line.substr(0, 2) == "t ") {
                pg->texts.push_back(TxtEl());
                TxtEl* t = &pg->texts.back();
                const char* tok;
                tok = strstr(line.c_str(), "x="); if(tok) t->x = atof(tok+2);
                tok = strstr(line.c_str(), "y="); if(tok) t->y = atof(tok+2);
                tok = strstr(line.c_str(), "fs="); if(tok) t->fontSize = atof(tok+3);
                tok = strstr(line.c_str(), "r="); if(tok) t->r = atof(tok+2);
                tok = strstr(line.c_str(), "g="); if(tok) t->g = atof(tok+2);
                tok = strstr(line.c_str(), "b="); if(tok) t->b = atof(tok+2);
                tok = strstr(line.c_str(), "txt="); if(tok) t->text = LegacyNoteTextCodec::decode(tok+4);
                continue;
            }

            if (line.substr(0, 4) == "img ") {
                pg->images.push_back(ImgEl());
                ImgEl* img = &pg->images.back();
                const char* tok;
                tok = strstr(line.c_str(), "x="); if(tok) img->x = atof(tok+2);
                tok = strstr(line.c_str(), "y="); if(tok) img->y = atof(tok+2);
                tok = strstr(line.c_str(), "w="); if(tok) img->w = atof(tok+2);
                tok = strstr(line.c_str(), "h="); if(tok) img->h = atof(tok+2);
                tok = strstr(line.c_str(), "src="); if(tok) img->srcFile = tok+4;
                continue;
            }

            if (line.substr(0, 3) == "bg ") {
                const char* tok = strstr(line.c_str(), "src=");
                if (tok) {
                    pg->bgFile = tok + 4;
                    tok = strstr(line.c_str(), "w="); if(tok) pg->bgW = atof(tok+2);
                    tok = strstr(line.c_str(), "h="); if(tok) pg->bgH = atof(tok+2);
                }
                continue;
            }
        }
    }

    // Reload images/backgrounds from disk
    std::string saveDir = get_save_dir();
    for (auto& pg : nd->pages) {
        for (auto& img : pg.images) {
            std::string fullPath = saveDir + "/" + img.srcFile;
            GError* err = nullptr;
            GdkPixbuf* pb = gdk_pixbuf_new_from_file(fullPath.c_str(), &err);
            if (pb) {
                int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
                cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
                cairo_t* cr = cairo_create(surf);
                gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
                cairo_paint(cr);
                cairo_destroy(cr);
                g_object_unref(pb);
                img.surf = surf;
            } else { if (err) g_error_free(err); }
        }
        if (!pg.bgFile.empty()) {
            std::string fullPath = saveDir + "/" + pg.bgFile;
            GError* err = nullptr;
            GdkPixbuf* pb = gdk_pixbuf_new_from_file(fullPath.c_str(), &err);
            if (pb) {
                int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
                cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
                cairo_t* cr = cairo_create(surf);
                gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
                cairo_paint(cr);
                cairo_destroy(cr);
                g_object_unref(pb);
                pg.bgSurf = surf;
            } else { if (err) g_error_free(err); }
        }
    }
}

static bool save_note_to_file(NoteData* nd) {
    std::string fn_utf8 = note_filename(nd);
    const fs::path notePath = utf8_to_path(fn_utf8);
    FILE* f = wfopen_utf8(fn_utf8, L"wb");
    if (!f) {
        logInfo("儲存失敗: 無法建立 %s", fn_utf8.c_str());
        return false;
    }

    // Simple format: key=value lines, sections for pages
    fprintf(f, "# OfflineNote v1\n");
    fprintf(f, "name=%s\n", nd->name.c_str());
    fprintf(f, "pages=%zu\n", nd->pages.size());

    for (size_t pi = 0; pi < nd->pages.size(); pi++) {
        PageData* pg = &nd->pages[pi];
        fprintf(f, "[page %zu]\n", pi);
        fprintf(f, "pw=%g\nph=%g\n", pg->pw, pg->ph);
        fprintf(f, "strokes=%zu\nimages=%zu\ntexts=%zu\n", pg->strokes.size(), pg->images.size(), pg->texts.size());

        // Save strokes
        for (size_t si = 0; si < pg->strokes.size(); si++) {
            StrokeData* s = &pg->strokes[si];
            fprintf(f, "s %zu w=%g r=%g g=%g b=%g a=%g t=%d\n", si, s->w, s->r, s->g, s->b, s->a, s->tool);
            for (size_t pi2 = 0; pi2 < s->x.size(); pi2++) {
                fprintf(f, "p %g %g\n", s->x[pi2], s->y[pi2]);
            }
        }

        // Save texts
        for (size_t ti = 0; ti < pg->texts.size(); ti++) {
            TxtEl* t = &pg->texts[ti];
            std::string escaped = LegacyNoteTextCodec::encode(t->text);
            fprintf(f, "t %zu x=%g y=%g fs=%g r=%g g=%g b=%g txt=%s\n",
                    ti, t->x, t->y, t->fontSize, t->r, t->g, t->b, escaped.c_str());
        }

        // Save image sources
        for (size_t ii = 0; ii < pg->images.size(); ii++) {
            ImgEl* img = &pg->images[ii];
            if (!img->srcFile.empty()) {
                const std::string storedPath = LegacyNoteResourceHelper::stageForNote(
                    notePath, utf8_to_path(img->srcFile), notePath);
                if (!storedPath.empty()) {
                    img->srcFile = storedPath;
                    fprintf(f, "img %zu x=%g y=%g w=%g h=%g src=%s\n",
                            ii, img->x, img->y, img->w, img->h, storedPath.c_str());
                }
            }
        }

        // Save background source
        if (!pg->bgFile.empty()) {
            const std::string storedPath = LegacyNoteResourceHelper::stageForNote(
                notePath, utf8_to_path(pg->bgFile), notePath);
            if (!storedPath.empty()) {
                pg->bgFile = storedPath;
                fprintf(f, "bg src=%s w=%g h=%g\n", storedPath.c_str(), pg->bgW, pg->bgH);
            }
        }
    }

    fclose(f);
    nd->dirty = 0;
    return true;
}

static bool write_crash_recovery_snapshot(NoteData* nd) {
    if (!nd || G.crashRecoveryFile.empty()) {
        return false;
    }

    FILE* f = wfopen_utf8(G.crashRecoveryFile, L"wb");
    if (!f) {
        return false;
    }

    fprintf(f, "# OfflineNote v1\n");
    fprintf(f, "name=%s\n", nd->name.c_str());
    fprintf(f, "pages=%zu\n", nd->pages.size());
    for (size_t pi = 0; pi < nd->pages.size(); pi++) {
        PageData* pg = &nd->pages[pi];
        fprintf(f, "[page %zu]\n", pi);
        fprintf(f, "pw=%g\nph=%g\n", pg->pw, pg->ph);
        for (size_t si = 0; si < pg->strokes.size(); si++) {
            StrokeData* s = &pg->strokes[si];
            fprintf(f, "s %zu w=%g r=%g g=%g b=%g a=%g t=%d\n", si, s->w, s->r, s->g, s->b, s->a, s->tool);
            for (size_t pt = 0; pt < s->x.size(); pt++) {
                fprintf(f, "p %g %g\n", s->x[pt], s->y[pt]);
            }
        }
        for (size_t ti = 0; ti < pg->texts.size(); ti++) {
            TxtEl* t = &pg->texts[ti];
            std::string escaped = LegacyNoteTextCodec::encode(t->text);
            fprintf(f, "t %zu x=%g y=%g fs=%g r=%g g=%g b=%g txt=%s\n",
                    ti, t->x, t->y, t->fontSize, t->r, t->g, t->b, escaped.c_str());
        }
        for (size_t ii = 0; ii < pg->images.size(); ii++) {
            ImgEl* img = &pg->images[ii];
            if (!img->srcFile.empty()) {
                fprintf(f, "img %zu x=%g y=%g w=%g h=%g src=%s\n",
                        ii, img->x, img->y, img->w, img->h, img->srcFile.c_str());
            }
        }
        if (!pg->bgFile.empty()) {
            fprintf(f, "bg src=%s w=%g h=%g\n", pg->bgFile.c_str(), pg->bgW, pg->bgH);
        }
    }

    fclose(f);
    return true;
}

static bool load_note_from_file(const std::string& fn_utf8, NoteData* nd) {
    const fs::path notePath = utf8_to_path(fn_utf8);
    // Use _wfopen for wide path support (Chinese characters)
    FILE* f = wfopen_utf8(fn_utf8, L"rb");
    if (!f) return false;

    // Use dynamic line buffer to avoid truncation of long lines
    std::string line;
    line.reserve(8192);
    int curPage = -1;
    StrokeData* curStroke = nullptr;

    // Read line by line using fgets with growing buffer
    char chunk[4096];
    while (fgets(chunk, sizeof(chunk), f)) {
        std::string sline(chunk);
        // If line was truncated (no newline and buffer full), read more
        while (!sline.empty() && sline.back() != '\n' && sline.size() >= sizeof(chunk) - 1) {
            // Line was truncated — read continuation
            if (fgets(chunk, sizeof(chunk), f)) {
                sline += chunk;
            } else {
                break;  // EOF
            }
        }
        // Trim trailing newline
        if (!sline.empty() && sline.back() == '\n') sline.pop_back();
        if (sline.empty() || sline[0] == '#') continue;

        if (sline.substr(0, 5) == "name=") { nd->name = sline.substr(5); continue; }
        if (sline.substr(0, 6) == "pages=") continue;

        if (sline.substr(0, 6) == "[page ") {
            curPage++;
            nd->pages.push_back(PageData());
            continue;
        }

        if (PageData* pg = itemAt(nd->pages, curPage)) {

            if (sline.substr(0, 3) == "pw=") { pg->pw = atof(sline.c_str()+3); continue; }
            if (sline.substr(0, 3) == "ph=") { pg->ph = atof(sline.c_str()+3); continue; }

            if (sline.substr(0, 2) == "s ") {
                pg->strokes.push_back(StrokeData());
                curStroke = &pg->strokes.back();
                // Parse params
                const char* str = sline.c_str();
                const char* tok;
                tok = strstr(str, "w="); if(tok) curStroke->w = atof(tok+2);
                tok = strstr(str, "r="); if(tok) curStroke->r = atof(tok+2);
                tok = strstr(str, "g="); if(tok) curStroke->g = atof(tok+2);
                tok = strstr(str, "b="); if(tok) curStroke->b = atof(tok+2);
                tok = strstr(str, "a="); if(tok) curStroke->a = atof(tok+2);
                tok = strstr(str, "t="); if(tok) curStroke->tool = atoi(tok+2);
                continue;
            }

            if (sline[0] == 'p' && sline[1] == ' ' && curStroke) {
                double x, y;
                if (sscanf(sline.c_str()+2, "%lf %lf", &x, &y) == 2) {
                    curStroke->addPt(x, y);
                }
                continue;
            }

            if (sline.substr(0, 2) == "t ") {
                pg->texts.push_back(TxtEl());
                TxtEl* t = &pg->texts.back();
                char* tok;
                tok = strstr((const char*)sline.c_str(), "x="); if(tok) t->x = atof(tok+2);
                tok = strstr((const char*)sline.c_str(), "y="); if(tok) t->y = atof(tok+2);
                tok = strstr((const char*)sline.c_str(), "fs="); if(tok) t->fontSize = atof(tok+3);
                tok = strstr((const char*)sline.c_str(), "r="); if(tok) t->r = atof(tok+2);
                tok = strstr((const char*)sline.c_str(), "g="); if(tok) t->g = atof(tok+2);
                tok = strstr((const char*)sline.c_str(), "b="); if(tok) t->b = atof(tok+2);
                tok = strstr((const char*)sline.c_str(), "txt="); if(tok) t->text = LegacyNoteTextCodec::decode(tok+4);
                continue;
            }

            if (sline.substr(0, 4) == "img ") {
                pg->images.push_back(ImgEl());
                ImgEl* img = &pg->images.back();
                char* tok;
                tok = strstr((const char*)sline.c_str(), "x="); if(tok) img->x = atof(tok+2);
                tok = strstr((const char*)sline.c_str(), "y="); if(tok) img->y = atof(tok+2);
                tok = strstr((const char*)sline.c_str(), "w="); if(tok) img->w = atof(tok+2);
                tok = strstr((const char*)sline.c_str(), "h="); if(tok) img->h = atof(tok+2);
                tok = strstr((const char*)sline.c_str(), "src=");
                if(tok) img->srcFile = LegacyNoteResourceHelper::sanitizeStoredPath(tok+4);

                // Reload image if file exists
                const fs::path fullPath = LegacyNoteResourceHelper::resolveForNote(notePath, img->srcFile);
                if (!img->srcFile.empty() && !fullPath.empty() && fs::exists(fullPath)) {
                    GError* err = nullptr;
                    GdkPixbuf* pb = gdk_pixbuf_new_from_file(fullPath.u8string().c_str(), &err);
                    if (pb) {
                        int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
                        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
                        cairo_t* cr = cairo_create(surf);
                        gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
                        cairo_paint(cr);
                        cairo_destroy(cr);
                        g_object_unref(pb);
                        img->surf = surf;
                    } else {
                        if (err) g_error_free(err);
                    }
                }
                continue;
            }

            if (sline.substr(0, 3) == "bg ") {
                char* tok = strstr((const char*)sline.c_str(), "src=");
                if (tok) {
                    pg->bgFile = LegacyNoteResourceHelper::sanitizeStoredPath(tok + 4);
                    tok = strstr((const char*)sline.c_str(), "w="); if(tok) pg->bgW = atof(tok+2);
                    tok = strstr((const char*)sline.c_str(), "h="); if(tok) pg->bgH = atof(tok+2);

                    // Reload background
                    const fs::path fullPath = LegacyNoteResourceHelper::resolveForNote(notePath, pg->bgFile);
                    if (!pg->bgFile.empty() && !fullPath.empty() && fs::exists(fullPath)) {
                        GError* err = nullptr;
                        GdkPixbuf* pb = gdk_pixbuf_new_from_file(fullPath.u8string().c_str(), &err);
                        if (pb) {
                            int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
                            cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
                            cairo_t* cr = cairo_create(surf);
                            gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
                            cairo_paint(cr);
                            cairo_destroy(cr);
                            g_object_unref(pb);
                            pg->bgSurf = surf;
                        } else {
                            if (err) g_error_free(err);
                        }
                    }
                }
                continue;
            }
        }
    }

    fclose(f);
    return true;
}

static void on_save(GtkButton*, gpointer) {
    NoteData* n = curNote();
    if (!n) {
        GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "沒有選擇筆記，無法儲存");
        gtk_dialog_run(GTK_DIALOG(err)); gtk_widget_destroy(err);
        return;
    }

    // Get save dir and ensure it exists
    std::string saveDir = get_save_dir();
    std::error_code ec;
    if (!fs::exists(fs::path(saveDir), ec)) {
        fs::create_directories(fs::path(saveDir), ec);
    }

    if (ec || !fs::is_directory(fs::path(saveDir), ec)) {
        char errMsg[512];
        snprintf(errMsg, sizeof(errMsg),
            "無法建立儲存目錄!\n\n嘗試路徑: %s\n錯誤: %s\n\n請確認磁碟空間充足。",
            saveDir.c_str(), ec ? ec.message().c_str() : "未知");
        GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", errMsg);
        gtk_dialog_run(GTK_DIALOG(err)); gtk_widget_destroy(err);
        return;
    }

    bool ok = save_note_to_file(n);
    if (ok) {
        logInfo("已儲存: %s", n->name.c_str());
        gtk_label_set_text(GTK_LABEL(G.lblStatus), "已儲存");
    } else {
        std::string fn = note_filename(n);
        // Escape backslashes for GTK message dialog (\ -> \\)
        std::string safeDir, safeFn;
        for (char c : saveDir) { safeDir += c; if (c == '\\') safeDir += '\\'; }
        for (char c : fn) { safeFn += c; if (c == '\\') safeFn += '\\'; }
        char errMsg[1024];
        snprintf(errMsg, sizeof(errMsg),
            "儲存失敗！\n\n儲存目錄: %s\n檔案: %s\n筆記名稱: %s\n\n請確認有寫入權限。",
            safeDir.c_str(), safeFn.c_str(), n->name.c_str());
        GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", errMsg);
        gtk_dialog_run(GTK_DIALOG(err)); gtk_widget_destroy(err);
    }
    rebuildNoteList(); updateStatus();
}

static void on_load_note(const std::string& fn) {
    G.notes.push_back(NoteData());
    NoteData* nd = &G.notes.back();
    if (load_note_from_file(fn, nd)) {
        G.selNote = boundedIntCount(G.notes.size() - 1);
        G.selPage = 0;
        if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
        rebuildNoteList(); renderCanvas(); updateStatus(); rebuildThumbs();
    } else {
        G.notes.pop_back();
        logInfo("Failed to load note: %s", fn.c_str());
    }
}

// Batch import notes from a folder
static void on_batch_import(GtkButton*, gpointer) {
    GtkWidget* dlg = gtk_file_chooser_dialog_new("批次匯入筆記資料夾", GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "取消", GTK_RESPONSE_CANCEL, "匯入", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* dirPath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (dirPath) {
            // Convert path to wide string for directory iteration
            std::wstring wDirPath = utf8_to_wide(std::string(dirPath));
            int imported = 0;
            int failed = 0;
            
            try {
                if (fs::exists(wDirPath)) {
                    for (const auto& entry : fs::directory_iterator(wDirPath)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".onote") {
                            std::string notePath = entry.path().u8string();
                            NoteData* nd = &G.notes.emplace_back();
                            if (load_note_from_file(notePath, nd)) {
                                imported++;
                            } else {
                                G.notes.pop_back();
                                failed++;
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                logInfo("Batch import error: %s", e.what());
            }

            // Select first imported note if any
            if (imported > 0) {
                const size_t importedCount = static_cast<size_t>(imported);
                if (importedCount <= G.notes.size()) {
                    G.selNote = boundedIntCount(G.notes.size() - importedCount);
                }
                G.selPage = 0;
                if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
                rebuildNoteList(); renderCanvas(); updateStatus(); rebuildThumbs();
            }

            char msg[256];
            snprintf(msg, sizeof(msg), "匯入完成!\n成功: %d 個\n失敗: %d 個", imported, failed);
            GtkWidget* info = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg);
            gtk_dialog_run(GTK_DIALOG(info)); gtk_widget_destroy(info);
            g_free(dirPath);
        }
    }
    gtk_widget_destroy(dlg);
}

// ============================================================
// Export
// ============================================================
static void on_export_pdf(GtkButton*, gpointer) {
    NoteData* n = curNote(); if(!n || n->pages.empty()) return;

    // Ask user: single page or all pages?
    GtkWidget* askDlg = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "匯出 PDF：選擇模式");
    gtk_dialog_add_buttons(GTK_DIALOG(askDlg),
        "目前單頁", GTK_RESPONSE_REJECT,
        "全部頁面 (單一PDF)", GTK_RESPONSE_ACCEPT,
        "全部頁面 (每頁獨立PDF)", GTK_RESPONSE_YES,
        nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(askDlg), GTK_RESPONSE_ACCEPT);
    int resp = gtk_dialog_run(GTK_DIALOG(askDlg));
    gtk_widget_destroy(askDlg);

    GtkWidget* dlg = gtk_file_chooser_dialog_new("匯出 PDF", GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_SAVE, "取消", GTK_RESPONSE_CANCEL, "儲存", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), (n->name + ".pdf").c_str());
    GtkFileFilter* f = gtk_file_filter_new(); gtk_file_filter_set_name(f, "PDF"); gtk_file_filter_add_pattern(f, "*.pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dlg);
        return;
    }

    char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    if (!fn) { gtk_widget_destroy(dlg); return; }

    if (resp == GTK_RESPONSE_REJECT) {
        // === Single page export ===
        PageData* pg = curPage();
        if (pg) {
            cairo_surface_t* ps = cairo_pdf_surface_create(fn, pg->pw, pg->ph);
            cairo_t* cr = cairo_create(ps);
            cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);
            if (pg->bgSurf && cairo_surface_status(pg->bgSurf) == CAIRO_STATUS_SUCCESS) {
                double sc = fmin(pg->pw / pg->bgW, pg->ph / pg->bgH);
                double bx = (pg->pw - pg->bgW * sc) / 2, by = (pg->ph - pg->bgH * sc) / 2;
                cairo_save(cr); cairo_translate(cr, bx, by); cairo_scale(cr, sc, sc);
                cairo_set_source_surface(cr, pg->bgSurf, 0, 0); cairo_paint(cr); cairo_restore(cr);
            }
            for (auto& img : pg->images) {
                if (!img.surf) continue;
                int ow = cairo_image_surface_get_width(img.surf);
                int oh = cairo_image_surface_get_height(img.surf);
                double sx = img.w / fmax(1, ow), sy = img.h / fmax(1, oh);
                cairo_save(cr); cairo_translate(cr, img.x, img.y); cairo_scale(cr, sx, sy);
                cairo_set_source_surface(cr, img.surf, 0, 0); cairo_paint(cr); cairo_restore(cr);
            }
            for (auto& t : pg->texts) {
                if (t.text.empty()) continue;
                PangoLayout* layout = pango_cairo_create_layout(cr);
                pango_layout_set_text(layout, t.text.c_str(), -1);
                PangoFontDescription* desc = pango_font_description_from_string("Microsoft JhengHei, Noto Sans CJK TC, PMingLiU, Arial, Sans 10");
                pango_font_description_set_size(desc, (int)(t.fontSize * PANGO_SCALE));
                pango_layout_set_font_description(layout, desc);
                cairo_set_source_rgb(cr, t.r, t.g, t.b);
                pango_cairo_update_layout(cr, layout);
                cairo_move_to(cr, t.x, t.y);
                pango_cairo_show_layout(cr, layout);
                g_object_unref(layout); pango_font_description_free(desc);
            }
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            for (auto& s : pg->strokes) {
                if (s.x.size() < 2 || s.tool == 2) continue;
                cairo_set_source_rgba(cr, s.r, s.g, s.b, s.a);
                cairo_set_line_width(cr, s.w);
                cairo_move_to(cr, s.x[0], s.y[0]);
                for (size_t i = 1; i < s.x.size(); i++) cairo_line_to(cr, s.x[i], s.y[i]);
                cairo_stroke(cr);
            }
            cairo_destroy(cr); cairo_surface_finish(ps); cairo_surface_destroy(ps);
            logInfo("已匯出 PDF (單頁): %s", fn);
            char msg[128]; snprintf(msg, sizeof(msg), "已匯出 PDF (第 %d 頁)", G.selPage + 1);
            gtk_label_set_text(GTK_LABEL(G.lblStatus), msg);
        }
    } else if (resp == GTK_RESPONSE_ACCEPT) {
        // === All pages - single PDF file using cairo's built-in multi-page ===
        // Use wide filename for Chinese support on Windows
        std::wstring fn_wide = utf8_to_wide(std::string(fn));
        cairo_surface_t* ps = nullptr;
        cairo_t* cr = nullptr;

        for (size_t pi = 0; pi < n->pages.size(); pi++) {
            PageData* pg = &n->pages[pi];

            if (pi == 0) {
                // First page: create PDF surface
                ps = cairo_pdf_surface_create(fn, pg->pw, pg->ph);
                cr = cairo_create(ps);
            } else {
                // Subsequent pages: flush, resize, and advance
                cairo_show_page(cr);
                cairo_pdf_surface_set_size(ps, pg->pw, pg->ph);
            }

            // Draw page content
            cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);
            if (pg->bgSurf && cairo_surface_status(pg->bgSurf) == CAIRO_STATUS_SUCCESS) {
                double sc = fmin(pg->pw / pg->bgW, pg->ph / pg->bgH);
                double bx = (pg->pw - pg->bgW * sc) / 2, by = (pg->ph - pg->bgH * sc) / 2;
                cairo_save(cr); cairo_translate(cr, bx, by); cairo_scale(cr, sc, sc);
                cairo_set_source_surface(cr, pg->bgSurf, 0, 0); cairo_paint(cr); cairo_restore(cr);
            }
            for (auto& img : pg->images) {
                if (!img.surf) continue;
                int ow = cairo_image_surface_get_width(img.surf);
                int oh = cairo_image_surface_get_height(img.surf);
                double sx = img.w / fmax(1, ow), sy = img.h / fmax(1, oh);
                cairo_save(cr); cairo_translate(cr, img.x, img.y); cairo_scale(cr, sx, sy);
                cairo_set_source_surface(cr, img.surf, 0, 0); cairo_paint(cr); cairo_restore(cr);
            }
            for (auto& t : pg->texts) {
                if (t.text.empty()) continue;
                PangoLayout* layout = pango_cairo_create_layout(cr);
                pango_layout_set_text(layout, t.text.c_str(), -1);
                PangoFontDescription* desc = pango_font_description_from_string("Microsoft JhengHei, Noto Sans CJK TC, PMingLiU, Arial, Sans 10");
                pango_font_description_set_size(desc, (int)(t.fontSize * PANGO_SCALE));
                pango_layout_set_font_description(layout, desc);
                cairo_set_source_rgb(cr, t.r, t.g, t.b);
                pango_cairo_update_layout(cr, layout);
                cairo_move_to(cr, t.x, t.y);
                pango_cairo_show_layout(cr, layout);
                g_object_unref(layout); pango_font_description_free(desc);
            }
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            for (auto& s : pg->strokes) {
                if (s.x.size() < 2 || s.tool == 2) continue;
                cairo_set_source_rgba(cr, s.r, s.g, s.b, s.a);
                cairo_set_line_width(cr, s.w);
                cairo_move_to(cr, s.x[0], s.y[0]);
                for (size_t i = 1; i < s.x.size(); i++) cairo_line_to(cr, s.x[i], s.y[i]);
                cairo_stroke(cr);
            }
        }

        if (cr) cairo_destroy(cr);
        if (ps) cairo_surface_finish(ps);
        if (ps) cairo_surface_destroy(ps);

        logInfo("已匯出 PDF: %s (共 %zu 頁，單一檔案)", fn, n->pages.size());
        char msg[128]; snprintf(msg, sizeof(msg), "已匯出 PDF (共 %zu 頁)", n->pages.size());
        gtk_label_set_text(GTK_LABEL(G.lblStatus), msg);
    } else if (resp == GTK_RESPONSE_YES) {
        // === All pages - separate PDF files ===
        std::string baseFn(fn);
        size_t dotPos = baseFn.find_last_of('.');
        std::string base = (dotPos != std::string::npos) ? baseFn.substr(0, dotPos) : baseFn;
        int exported = 0;
        for (size_t pi = 0; pi < n->pages.size(); pi++) {
            PageData* pg = &n->pages[pi];
            char pageFn[512];
            if (n->pages.size() > 1)
                snprintf(pageFn, sizeof(pageFn), "%s_第%zu頁.pdf", base.c_str(), pi + 1);
            else
                snprintf(pageFn, sizeof(pageFn), "%s.pdf", base.c_str());

            std::wstring pageFn_wide = utf8_to_wide(std::string(pageFn));
            FILE* f = _wfopen(pageFn_wide.c_str(), L"wb");
            if (!f) continue;
            cairo_surface_t* ps = cairo_pdf_surface_create_for_stream(nullptr, f, pg->pw, pg->ph);
            cairo_t* cr = cairo_create(ps);
            cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);
            if (pg->bgSurf && cairo_surface_status(pg->bgSurf) == CAIRO_STATUS_SUCCESS) {
                double sc = fmin(pg->pw / pg->bgW, pg->ph / pg->bgH);
                double bx = (pg->pw - pg->bgW * sc) / 2, by = (pg->ph - pg->bgH * sc) / 2;
                cairo_save(cr); cairo_translate(cr, bx, by); cairo_scale(cr, sc, sc);
                cairo_set_source_surface(cr, pg->bgSurf, 0, 0); cairo_paint(cr); cairo_restore(cr);
            }
            for (auto& img : pg->images) {
                if (!img.surf) continue;
                int ow = cairo_image_surface_get_width(img.surf);
                int oh = cairo_image_surface_get_height(img.surf);
                double sx = img.w / fmax(1, ow), sy = img.h / fmax(1, oh);
                cairo_save(cr); cairo_translate(cr, img.x, img.y); cairo_scale(cr, sx, sy);
                cairo_set_source_surface(cr, img.surf, 0, 0); cairo_paint(cr); cairo_restore(cr);
            }
            for (auto& t : pg->texts) {
                if (t.text.empty()) continue;
                PangoLayout* layout = pango_cairo_create_layout(cr);
                pango_layout_set_text(layout, t.text.c_str(), -1);
                PangoFontDescription* desc = pango_font_description_from_string("Microsoft JhengHei, Noto Sans CJK TC, PMingLiU, Arial, Sans 10");
                pango_font_description_set_size(desc, (int)(t.fontSize * PANGO_SCALE));
                pango_layout_set_font_description(layout, desc);
                cairo_set_source_rgb(cr, t.r, t.g, t.b);
                pango_cairo_update_layout(cr, layout);
                cairo_move_to(cr, t.x, t.y);
                pango_cairo_show_layout(cr, layout);
                g_object_unref(layout); pango_font_description_free(desc);
            }
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            for (auto& s : pg->strokes) {
                if (s.x.size() < 2 || s.tool == 2) continue;
                cairo_set_source_rgba(cr, s.r, s.g, s.b, s.a);
                cairo_set_line_width(cr, s.w);
                cairo_move_to(cr, s.x[0], s.y[0]);
                for (size_t i = 1; i < s.x.size(); i++) cairo_line_to(cr, s.x[i], s.y[i]);
                cairo_stroke(cr);
            }
            cairo_destroy(cr); cairo_surface_finish(ps); cairo_surface_destroy(ps);
            exported++;
        }
        logInfo("已匯出 PDF: %d 個獨立檔案", exported);
        char msg[128]; snprintf(msg, sizeof(msg), "已匯出 PDF (%d 個獨立檔案)", exported);
        gtk_label_set_text(GTK_LABEL(G.lblStatus), msg);
    }

    g_free(fn);
    gtk_widget_destroy(dlg);
}

static void on_export_png(GtkButton*, gpointer) {
    NoteData* n = curNote(); if(!n || n->pages.empty()) return;

    // Ask user: single page or all pages?
    GtkWidget* askDlg = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "匯出 PNG：選擇模式");
    gtk_dialog_add_buttons(GTK_DIALOG(askDlg),
        "目前單頁", GTK_RESPONSE_REJECT,
        "全部頁面 (每頁獨立PNG)", GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(askDlg), GTK_RESPONSE_ACCEPT);
    int resp = gtk_dialog_run(GTK_DIALOG(askDlg));
    gtk_widget_destroy(askDlg);

    GtkWidget* dlg = gtk_file_chooser_dialog_new("匯出 PNG", GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_SAVE, "取消", GTK_RESPONSE_CANCEL, "儲存", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), (n->name + ".png").c_str());
    GtkFileFilter* f = gtk_file_filter_new(); gtk_file_filter_set_name(f, "PNG"); gtk_file_filter_add_pattern(f, "*.png");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dlg);
        return;
    }

    char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    if (!fn) { gtk_widget_destroy(dlg); return; }

    if (resp == GTK_RESPONSE_REJECT) {
        // === Single page export ===
        PageData* pg = curPage();
        if (pg) {
            int pw = (int)(pg->pw), ph = (int)(pg->ph);
            cairo_surface_t* ps = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
            cairo_t* cr = cairo_create(ps);
            cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);
            if (pg->bgSurf && cairo_surface_status(pg->bgSurf) == CAIRO_STATUS_SUCCESS) {
                double sc = fmin((double)pw / pg->bgW, (double)ph / pg->bgH);
                double bx = (pw - pg->bgW * sc) / 2, by = (ph - pg->bgH * sc) / 2;
                cairo_save(cr); cairo_translate(cr, bx, by); cairo_scale(cr, sc, sc);
                cairo_set_source_surface(cr, pg->bgSurf, 0, 0); cairo_paint(cr); cairo_restore(cr);
            }
            for (auto& img : pg->images) {
                if (!img.surf) continue;
                int ow = cairo_image_surface_get_width(img.surf);
                int oh = cairo_image_surface_get_height(img.surf);
                double sx = img.w / fmax(1, ow), sy = img.h / fmax(1, oh);
                cairo_save(cr); cairo_translate(cr, img.x, img.y); cairo_scale(cr, sx, sy);
                cairo_set_source_surface(cr, img.surf, 0, 0); cairo_paint(cr); cairo_restore(cr);
            }
            for (auto& t : pg->texts) {
                if (t.text.empty()) continue;
                PangoLayout* layout = pango_cairo_create_layout(cr);
                pango_layout_set_text(layout, t.text.c_str(), -1);
                PangoFontDescription* desc = pango_font_description_from_string("Microsoft JhengHei, Noto Sans CJK TC, PMingLiU, Arial, Sans 10");
                pango_font_description_set_size(desc, (int)(t.fontSize * PANGO_SCALE));
                pango_layout_set_font_description(layout, desc);
                cairo_set_source_rgb(cr, t.r, t.g, t.b);
                pango_cairo_update_layout(cr, layout);
                cairo_move_to(cr, t.x, t.y);
                pango_cairo_show_layout(cr, layout);
                g_object_unref(layout); pango_font_description_free(desc);
            }
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            for (auto& s : pg->strokes) {
                if (s.x.size() < 2) continue;
                if (s.tool == 2) { cairo_set_source_rgb(cr, 1, 1, 1); cairo_set_line_width(cr, s.w * 3); }
                else { cairo_set_source_rgba(cr, s.r, s.g, s.b, s.a); cairo_set_line_width(cr, s.w); }
                cairo_move_to(cr, s.x[0], s.y[0]);
                for (size_t i = 1; i < s.x.size(); i++) cairo_line_to(cr, s.x[i], s.y[i]);
                cairo_stroke(cr);
            }
            cairo_surface_write_to_png(ps, fn);
            cairo_destroy(cr); cairo_surface_destroy(ps);
            logInfo("已匯出 PNG (單頁): %s", fn);
            char msg[128]; snprintf(msg, sizeof(msg), "已匯出 PNG (第 %d 頁)", G.selPage + 1);
            gtk_label_set_text(GTK_LABEL(G.lblStatus), msg);
        }
    } else {
        // === All pages - separate PNG files ===
        std::string baseFn(fn);
        size_t dotPos = baseFn.find_last_of('.');
        std::string base = (dotPos != std::string::npos) ? baseFn.substr(0, dotPos) : baseFn;
        int exported = 0;

        for (size_t pi = 0; pi < n->pages.size(); pi++) {
            PageData* pg = &n->pages[pi];
            char pageFn[512];
            if (n->pages.size() > 1)
                snprintf(pageFn, sizeof(pageFn), "%s_第%zu頁.png", base.c_str(), pi + 1);
            else
                snprintf(pageFn, sizeof(pageFn), "%s.png", base.c_str());

            std::wstring pageFn_wide = utf8_to_wide(std::string(pageFn));
            int pw = (int)(pg->pw), ph = (int)(pg->ph);
            cairo_surface_t* ps = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
            cairo_t* cr = cairo_create(ps);
            cairo_set_source_rgb(cr, 1, 1, 1); cairo_paint(cr);
            if (pg->bgSurf && cairo_surface_status(pg->bgSurf) == CAIRO_STATUS_SUCCESS) {
                double sc = fmin((double)pw / pg->bgW, (double)ph / pg->bgH);
                double bx = (pw - pg->bgW * sc) / 2, by = (ph - pg->bgH * sc) / 2;
                cairo_save(cr); cairo_translate(cr, bx, by); cairo_scale(cr, sc, sc);
                cairo_set_source_surface(cr, pg->bgSurf, 0, 0); cairo_paint(cr); cairo_restore(cr);
            }
            for (auto& img : pg->images) {
                if (!img.surf) continue;
                int ow = cairo_image_surface_get_width(img.surf);
                int oh = cairo_image_surface_get_height(img.surf);
                double sx = img.w / fmax(1, ow), sy = img.h / fmax(1, oh);
                cairo_save(cr); cairo_translate(cr, img.x, img.y); cairo_scale(cr, sx, sy);
                cairo_set_source_surface(cr, img.surf, 0, 0); cairo_paint(cr); cairo_restore(cr);
            }
            for (auto& t : pg->texts) {
                if (t.text.empty()) continue;
                PangoLayout* layout = pango_cairo_create_layout(cr);
                pango_layout_set_text(layout, t.text.c_str(), -1);
                PangoFontDescription* desc = pango_font_description_from_string("Microsoft JhengHei, Noto Sans CJK TC, PMingLiU, Arial, Sans 10");
                pango_font_description_set_size(desc, (int)(t.fontSize * PANGO_SCALE));
                pango_layout_set_font_description(layout, desc);
                cairo_set_source_rgb(cr, t.r, t.g, t.b);
                pango_cairo_update_layout(cr, layout);
                cairo_move_to(cr, t.x, t.y);
                pango_cairo_show_layout(cr, layout);
                g_object_unref(layout); pango_font_description_free(desc);
            }
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            for (auto& s : pg->strokes) {
                if (s.x.size() < 2) continue;
                if (s.tool == 2) { cairo_set_source_rgb(cr, 1, 1, 1); cairo_set_line_width(cr, s.w * 3); }
                else { cairo_set_source_rgba(cr, s.r, s.g, s.b, s.a); cairo_set_line_width(cr, s.w); }
                cairo_move_to(cr, s.x[0], s.y[0]);
                for (size_t i = 1; i < s.x.size(); i++) cairo_line_to(cr, s.x[i], s.y[i]);
                cairo_stroke(cr);
            }
            cairo_surface_write_to_png(ps, pageFn);
            cairo_destroy(cr); cairo_surface_destroy(ps);
            exported++;
        }
        logInfo("已匯出 PNG: %d 個獨立檔案", exported);
        char msg[128]; snprintf(msg, sizeof(msg), "已匯出 PNG (%d 個獨立檔案)", exported);
        gtk_label_set_text(GTK_LABEL(G.lblStatus), msg);
    }

    g_free(fn);
    gtk_widget_destroy(dlg);
}

// Export note as .onote file (can be re-imported and edited)
static void on_export_note(GtkButton*, gpointer) {
    NoteData* n = curNote(); if(!n) return;
    GtkWidget* dlg = gtk_file_chooser_dialog_new("匯出筆記", GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_SAVE, "取消", GTK_RESPONSE_CANCEL, "匯出", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), (n->name + ".onote").c_str());
    GtkFileFilter* f = gtk_file_filter_new(); gtk_file_filter_set_name(f, "OfflineNote 筆記");
    gtk_file_filter_add_pattern(f, "*.onote");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (fn) {
            std::string target(fn);
            if (target.size() < 6 || target.substr(target.size() - 6) != ".onote") target += ".onote";

            // Write directly using FILE* for wide path support
            FILE* f = wfopen_utf8(target, L"wb");
            if (f) {
                const fs::path targetNotePath = utf8_to_path(target);
                const fs::path sourceNotePath = utf8_to_path(note_filename(n));
                fprintf(f, "# OfflineNote v1\nname=%s\npages=%zu\n", n->name.c_str(), n->pages.size());
                for (size_t pi = 0; pi < n->pages.size(); pi++) {
                    PageData* pg = &n->pages[pi];
                    fprintf(f, "[page %zu]\npw=%g\nph=%g\nstrokes=%zu\nimages=%zu\ntexts=%zu\n",
                            pi, pg->pw, pg->ph, pg->strokes.size(), pg->images.size(), pg->texts.size());
                    for (auto& s : pg->strokes) {
                        fprintf(f, "s 0 w=%g r=%g g=%g b=%g a=%g t=%d\n", s.w, s.r, s.g, s.b, s.a, s.tool);
                        for (size_t j = 0; j < s.x.size(); j++)
                            fprintf(f, "p %g %g\n", s.x[j], s.y[j]);
                    }
                    for (auto& t : pg->texts) {
                        std::string esc = LegacyNoteTextCodec::encode(t.text);
                        fprintf(f, "t 0 x=%g y=%g fs=%g r=%g g=%g b=%g txt=%s\n",
                                t.x, t.y, t.fontSize, t.r, t.g, t.b, esc.c_str());
                    }
                    for (auto& img : pg->images) {
                        if (!img.srcFile.empty()) {
                            const std::string storedPath = LegacyNoteResourceHelper::stageForNote(
                                targetNotePath, utf8_to_path(img.srcFile), sourceNotePath);
                            if (!storedPath.empty()) {
                                fprintf(f, "img 0 x=%g y=%g w=%g h=%g src=%s\n",
                                        img.x, img.y, img.w, img.h, storedPath.c_str());
                            }
                        }
                    }
                    if (!pg->bgFile.empty()) {
                        const std::string storedPath = LegacyNoteResourceHelper::stageForNote(
                            targetNotePath, utf8_to_path(pg->bgFile), sourceNotePath);
                        if (!storedPath.empty()) {
                            fprintf(f, "bg src=%s w=%g h=%g\n", storedPath.c_str(), pg->bgW, pg->bgH);
                        }
                    }
                }
                fclose(f);
                logInfo("已匯出筆記: %s", target.c_str());
                gtk_label_set_text(GTK_LABEL(G.lblStatus), "已匯出筆記");
            } else {
                logInfo("匯出失敗: 無法建立 %s", target.c_str());
            }
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

// Batch export all notes as .onote files
static void on_batch_export(GtkButton*, gpointer) {
    if (G.notes.empty()) {
        GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "沒有筆記可匯出");
        gtk_dialog_run(GTK_DIALOG(err)); gtk_widget_destroy(err);
        return;
    }

    GtkWidget* dlg = gtk_file_chooser_dialog_new("批次匯出所有筆記", GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "取消", GTK_RESPONSE_CANCEL, "選擇目錄", GTK_RESPONSE_ACCEPT, nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* dirPath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (dirPath) {
            std::string targetDir(dirPath);
            int exported = 0;
            int failed = 0;
            std::string failedNames;

            for (size_t i = 0; i < G.notes.size(); i++) {
                NoteData* nd = &G.notes[i];
                std::string fn = nd->name;
                if (fn.empty()) fn = "unnamed_" + std::to_string(i);
                for (auto& c : fn) {
                    if (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
                }
                std::string target = targetDir + "/" + fn + ".onote";

                // Write directly using FILE* for wide path support
                FILE* f = wfopen_utf8(target, L"wb");
                if (f) {
                    const fs::path targetNotePath = utf8_to_path(target);
                    const fs::path sourceNotePath = utf8_to_path(note_filename(nd));
                    fprintf(f, "# OfflineNote v1\nname=%s\npages=%zu\n", nd->name.c_str(), nd->pages.size());
                    for (size_t pi = 0; pi < nd->pages.size(); pi++) {
                        PageData* pg = &nd->pages[pi];
                        fprintf(f, "[page %zu]\npw=%g\nph=%g\nstrokes=%zu\nimages=%zu\ntexts=%zu\n",
                                pi, pg->pw, pg->ph, pg->strokes.size(), pg->images.size(), pg->texts.size());
                        for (auto& s : pg->strokes) {
                            fprintf(f, "s 0 w=%g r=%g g=%g b=%g a=%g t=%d\n", s.w, s.r, s.g, s.b, s.a, s.tool);
                            for (size_t j = 0; j < s.x.size(); j++)
                                fprintf(f, "p %g %g\n", s.x[j], s.y[j]);
                        }
                        for (auto& t : pg->texts) {
                            std::string esc = LegacyNoteTextCodec::encode(t.text);
                            fprintf(f, "t 0 x=%g y=%g fs=%g r=%g g=%g b=%g txt=%s\n",
                                    t.x, t.y, t.fontSize, t.r, t.g, t.b, esc.c_str());
                        }
                        for (auto& img : pg->images) {
                            if (!img.srcFile.empty()) {
                                const std::string storedPath = LegacyNoteResourceHelper::stageForNote(
                                    targetNotePath, utf8_to_path(img.srcFile), sourceNotePath);
                                if (!storedPath.empty()) {
                                    fprintf(f, "img 0 x=%g y=%g w=%g h=%g src=%s\n",
                                            img.x, img.y, img.w, img.h, storedPath.c_str());
                                }
                            }
                        }
                        if (!pg->bgFile.empty()) {
                            const std::string storedPath = LegacyNoteResourceHelper::stageForNote(
                                targetNotePath, utf8_to_path(pg->bgFile), sourceNotePath);
                            if (!storedPath.empty()) {
                                fprintf(f, "bg src=%s w=%g h=%g\n", storedPath.c_str(), pg->bgW, pg->bgH);
                            }
                        }
                    }
                    fclose(f);
                    exported++; continue;
                }
                failed++;
                if (!failedNames.empty()) failedNames += ", ";
                failedNames += nd->name;
            }

            char msg[512];
            if (failed > 0)
                snprintf(msg, sizeof(msg), "匯出完成!\n成功: %d 個\n失敗: %d 個 (%s)\n\n目錄:\n%s",
                         exported, failed, failedNames.c_str(), dirPath);
            else
                snprintf(msg, sizeof(msg), "匯出完成!\n成功: %d 個\n失敗: 0 個\n\n目錄:\n%s",
                         exported, dirPath);
            GtkWidget* info = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg);
            gtk_dialog_run(GTK_DIALOG(info)); gtk_widget_destroy(info);
            g_free(dirPath);
        }
    }
    gtk_widget_destroy(dlg);
}

// Import note from .onote file
static void on_import_note(GtkButton*, gpointer) {
    GtkWidget* dlg=gtk_file_chooser_dialog_new("匯入筆記",GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_OPEN,"取消",GTK_RESPONSE_CANCEL,"匯入",GTK_RESPONSE_ACCEPT,nullptr);
    GtkFileFilter* f=gtk_file_filter_new(); gtk_file_filter_set_name(f,"OfflineNote 筆記");
    gtk_file_filter_add_pattern(f,"*.onote");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg),f);
    if(gtk_dialog_run(GTK_DIALOG(dlg))==GTK_RESPONSE_ACCEPT){
        char* fn=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if(fn){
            // On Windows, gtk_file_chooser_get_filename returns locale-encoded string
            // We need to convert it to UTF-8 for our wfopen_utf8 function
            std::string utf8Path = fn;
            on_load_note(utf8Path);
            logInfo("已匯入筆記: %s", fn);
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

// PDF 匯入
#include "../import/PdfImporter.h"

#if 0
static void on_import_pdf(GtkButton*, gpointer) {
    GtkWidget* dlg = gtk_file_chooser_dialog_new("匯入 PDF", GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "取消", GTK_RESPONSE_CANCEL, "匯入", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "PDF 文件");
    gtk_file_filter_add_pattern(f, "*.pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (fn) {
            std::string pdfPath(fn);
            std::string saveDir = get_save_dir();

            // 建立筆記
            G.notes.push_back(NoteData());
            NoteData* nd = &G.notes.back();

            // 從檔案名取筆記名
            size_t pos = pdfPath.find_last_of("/\\");
            std::string baseName = (pos != std::string::npos) ? pdfPath.substr(pos + 1) : pdfPath;
            if (baseName.size() > 4 && baseName.substr(baseName.size() - 4) == ".pdf")
                baseName = baseName.substr(0, baseName.size() - 4);
            nd->name = baseName;

            // 建立專屬目錄存放 PDF 頁面 PNG
            std::string noteDir = saveDir + "/" + baseName + "_pdf";
            auto pages = PdfImporter::importPdf(pdfPath, noteDir);

            for (auto& pg : pages) {
                nd->pages.push_back(PageData());
                PageData* pd = &nd->pages.back();
                pd->pw = pg.width;
                pd->ph = pg.height;
                pd->pdfPageNum = pg.pdfPageNum;
                pd->bgFile = baseName + "_pdf/" + pg.bgImagePath;
                pd->bgW = pg.width;
                pd->bgH = pg.height;

                // 載入背景
                std::string fullPath = saveDir + "/" + pd->bgFile;
                GError* err = nullptr;
                GdkPixbuf* pb = gdk_pixbuf_new_from_file(fullPath.c_str(), &err);
                if (pb) {
                    int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
                    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
                    cairo_t* cr = cairo_create(surf);
                    gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
                    cairo_paint(cr);
                    cairo_destroy(cr);
                    g_object_unref(pb);
                    pd->bgSurf = surf;
                } else {
                    if (err) g_error_free(err);
                }
            }

            if (!pages.empty()) {
                G.selNote = (int)G.notes.size() - 1;
                G.selPage = 0;
                if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
                rebuildNoteList(); renderCanvas(); updateStatus(); rebuildThumbs();

                char msg[256];
                snprintf(msg, sizeof(msg), "PDF 匯入成功!\n%d 頁已匯入", (int)pages.size());
                GtkWidget* info = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg);
                gtk_dialog_run(GTK_DIALOG(info)); gtk_widget_destroy(info);
            } else {
                G.notes.pop_back();
                GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
                    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "PDF 匯入失敗或無效");
                gtk_dialog_run(GTK_DIALOG(err)); gtk_widget_destroy(err);
            }

            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}
#endif

static void on_import_pdf_safe(GtkButton*, gpointer) {
    GtkWidget* dlg = gtk_file_chooser_dialog_new("匯入 PDF", GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "取消", GTK_RESPONSE_CANCEL, "匯入", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "PDF 文件");
    gtk_file_filter_add_pattern(f, "*.pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (fn) {
            struct ImportedPdfPageData {
                std::string bgFile;
                cairo_surface_t* bgSurf = nullptr;
                double width = 0.0;
                double height = 0.0;
                int pdfPageNum = -1;

                ImportedPdfPageData() = default;
                ImportedPdfPageData(const ImportedPdfPageData&) = delete;
                ImportedPdfPageData& operator=(const ImportedPdfPageData&) = delete;
                ImportedPdfPageData(ImportedPdfPageData&& other) noexcept
                    : bgFile(std::move(other.bgFile)),
                      bgSurf(other.bgSurf),
                      width(other.width),
                      height(other.height),
                      pdfPageNum(other.pdfPageNum) {
                    other.bgSurf = nullptr;
                }
                ImportedPdfPageData& operator=(ImportedPdfPageData&& other) noexcept {
                    if (this == &other) return *this;
                    if (bgSurf) cairo_surface_destroy(bgSurf);
                    bgFile = std::move(other.bgFile);
                    bgSurf = other.bgSurf;
                    width = other.width;
                    height = other.height;
                    pdfPageNum = other.pdfPageNum;
                    other.bgSurf = nullptr;
                    return *this;
                }
                ~ImportedPdfPageData() {
                    if (bgSurf) cairo_surface_destroy(bgSurf);
                }
            };

            std::string pdfPath(fn);
            auto pathResult = PathValidator::validatePdfPath(utf8_to_path(pdfPath), true);
            if (!pathResult.valid) {
                GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
                    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "PDF 路徑無效，已取消匯入。");
                gtk_dialog_run(GTK_DIALOG(err));
                gtk_widget_destroy(err);
                g_free(fn);
                gtk_widget_destroy(dlg);
                return;
            }

            pdfPath = pathResult.canonical.u8string();
            const fs::path saveDirPath = utf8_to_path(get_save_dir());
            const std::string baseName = pdf_import_note_name(pdfPath);
            const fs::path importDir = PdfImportPlan::uniquePdfImportDirectory(saveDirPath, baseName);
            const std::string importDirName = importDir.filename().u8string();

            auto pages = PdfImporter::importPdf(pdfPath, importDir.u8string());
            if (pages.empty()) {
                GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
                    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "PDF 匯入失敗，未建立任何頁面。");
                gtk_dialog_run(GTK_DIALOG(err));
                gtk_widget_destroy(err);
                g_free(fn);
                gtk_widget_destroy(dlg);
                return;
            }

            std::vector<ImportedPdfPageData> preparedPages;
            preparedPages.reserve(pages.size());
            bool preloadFailed = false;
            for (const auto& pg : pages) {
                ImportedPdfPageData prepared;
                prepared.bgFile = importDirName + "/" + pg.bgImagePath;
                prepared.width = pg.width;
                prepared.height = pg.height;
                prepared.pdfPageNum = pg.pdfPageNum;

                const fs::path fullPath = saveDirPath / fs::u8path(prepared.bgFile);
                prepared.bgSurf = load_image_surface(fullPath.u8string().c_str());
                if (!prepared.bgSurf || cairo_surface_status(prepared.bgSurf) != CAIRO_STATUS_SUCCESS) {
                    preloadFailed = true;
                    break;
                }

                preparedPages.push_back(std::move(prepared));
            }

            if (preloadFailed || preparedPages.size() != pages.size()) {
                std::error_code ec;
                fs::remove_all(importDir, ec);
                GtkWidget* err = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
                    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "PDF 已轉出頁面，但背景載入失敗，匯入已取消以避免假成功。");
                gtk_dialog_run(GTK_DIALOG(err));
                gtk_widget_destroy(err);
                g_free(fn);
                gtk_widget_destroy(dlg);
                return;
            }

            NoteData* nd = curNote();
            bool createdNewNote = false;
            if (!nd) {
                G.notes.push_back(NoteData());
                nd = &G.notes.back();
                G.selNote = (int)G.notes.size() - 1;
                createdNewNote = true;
            }

            const bool replaceBlankNote = nd && is_blank_note_for_pdf_import(*nd);
            const int firstImportedPage = replaceBlankNote ? 0 : (int)nd->pages.size();
            if (replaceBlankNote) {
                nd->pages.clear();
                nd->name = baseName;
            } else if (createdNewNote || nd->name.empty()) {
                nd->name = baseName;
            }

            for (auto& prepared : preparedPages) {
                nd->pages.push_back(PageData());
                PageData* pd = &nd->pages.back();
                pd->pw = prepared.width;
                pd->ph = prepared.height;
                pd->pdfPageNum = prepared.pdfPageNum;
                pd->bgFile = prepared.bgFile;
                pd->bgW = prepared.width;
                pd->bgH = prepared.height;
                pd->bgSurf = prepared.bgSurf;
                prepared.bgSurf = nullptr;
            }

            nd->dirty = 1;
            G.selPage = firstImportedPage;
            clamp_page_scroll(curPage());
            rebuildNoteList();
            rebuildThumbs();
            invalidate_canvas_and_refresh();

            char msg[256];
            snprintf(msg, sizeof(msg), "PDF 匯入成功。\n%d 頁已加入目前筆記。", (int)preparedPages.size());
            GtkWidget* info = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg);
            gtk_dialog_run(GTK_DIALOG(info));
            gtk_widget_destroy(info);

            g_free(fn);
        }
    }

    gtk_widget_destroy(dlg);
}

// ============================================================
// Image loading - GdkPixbuf supports PNG/JPEG/BMP/GIF
// ============================================================
static cairo_surface_t* load_image_surface(const char* filename) {
    GError* err = nullptr;
    GdkPixbuf* pb = gdk_pixbuf_new_from_file(filename, &err);
    if (!pb) {
        if (err) {
            logInfo("載入圖片失敗: %s", err->message);
            g_error_free(err);
        }
        return nullptr;
    }
    int w = gdk_pixbuf_get_width(pb);
    int h = gdk_pixbuf_get_height(pb);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surf);
    gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    g_object_unref(pb);
    return surf;
}

static void on_insert_img(GtkButton*, gpointer) {
    PageData* pg=curPage(); if(!pg) return;
    GtkWidget* dlg=gtk_file_chooser_dialog_new("插入圖片",GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_OPEN,"取消",GTK_RESPONSE_CANCEL,"插入",GTK_RESPONSE_ACCEPT,nullptr);
    GtkFileFilter* f=gtk_file_filter_new(); gtk_file_filter_set_name(f,"圖片");
    gtk_file_filter_add_pattern(f,"*.png");gtk_file_filter_add_pattern(f,"*.jpg");gtk_file_filter_add_pattern(f,"*.jpeg");gtk_file_filter_add_pattern(f,"*.bmp");gtk_file_filter_add_pattern(f,"*.gif");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg),f);

    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if(resp == GTK_RESPONSE_ACCEPT){
        char* fn=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if(fn){
            logInfo("正在載入圖片: %s", fn);
            cairo_surface_t* img = load_image_surface(fn);
            if(img && cairo_surface_status(img)==CAIRO_STATUS_SUCCESS){
                int iw=cairo_image_surface_get_width(img),ih=cairo_image_surface_get_height(img);
                double sc=fmin(300.0/iw, 300.0/ih);
                pg->images.push_back(ImgEl());
                ImgEl* ie=&pg->images.back();
                ie->surf=img; ie->x=50;ie->y=50; ie->w=iw*sc;ie->h=ih*sc;
                ie->srcFile = fn;
                NoteData* n=curNote();if(n)n->dirty=1;
                renderCanvas();updateStatus();
                logInfo("已插入圖片: %dx%d", iw, ih);
            }else{
                GtkWidget* err=gtk_message_dialog_new(GTK_WINDOW(G.window),GTK_DIALOG_MODAL,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,
                    "無法載入圖片:\n%s", fn);
                gtk_dialog_run(GTK_DIALOG(err));gtk_widget_destroy(err);
                if(img)cairo_surface_destroy(img);
            }
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_load_bg(GtkButton*, gpointer) {
    PageData* pg=curPage(); if(!pg) return;
    GtkWidget* dlg=gtk_file_chooser_dialog_new("載入背景",GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_OPEN,"取消",GTK_RESPONSE_CANCEL,"載入",GTK_RESPONSE_ACCEPT,nullptr);
    GtkFileFilter* f=gtk_file_filter_new(); gtk_file_filter_set_name(f,"圖片 (PNG/JPG/BMP)");
    gtk_file_filter_add_pattern(f,"*.png");gtk_file_filter_add_pattern(f,"*.jpg");gtk_file_filter_add_pattern(f,"*.jpeg");gtk_file_filter_add_pattern(f,"*.bmp");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg),f);

    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if(resp == GTK_RESPONSE_ACCEPT){
        char* fn=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if(fn){
            logInfo("正在載入背景: %s", fn);
            cairo_surface_t* bg = load_image_surface(fn);
            if(bg && cairo_surface_status(bg)==CAIRO_STATUS_SUCCESS){
                if(pg->bgSurf)cairo_surface_destroy(pg->bgSurf);
                pg->bgSurf=bg;
                pg->bgW=cairo_image_surface_get_width(bg);
                pg->bgH=cairo_image_surface_get_height(bg);
                pg->bgFile = fn;
                if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
                NoteData* n=curNote();if(n)n->dirty=1;
                renderCanvas();updateStatus();
                logInfo("已載入背景");
            }else{
                GtkWidget* err=gtk_message_dialog_new(GTK_WINDOW(G.window),GTK_DIALOG_MODAL,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,
                    "無法載入背景:\n%s", fn);
                gtk_dialog_run(GTK_DIALOG(err));gtk_widget_destroy(err);
                if(bg)cairo_surface_destroy(bg);
            }
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_page_settings(GtkButton*, gpointer) {
    PageData* pg=curPage(); if(!pg) return;
    GtkWidget* dlg=gtk_dialog_new_with_buttons("頁面設定",GTK_WINDOW(G.window),
        (GtkDialogFlags)(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
        "確定",GTK_RESPONSE_ACCEPT,"取消",GTK_RESPONSE_CANCEL,nullptr);
    GtkWidget* content=gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content),12);
    GtkWidget* ws=gtk_spin_button_new_with_range(200,2000,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(ws),pg->pw);
    GtkWidget* hs=gtk_spin_button_new_with_range(200,2000,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(hs),pg->ph);
    GtkWidget* hb;
    hb=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(hb),gtk_label_new("寬 (pt)"),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(hb),ws,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(content),hb,FALSE,FALSE,4);
    hb=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(hb),gtk_label_new("高 (pt)"),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(hb),hs,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(content),hb,FALSE,FALSE,4);
    const char* ml[]={"左","上","下","右"};
    GtkWidget* spins[4];
    for(int i=0;i<4;i++){spins[i]=gtk_spin_button_new_with_range(0,200,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(spins[i]),G.margins[i]);hb=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(hb),gtk_label_new(ml[i]),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(hb),spins[i],TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(content),hb,FALSE,FALSE,4);}
    gtk_widget_show_all(content);
    if(gtk_dialog_run(GTK_DIALOG(dlg))==GTK_RESPONSE_ACCEPT){
        pg->pw=gtk_spin_button_get_value(GTK_SPIN_BUTTON(ws));pg->ph=gtk_spin_button_get_value(GTK_SPIN_BUTTON(hs));
        for(int i=0;i<4;i++)G.margins[i]=(int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spins[i]));
        if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}renderCanvas();
    }
    gtk_widget_destroy(dlg);
}

static void on_about(GtkButton*, gpointer) {
    GtkWidget* dlg=gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg),"OfflineNote");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dlg),OFFLINENOTE_APP_VERSION);
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dlg),"離線筆記本");
    gtk_about_dialog_set_license(GTK_ABOUT_DIALOG(dlg),"GPL-2.0");
    gtk_dialog_run(GTK_DIALOG(dlg));gtk_widget_destroy(dlg);
}
static void on_quit(GtkButton*, gpointer) {
    if (G.window) {
        gtk_window_close(GTK_WINDOW(G.window));
    }
}

// ============================================================
// UI Builder helpers
// ============================================================
static void tb(GtkToolbar* tb, const char* lb, GCallback cb) {
    GtkToolItem* btn=gtk_tool_button_new(nullptr,lb);
    gtk_tool_item_set_is_important(btn, TRUE);
    gtk_widget_set_tooltip_text(GTK_WIDGET(btn), lb);
    g_signal_connect(btn,"clicked",cb,nullptr);
    gtk_toolbar_insert(tb,btn,-1);
}

// ============================================================
// MainWindow
// ============================================================
MainWindow::MainWindow(GtkApplication* app, AppController& ctrl) : app_(app), controller_(ctrl) {
    G.ctrl = &ctrl;
    window_ = gtk_application_window_new(app);
    G.window = window_;
    gtk_window_set_default_size(GTK_WINDOW(window_), 1280, 800);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);

    // ── CSS for clear note selection highlight ──
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        ".note-selected {\n"
        "  background-color: #1a73e8;\n"
        "  color: #ffffff;\n"
        "  font-weight: bold;\n"
        "}\n"
        ".note-selected label {\n"
        "  color: #ffffff;\n"
        "  font-weight: bold;\n"
        "}\n"
        ".note-selected button {\n"
        "  background-color: rgba(255,255,255,0.3);\n"
        "  color: #ffffff;\n"
        "}\n"
        "list row:hover {\n"
        "  background-color: #e8f0fe;\n"
        "}\n"
        ".sidebar-label {\n"
        "  font-weight: bold;\n"
        "  margin: 4px;\n"
        "}\n",
        -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    // Set title with note name
    updateWindowTitle();

    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), mainBox);

    // ── Menu bar ──
    GtkWidget* menuBar = gtk_menu_bar_new();
    {
        GtkWidget* mi=gtk_menu_item_new_with_label("檔案");GtkWidget* m=gtk_menu_new();GtkWidget* it;
        it=gtk_menu_item_new_with_label("新建筆記");g_signal_connect(it,"activate",G_CALLBACK(on_newnote),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("儲存");g_signal_connect(it,"activate",G_CALLBACK(on_save),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new());
        it=gtk_menu_item_new_with_label("匯出 PDF");g_signal_connect(it,"activate",G_CALLBACK(on_export_pdf),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("匯出 PNG");g_signal_connect(it,"activate",G_CALLBACK(on_export_png),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("匯出筆記 (.onote)");g_signal_connect(it,"activate",G_CALLBACK(on_export_note),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("批次匯出所有筆記");g_signal_connect(it,"activate",G_CALLBACK(on_batch_export),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("匯入筆記 (.onote)");g_signal_connect(it,"activate",G_CALLBACK(on_import_note),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new());
        it=gtk_menu_item_new_with_label("結束");g_signal_connect(it,"activate",G_CALLBACK(on_quit),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi),m);gtk_menu_shell_append(GTK_MENU_SHELL(menuBar),mi);
    }
    {
        GtkWidget* mi=gtk_menu_item_new_with_label("編輯");GtkWidget* m=gtk_menu_new();GtkWidget* it;
        it=gtk_menu_item_new_with_label("復原 (Ctrl+Z)");g_signal_connect(it,"activate",G_CALLBACK(on_undo),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("刪除");g_signal_connect(it,"activate",G_CALLBACK(on_del),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new());
        it=gtk_menu_item_new_with_label("插入圖片");g_signal_connect(it,"activate",G_CALLBACK(on_insert_img),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("載入背景");g_signal_connect(it,"activate",G_CALLBACK(on_load_bg),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new());
        it=gtk_menu_item_new_with_label("頁面設定");g_signal_connect(it,"activate",G_CALLBACK(on_page_settings),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi),m);gtk_menu_shell_append(GTK_MENU_SHELL(menuBar),mi);
    }
    {
        GtkWidget* mi=gtk_menu_item_new_with_label("說明");GtkWidget* m=gtk_menu_new();GtkWidget* it;
        it=gtk_menu_item_new_with_label("關於");g_signal_connect(it,"activate",G_CALLBACK(on_about),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi),m);gtk_menu_shell_append(GTK_MENU_SHELL(menuBar),mi);
    }
    gtk_box_pack_start(GTK_BOX(mainBox), menuBar, FALSE, FALSE, 0);

    // ── Toolbar ──
    GtkToolbar* toolbar = GTK_TOOLBAR(gtk_toolbar_new());
    gtk_toolbar_set_style(toolbar, GTK_TOOLBAR_BOTH_HORIZ);
    gtk_toolbar_set_icon_size(toolbar, GTK_ICON_SIZE_SMALL_TOOLBAR);

    tb(toolbar,"📄 新增筆記",G_CALLBACK(on_newnote));
    tb(toolbar,"💾 儲存",G_CALLBACK(on_save));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"↩ 復原",G_CALLBACK(on_undo));
    tb(toolbar,"↪ 重做",G_CALLBACK(on_redo));
    tb(toolbar,"🗑 清除頁面",G_CALLBACK(on_del));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"✏ 鋼筆",G_CALLBACK(on_tool_pen));
    tb(toolbar,"🖍 螢光筆",G_CALLBACK(on_tool_hl));
    tb(toolbar,"✖ 橡皮擦",G_CALLBACK(on_tool_eraser));
    tb(toolbar,"🔤 插入文字",G_CALLBACK(on_tool_text));
    tb(toolbar,"☞ 選取/移動",G_CALLBACK(on_tool_select));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"🖼 插入圖片",G_CALLBACK(on_insert_img));
    tb(toolbar,"📁 載入背景",G_CALLBACK(on_load_bg));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"🔍 放大",G_CALLBACK(on_zoomin));
    tb(toolbar,"🔍 縮小",G_CALLBACK(on_zoomout));
    tb(toolbar,"🔍 適合畫面",G_CALLBACK(on_zoomfit));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"📄 匯出 PDF",G_CALLBACK(on_export_pdf));
    tb(toolbar,"📤 匯出筆記",G_CALLBACK(on_export_note));
    tb(toolbar,"📦 批次匯出",G_CALLBACK(on_batch_export));
    tb(toolbar,"📥 匯入筆記",G_CALLBACK(on_import_note));
    tb(toolbar,"📂 批次匯入",G_CALLBACK(on_batch_import));
    tb(toolbar,"📕 匯入 PDF",G_CALLBACK(on_import_pdf_safe));

    // Color button
    GtkWidget* colorBtn = gtk_button_new_with_label("🎨顏色");
    g_signal_connect(colorBtn, "clicked", G_CALLBACK(on_color), nullptr);
    GtkToolItem* ci = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(ci), colorBtn);
    gtk_toolbar_insert(toolbar, ci, -1);

    // Pen size
    GtkWidget* sc=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,0.5,20.0,0.5);
    gtk_range_set_value(GTK_RANGE(sc),2.0);gtk_widget_set_size_request(sc,80,-1);
    g_signal_connect(sc,"value-changed",G_CALLBACK(on_pensize),nullptr);
    GtkToolItem* si=gtk_tool_item_new();gtk_container_add(GTK_CONTAINER(si),sc);gtk_toolbar_insert(toolbar,si,-1);
    gtk_box_pack_start(GTK_BOX(mainBox), GTK_WIDGET(toolbar), FALSE, FALSE, 0);

    // ── Content area: Drawing area (centered) + Right sidebar ──
    GtkWidget* contentBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(mainBox), contentBox, TRUE, TRUE, 0);

    // Drawing area wrapped in GtkOverlay (takes most space, centered)
    G.overlay = gtk_overlay_new();
    G.drawingArea = gtk_drawing_area_new();
    gtk_widget_set_events(G.drawingArea,
        GDK_EXPOSURE_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|
        GDK_POINTER_MOTION_MASK|GDK_SCROLL_MASK|GDK_KEY_PRESS_MASK);
    g_signal_connect(G.drawingArea,"draw",G_CALLBACK(on_draw),nullptr);
    g_signal_connect(G.drawingArea,"button-press-event",G_CALLBACK(on_btnpress),nullptr);
    g_signal_connect(G.drawingArea,"button-release-event",G_CALLBACK(on_btnrelease),nullptr);
    g_signal_connect(G.drawingArea,"motion-notify-event",G_CALLBACK(on_motion),nullptr);
    g_signal_connect(G.drawingArea,"scroll-event",G_CALLBACK(on_scroll),nullptr);
    g_signal_connect(G.drawingArea,"key-press-event",G_CALLBACK(on_keypress),nullptr);
    gtk_container_add(GTK_CONTAINER(G.overlay), G.drawingArea);
    gtk_box_pack_start(GTK_BOX(contentBox), G.overlay, TRUE, TRUE, 0);

    // Right sidebar (note list, thumbnails, search, properties)
    GtkWidget* sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(sidebar, 180, -1);
    gtk_box_pack_end(GTK_BOX(contentBox), sidebar, FALSE, FALSE, 0);

    // Separator between drawing area and sidebar
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(contentBox), sep, FALSE, FALSE, 0);

    // Note list (top of sidebar)
    G.noteList = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(G.noteList), GTK_SELECTION_NONE);
    g_signal_connect(G.noteList,"row-activated",G_CALLBACK(on_note_activated),nullptr);
    g_signal_connect(G.noteList, "button-press-event", G_CALLBACK(on_note_list_button_press), nullptr);
    gtk_widget_add_events(G.noteList, GDK_BUTTON_PRESS_MASK);

    GtkWidget* noteScroll = gtk_scrolled_window_new(nullptr,nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(noteScroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(noteScroll),G.noteList);
    gtk_box_pack_start(GTK_BOX(sidebar),noteScroll,TRUE,TRUE,2);

    GtkWidget* newBtn = gtk_button_new_with_label("+ 新筆記");
    g_signal_connect(newBtn,"clicked",G_CALLBACK(on_newnote),nullptr);
    gtk_box_pack_start(GTK_BOX(sidebar),newBtn,FALSE,FALSE,2);

    // ── Page thumbnails (bottom of sidebar) ──
    GtkWidget* thumbLabel = gtk_label_new("頁面縮圖");
    gtk_widget_set_halign(thumbLabel, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(thumbLabel), GTK_STYLE_CLASS_DIM_LABEL);
    gtk_box_pack_start(GTK_BOX(sidebar), thumbLabel, FALSE, FALSE, 4);

    G.pageThumbs = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(G.pageThumbs), GTK_SELECTION_NONE);
    g_signal_connect(G.pageThumbs, "row-activated", G_CALLBACK(on_thumb_activated), nullptr);
    gtk_widget_add_events(G.pageThumbs, GDK_BUTTON_PRESS_MASK);

    GtkWidget* thumbScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(thumbScroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(thumbScroll, 180, 180);
    gtk_container_add(GTK_CONTAINER(thumbScroll), G.pageThumbs);
    gtk_box_pack_start(GTK_BOX(sidebar), thumbScroll, FALSE, FALSE, 2);

    // Search box
    GtkWidget* searchBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    G.searchEntry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(G.searchEntry), "搜尋筆記...");
    g_signal_connect(G.searchEntry, "search-changed", G_CALLBACK(on_search_changed), nullptr);
    gtk_box_pack_start(GTK_BOX(searchBox), G.searchEntry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(sidebar), searchBox, FALSE, FALSE, 4);

    // Properties panel (for selected text/image)
    G.propPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(G.propPanel), 4);
    gtk_widget_set_visible(G.propPanel, FALSE);

    G.propLabel = gtk_label_new("");
    gtk_widget_set_halign(G.propLabel, GTK_ALIGN_START);
    GtkStyleContext* psc = gtk_widget_get_style_context(G.propLabel);
    gtk_style_context_add_class(psc, GTK_STYLE_CLASS_DIM_LABEL);
    gtk_box_pack_start(GTK_BOX(G.propPanel), G.propLabel, FALSE, FALSE, 0);

    // Font size
    G.propFontSize = gtk_spin_button_new_with_range(8, 120, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.propFontSize), 14);
    gtk_widget_set_tooltip_text(G.propFontSize, "字型大小");
    g_signal_connect(G.propFontSize, "value-changed", G_CALLBACK(on_prop_font_changed), nullptr);
    GtkWidget* fsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(fsBox), gtk_label_new("大小"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fsBox), G.propFontSize, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(G.propPanel), fsBox, FALSE, FALSE, 0);

    // Text color
    G.propColorBtn = gtk_color_button_new();
    GdkRGBA defColor = {0, 0, 0, 1};
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(G.propColorBtn), &defColor);
    gtk_widget_set_tooltip_text(G.propColorBtn, "文字顏色");
    g_signal_connect(G.propColorBtn, "color-set", G_CALLBACK(on_prop_color_changed), nullptr);
    GtkWidget* clrBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(clrBox), gtk_label_new("顏色"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clrBox), G.propColorBtn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(G.propPanel), clrBox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(sidebar), G.propPanel, FALSE, FALSE, 0);

    // ── Bottom bar ──
    GtkWidget* bottomBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_pack_start(GTK_BOX(mainBox), bottomBox, FALSE, FALSE, 0);

    GtkWidget* btn=gtk_button_new_with_label("◀");g_signal_connect(btn,"clicked",G_CALLBACK(on_page_prev),nullptr);gtk_box_pack_start(GTK_BOX(bottomBox),btn,FALSE,FALSE,0);
    G.lblPage=gtk_label_new("1/1");gtk_box_pack_start(GTK_BOX(bottomBox),G.lblPage,FALSE,FALSE,4);
    btn=gtk_button_new_with_label("▶");g_signal_connect(btn,"clicked",G_CALLBACK(on_page_next),nullptr);gtk_box_pack_start(GTK_BOX(bottomBox),btn,FALSE,FALSE,0);
    btn=gtk_button_new_with_label("+ 頁");g_signal_connect(btn,"clicked",G_CALLBACK(on_page_add),nullptr);gtk_box_pack_start(GTK_BOX(bottomBox),btn,FALSE,FALSE,0);
    btn=gtk_button_new_with_label("清");g_signal_connect(btn,"clicked",G_CALLBACK(on_page_clear),nullptr);gtk_box_pack_start(GTK_BOX(bottomBox),btn,FALSE,FALSE,0);
    btn=gtk_button_new_with_label("x 頁");g_signal_connect(btn,"clicked",G_CALLBACK(on_page_delete),nullptr);gtk_box_pack_start(GTK_BOX(bottomBox),btn,FALSE,FALSE,0);
    btn=gtk_button_new_with_label("直/橫");g_signal_connect(btn,"clicked",G_CALLBACK(on_page_orientation),nullptr);gtk_box_pack_start(GTK_BOX(bottomBox),btn,FALSE,FALSE,0);

    G.lblStatus=gtk_label_new("準備中");
    gtk_widget_set_tooltip_text(G.lblStatus,
        "🔧 操作指南:\n"
        "• Ctrl+滾輪: 放大/縮小\n"
        "• Shift+滾輪: 水平平移\n"
        "• 滾輪: 垂直平移\n"
        "• ☞ 選取: 點擊拖曳筆劃/文字/圖片/背景\n"
        "• ✎ 重命名筆記, ✖ 刪除筆記\n"
        "• Ctrl+V: 貼上圖片或文字");

    G.lblZoom=gtk_label_new("100%");
    gtk_widget_set_tooltip_text(G.lblZoom,
        "🔍 縮放:\n"
        "• Ctrl+滾輪: 放大/縮小\n"
        "• 滾輪: 上下平移\n"
        "• Shift+滾輪: 左右平移");

    gtk_box_pack_end(GTK_BOX(bottomBox),G.lblZoom,FALSE,FALSE,8);
    gtk_box_pack_end(GTK_BOX(bottomBox),G.lblStatus,FALSE,FALSE,8);

    std::string saveDir = get_save_dir();
    std::wstring wSaveDir = utf8_to_wide(saveDir);
    const fs::path saveDirPath = utf8_to_path(saveDir);
    std::error_code saveDirEc;
    fs::create_directories(saveDirPath, saveDirEc);

    G.crashRecoveryFile = CrashRecovery::snapshotPath(saveDirPath).u8string();
    G.crashRecoveryMarkerFile = CrashRecovery::sessionMarkerPath(saveDirPath).u8string();

    const bool hadRecoverySnapshot = fs::exists(crash_recovery_snapshot_path());
    const bool hadSessionMarker = fs::exists(crash_recovery_marker_path());
    const bool shouldRestoreRecovery = CrashRecovery::shouldRestoreSnapshot(hadRecoverySnapshot, hadSessionMarker);

    if (CrashRecovery::shouldDeleteStaleSnapshot(hadRecoverySnapshot, hadSessionMarker)) {
        remove_file_if_exists(crash_recovery_snapshot_path(), "stale crash recovery snapshot");
    }
    if (hadSessionMarker) {
        remove_file_if_exists(crash_recovery_marker_path(), "previous crash recovery session marker");
    }
    write_session_marker();

    // ── Load existing notes ──
    if (fs::exists(wSaveDir)) {
        for (auto& entry : fs::directory_iterator(wSaveDir)) {
            if (entry.path().extension() == ".onote" &&
                !CrashRecovery::isSnapshotFile(entry.path())) {
                // Use u8string() for proper UTF-8 encoding
                on_load_note(entry.path().u8string());
            }
        }
    }

    bool restoredRecoveryNote = false;
    if (shouldRestoreRecovery && fs::exists(crash_recovery_snapshot_path())) {
        const size_t noteCountBefore = G.notes.size();
        on_load_note(G.crashRecoveryFile);
        if (G.notes.size() > noteCountBefore) {
            NoteData& recovered = G.notes.back();
            recovered.name = CrashRecovery::recoveredNoteName(recovered.name);
            recovered.dirty = 1;
            G.selNote = (int)G.notes.size() - 1;
            G.selPage = 0;
            restoredRecoveryNote = true;
            rebuildNoteList();
            renderCanvas();
            updateStatus();
            rebuildThumbs();

            GtkWidget* info = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
                GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                "Recovered unsaved changes from the previous session.");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(info),
                "A crash recovery copy was restored as a separate note so it does not silently disappear.");
            gtk_dialog_run(GTK_DIALOG(info));
            gtk_widget_destroy(info);
            logInfo("Recovered note restored from crash recovery snapshot");
        } else {
            logInfo("Crash recovery snapshot was present but could not be restored");
        }
    }

    // ── Default note if none loaded ──
    if (G.notes.empty()) {
        G.notes.push_back(NoteData());
        G.notes[0].name = "筆記 1";
        G.notes[0].pages.push_back(PageData());
        G.selNote = 0; G.selPage = 0;
    } else if (!restoredRecoveryNote) {
        G.selNote = 0; G.selPage = 0;
    }

    rebuildNoteList();
    renderCanvas();
    updateStatus();
    state_ = this;

    // ── Auto-save timer (every 30 seconds) ──
    G.autoSaveTimer = g_timeout_add_seconds(30, auto_save_callback, nullptr);

    // ── Keyboard shortcut hint in status bar ──
    gtk_widget_set_tooltip_text(G.lblStatus,
        "⌨ 快捷鍵:\n"
        "• Ctrl+V: 貼上剪貼簿圖片或文字\n"
        "• Ctrl+Z: 復原上一筆劃\n"
        "• Ctrl+S: 儲存筆記\n"
        "• Ctrl+滾輪: 放大/縮小\n"
        "• 滾輪: 垂直平移\n"
        "• Shift+滾輪: 水平平移\n\n"
        "📋 Ctrl+V 貼上教學:\n"
        "• 在瀏覽器/圖片檢視器複製圖片 → Ctrl+V\n"
        "• Windows 截圖工具 (Win+Shift+S) → Ctrl+V\n"
        "• 複製文字 → Ctrl+V 貼上為文字元素");

    // ── Shutdown handler - auto-save all dirty notes ──
    g_signal_connect(G.window, "delete-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer) -> gboolean {
        G.shuttingDown = true;
        // Stop auto-save timer
        if (G.autoSaveTimer) {
            g_source_remove(G.autoSaveTimer);
            G.autoSaveTimer = 0;
        }
        persist_notes_for_shutdown();
        // Return FALSE to let the default handler destroy the window and quit the app
        // Returning TRUE would inhibit the default handler, causing the app to hang
        return FALSE;
    }), nullptr);
}

MainWindow::~MainWindow() {
    // Stop auto-save timer if still running
    if (G.autoSaveTimer) {
        g_source_remove(G.autoSaveTimer);
        G.autoSaveTimer = 0;
    }
    if(G.canvasSurf) cairo_surface_destroy(G.canvasSurf);
    // Clean up thumbnail surfaces
    for (auto* s : G.pageThumbSurf) { if (s) cairo_surface_destroy(s); }
    G.pageThumbSurf.clear();
    G.notes.clear();
}

void MainWindow::show() { gtk_widget_show_all(GTK_WIDGET(window_)); }

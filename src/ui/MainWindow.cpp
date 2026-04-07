// src/ui/MainWindow.cpp - Complete working implementation
// SPDX-License-Identifier: GPL-2.0-or-later

#include "MainWindow.h"
#include "../application/AppController.h"
#include "../application/PathManager.h"
#include "../util/Logger.h"
#include "../util/FileUtils.h"

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
    GtkWidget* textEntry = nullptr;
    GtkWidget* textSizeSpin = nullptr;
    GtkWidget* textOverlayBox = nullptr; // Box holding entry+spin
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
    double dragOffX = 0, dragOffY = 0, selResizeW = 0, selResizeH = 0;
    double selResizeOrigW = 0, selResizeOrigH = 0; // For bg resize
    double imgRotateAngle = 0;  // 圖片旋轉角度（0, 90, 180, 270）
    double pageScrollY = 0; // Vertical scroll offset when zoomed
    double pageScrollX = 0; // Horizontal scroll offset when zoomed

    // Auto-save
    guint autoSaveTimer = 0;
    bool shuttingDown = false;

    // Undo/Redo stacks
    std::vector<std::string> undoStack;
    std::vector<std::string> redoStack;

    // Crash recovery
    std::string crashRecoveryFile;

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
static std::string serializeNote(const NoteData* nd);
static void deserializeNoteToCurrent(const std::string& data);
static void on_undo(GtkButton*, gpointer);
static void on_redo(GtkButton*, gpointer);
static void pushUndo();
static FILE* wfopen_utf8(const std::string& path_utf8, const wchar_t* mode);
static void hideTextEntry();
static void on_note_rename(GtkMenuItem*, gpointer);
static void on_note_delete(GtkMenuItem*, gpointer);
static void on_save(GtkButton*, gpointer);
static std::string get_save_dir();
static std::string get_exe_dir();
static std::string note_filename(NoteData* nd);
static bool save_note_to_file(NoteData* nd);

// Path validation - prevent path traversal attacks
static bool is_safe_path(const std::string& path) {
    // Block absolute paths and path traversal
    if (path.empty() || path[0] == '/' || path[0] == '\\') return false;
    if (path.find("..") != std::string::npos) return false;
    if (path.find(':') != std::string::npos) return false; // Block Windows drive letters
    return true;
}

// Sanitize external file paths from .onote files
static std::string sanitize_resource_path(const std::string& path) {
    // If absolute path, extract just the filename
    size_t pos = path.find_last_of("/\\");
    std::string filename = (pos != std::string::npos) ? path.substr(pos + 1) : path;
    // Remove dangerous characters
    std::string safe;
    for (char c : filename) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') continue;
        safe += c;
    }
    return safe;
}

// ============================================================
// Helpers
// ============================================================
static NoteData* curNote() {
    return (G.selNote >= 0 && G.selNote < (int)G.notes.size()) ? &G.notes[G.selNote] : nullptr;
}

static std::string serializeCurrentNote() { return serializeNote(curNote()); }

static void pushUndo() {
    std::string snap = serializeCurrentNote();
    if (!snap.empty()) {
        G.undoStack.push_back(snap);
        if (G.undoStack.size() > 50) G.undoStack.erase(G.undoStack.begin());
        G.redoStack.clear();
    }
}
static PageData* curPage() {
    NoteData* n = curNote();
    return (n && G.selPage >= 0 && G.selPage < (int)n->pages.size()) ? &n->pages[G.selPage] : nullptr;
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
    snprintf(buf, sizeof(buf), "%s %s %d/%d 筆:%d 文:%d 圖:%d | %s",
             nn, ori, G.selPage+1, npg, nst, ntx, nim, TOOL_NAMES[G.tool]);
    gtk_label_set_text(GTK_LABEL(G.lblStatus), buf);
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

// Auto-save callback (called every 30 seconds)
static gboolean auto_save_callback(gpointer) {
    if (G.shuttingDown) return FALSE;
    int saved = 0;
    for (auto& n : G.notes) {
        if (n.dirty) {
            save_note_to_file(&n);
            saved++;
        }
    }
    if (saved > 0) {
        logInfo("Auto-saved %d notes", saved);
        // Also save crash recovery snapshot (current note)
        NoteData* cur = curNote();
        if (cur && !G.crashRecoveryFile.empty()) {
            std::string snap = serializeNote(cur);
            if (!snap.empty()) {
                FILE* f = wfopen_utf8(G.crashRecoveryFile, L"wb");
                if (f) {
                    fprintf(f, "%s", snap.c_str());
                    fclose(f);
                }
            }
        }
    }
    return TRUE; // keep running
}

// Update properties panel based on current selection
static void updatePropPanel() {
    if (!G.propPanel) return;
    PageData* pg = curPage();
    if (!pg) { gtk_widget_hide(G.propPanel); return; }

    if (G.selTxt >= 0 && G.selTxt < (int)pg->texts.size()) {
        // Text selected - show text properties
        TxtEl* t = &pg->texts[G.selTxt];
        gtk_label_set_text(GTK_LABEL(G.propLabel), "文字屬性");
        if (G.propFontSize) gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.propFontSize), t->fontSize);
        if (G.propColorBtn) {
            GdkRGBA c = {t->r, t->g, t->b, 1.0};
            gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(G.propColorBtn), &c);
        }
        gtk_widget_show_all(G.propPanel);
    } else if (G.selImg >= 0 && G.selImg < (int)pg->images.size()) {
        // Image selected
        ImgEl* img = &pg->images[G.selImg];
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
    if (G.selTxt < 0) return;
    PageData* pg = curPage();
    if (!pg || G.selTxt >= (int)pg->texts.size()) return;
    pg->texts[G.selTxt].fontSize = gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.propFontSize));
    NoteData* n = curNote(); if(n) n->dirty = 1;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas();
}

static void on_prop_color_changed(GtkWidget* btn, gpointer) {
    if (G.selTxt < 0) return;
    PageData* pg = curPage();
    if (!pg || G.selTxt >= (int)pg->texts.size()) return;
    GdkRGBA c;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &c);
    pg->texts[G.selTxt].r = c.red;
    pg->texts[G.selTxt].g = c.green;
    pg->texts[G.selTxt].b = c.blue;
    NoteData* n = curNote(); if(n) n->dirty = 1;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas();
}

// ============================================================
// Text entry using GtkOverlay - simple and robust
// ============================================================
static void hideTextEntry() {
    if (!G.textOverlayBox) return;
    if (!gtk_widget_get_visible(G.textOverlayBox)) return;

    // Save data before hiding
    const char* txt = G.textEntry ? gtk_entry_get_text(GTK_ENTRY(G.textEntry)) : "";
    PageData* pg = curPage();
    if (pg && txt && strlen(txt) > 0) {
        int fs = 14;
        if (G.textSizeSpin) fs = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(G.textSizeSpin));

        double px = (G.textEntryX - G.margins[0]) / G.zoom;
        double py = (G.textEntryY - G.margins[1]) / G.zoom;

        pg->texts.push_back(TxtEl());
        TxtEl* t = &pg->texts.back();
        t->text = txt;
        t->x = fmax(0.0, px);
        t->y = fmax(0.0, py);
        t->fontSize = fs;
        t->r = G.penR; t->g = G.penG; t->b = G.penB;
        NoteData* nd = curNote(); if(nd) nd->dirty = 1;
    }

    // Clear and hide
    if (G.textEntry) gtk_entry_set_text(GTK_ENTRY(G.textEntry), "");
    gtk_widget_hide(G.textOverlayBox);

    // Force full canvas redraw
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas();
}

static void ensureTextOverlay() {
    if (G.textOverlayBox) return;

    G.textOverlayBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_valign(G.textOverlayBox, GTK_ALIGN_START);
    gtk_widget_set_halign(G.textOverlayBox, GTK_ALIGN_START);
    gtk_widget_set_visible(G.textOverlayBox, FALSE);

    G.textEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(G.textEntry), "輸入文字 (Enter 完成)");
    gtk_widget_set_size_request(G.textEntry, 200, -1);
    gtk_widget_set_tooltip_text(G.textEntry,
        "文字工具操作指南:\n"
        "• 輸入文字後按 Enter 完成\n"
        "• 選取工具可拖曳已輸入的文字\n"
        "• 多行文字：請分多次輸入");
    gtk_box_pack_start(GTK_BOX(G.textOverlayBox), G.textEntry, TRUE, TRUE, 0);

    G.textSizeSpin = gtk_spin_button_new_with_range(8, 72, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(G.textSizeSpin), 14);
    gtk_widget_set_size_request(G.textSizeSpin, 50, -1);
    gtk_box_pack_start(GTK_BOX(G.textOverlayBox), G.textSizeSpin, FALSE, FALSE, 0);

    gtk_overlay_add_overlay(GTK_OVERLAY(G.overlay), G.textOverlayBox);

    // Connect signals ONCE
    g_signal_connect(G.textEntry, "activate", G_CALLBACK(+[](GtkEntry*, gpointer) {
        hideTextEntry();
    }), nullptr);
    g_signal_connect(G.textEntry, "focus-out-event", G_CALLBACK(+[](GtkWidget*, GdkEventFocus*, gpointer) -> gboolean {
        hideTextEntry();
        return FALSE;
    }), nullptr);
}

static void showTextEntry(double sx, double sy) {
    hideTextEntry();
    if (!G.drawingArea || !G.overlay) return;

    G.textEntryX = sx;
    G.textEntryY = sy;

    ensureTextOverlay();

    // Position using margin
    gtk_widget_set_margin_start(G.textOverlayBox, (int)sx);
    gtk_widget_set_margin_top(G.textOverlayBox, (int)sy);
    gtk_widget_show_all(G.textOverlayBox);
    gtk_widget_grab_focus(G.textEntry);
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
        double sc = fmin((double)pw/pg->bgW, (double)ph/pg->bgH);
        double bw=pg->bgW*sc, bh=pg->bgH*sc;
        double bx=leftMargin+(pw-bw)/2, by=topMargin+(ph-bh)/2;
        cairo_set_source_surface(cr, pg->bgSurf, bx, by); cairo_paint(cr);

        // Selection indicator for background
        if (G.selBg) {
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.7);
            cairo_set_line_width(cr, 2);
            cairo_set_dash(cr, (double[]){6, 3}, 2, 0);
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

        if ((int)i == G.selImg) {
            // Selection border - dashed, more visible
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.8);
            cairo_set_line_width(cr, 3);
            cairo_set_dash(cr, (double[]){6, 3}, 2, 0);
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

        if ((int)i == G.selTxt) {
            // Dashed border for selected text
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.7);
            cairo_set_line_width(cr, 2);
            cairo_set_dash(cr, (double[]){4, 3}, 2, 0);
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

    // Highlight selected stroke with glow effect
    if (G.selStroke >= 0 && G.selStroke < (int)pg->strokes.size()) {
        StrokeData* s = &pg->strokes[G.selStroke];
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
            cairo_set_dash(cr, (double[]){4, 3}, 2, 0);
            cairo_rectangle(cr,
                leftMargin+minX*G.zoom-4, topMargin+minY*G.zoom-4,
                (maxX-minX)*G.zoom+8, (maxY-minY)*G.zoom+8);
            cairo_stroke(cr);
            cairo_set_dash(cr, nullptr, 0, 0);
        }
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

        // 1. Check strokes first (highest priority for content)
        double strokeHitDist = 15.0 / G.zoom;
        for (int i = (int)pg->strokes.size()-1; i >= 0; i--) {
            StrokeData* s = &pg->strokes[i];
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
        for (int i = (int)pg->texts.size()-1; i >= 0; i--) {
            TxtEl* t = &pg->texts[i];
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
                G.selTxt = i;
                G.dragging = 1;
                G.dragOffX = t->x - px;  // offset from click to text origin
                G.dragOffY = t->y - py;
                renderCanvas(); updateStatus(); updatePropPanel();
                return TRUE;
            }
        }

        // 3. Check images (lowest priority)
        for (int i = (int)pg->images.size()-1; i >= 0; i--) {
            ImgEl* img = &pg->images[i];
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
            double bw = pg->bgW, bh = pg->bgH;
            double sc = fmin(pg->pw/bw, pg->ph/bh);
            double dispW = bw * sc, dispH = bh * sc;
            double bx = (pg->pw - dispW) / 2, by = (pg->ph - dispH) / 2;

            if (px >= bx && px <= bx+dispW && py >= by && py <= by+dispH) {
                G.selBg = 1;
                // Check resize handle (bottom-right corner, 20px)
                if (px >= bx+dispW-20/G.zoom && px <= bx+dispW && py >= by+dispH-20/G.zoom && py <= by+dispH) {
                    G.resizing = 1;
                    G.dragOffX = px; G.dragOffY = py;
                    G.selResizeW = dispW; G.selResizeH = dispH;
                    G.selResizeOrigW = bw; G.selResizeOrigH = bh;
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
    G.drawing = 0;
    G.dragging = 0;
    G.resizing = 0;
    // Save state after any mouse interaction completes
    pushUndo();
    NoteData* n=curNote();if(n)n->dirty=1;
    return TRUE;
}

static gboolean on_motion(GtkWidget*, GdkEventMotion* ev, gpointer) {
    PageData* pg = curPage(); if (!pg) return TRUE;

    // Account for scroll offsets
    double px = (ev->x - G.margins[0] - G.pageScrollX) / G.zoom;
    double py = (ev->y - G.margins[1] - G.pageScrollY) / G.zoom;

    if (G.dragging) {
        if (G.selStroke >= 0 && G.selStroke < (int)pg->strokes.size()) {
            StrokeData* s = &pg->strokes[G.selStroke];
            double dx = px - G.dragOffX, dy = py - G.dragOffY;
            for (size_t j = 0; j < s->x.size(); j++) { s->x[j] += dx; s->y[j] += dy; }
            G.dragOffX = px; G.dragOffY = py;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            renderCanvas();
            return TRUE;
        }
        if (G.selImg >= 0 && G.selImg < (int)pg->images.size()) {
            pg->images[G.selImg].x = px - G.dragOffX;
            pg->images[G.selImg].y = py - G.dragOffY;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            renderCanvas();
            return TRUE;
        }
        if (G.selTxt >= 0 && G.selTxt < (int)pg->texts.size()) {
            pg->texts[G.selTxt].x = px + G.dragOffX;
            pg->texts[G.selTxt].y = py + G.dragOffY;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            renderCanvas();
            return TRUE;
        }
        return TRUE;
    }

    if (G.resizing && G.selBg && pg->bgSurf && G.selResizeOrigW > 0) {
        // Resize background - scale based on horizontal drag
        double dx = px - G.dragOffX;
        double scale = 1.0 + dx / fmax(1, G.selResizeW);
        pg->bgW = G.selResizeOrigW * scale;
        pg->bgH = G.selResizeOrigH * scale;
        if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
        renderCanvas();
        return TRUE;
    }

    if (G.resizing && G.selImg >= 0 && G.selImg < (int)pg->images.size()) {
        ImgEl* img = &pg->images[G.selImg];
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
        G.zoom = fmax(0.1, fmin(5.0, G.zoom * f));
        G.pageScrollY = 0; G.pageScrollX = 0;
        if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
        renderCanvas(); updateStatus();
        return TRUE;
    }

    // Shift+scroll = horizontal pan
    bool shiftHeld = (ev->state & GDK_SHIFT_MASK) != 0;
    if (shiftHeld && G.zoom > 0.5) {
        double scrollAmt = 30.0;
        if (ev->direction == GDK_SCROLL_LEFT || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_x < 0)) {
            G.pageScrollX = fmin(0.0, G.pageScrollX + scrollAmt);
        } else if (ev->direction == GDK_SCROLL_RIGHT || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_x > 0)) {
            double maxScroll = -(pg->pw * G.zoom - 800);
            if (maxScroll < 0) G.pageScrollX = fmax(maxScroll, G.pageScrollX - scrollAmt);
        } else if (ev->direction == GDK_SCROLL_UP || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y < 0)) {
            G.pageScrollX = fmin(0.0, G.pageScrollX + scrollAmt);
        } else if (ev->direction == GDK_SCROLL_DOWN || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y > 0)) {
            double maxScroll = -(pg->pw * G.zoom - 800);
            if (maxScroll < 0) G.pageScrollX = fmax(maxScroll, G.pageScrollX - scrollAmt);
        }
        if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
        renderCanvas();
        return TRUE;
    }

    // Normal scroll = vertical pan when zoomed
    if (G.zoom > 0.5) {
        double scrollAmt = 30.0;
        if (ev->direction == GDK_SCROLL_UP || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y < 0)) {
            G.pageScrollY = fmin(0.0, G.pageScrollY + scrollAmt);
        } else if (ev->direction == GDK_SCROLL_DOWN || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y > 0)) {
            double maxScroll = -(pg->ph * G.zoom - 600);
            if (maxScroll < 0) G.pageScrollY = fmax(maxScroll, G.pageScrollY - scrollAmt);
        } else if (ev->direction == GDK_SCROLL_LEFT || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_x < 0)) {
            G.pageScrollX = fmin(0.0, G.pageScrollX + scrollAmt);
        } else if (ev->direction == GDK_SCROLL_RIGHT || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_x > 0)) {
            double maxScroll = -(pg->pw * G.zoom - 800);
            if (maxScroll < 0) G.pageScrollX = fmax(maxScroll, G.pageScrollX - scrollAmt);
        }
        if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
        renderCanvas();
        return TRUE;
    }

    // Not zoomed - default zoom behavior
    double f = 1.0;
    if (ev->direction == GDK_SCROLL_UP || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y < 0)) f = 1.1;
    else if (ev->direction == GDK_SCROLL_DOWN || (ev->direction == GDK_SCROLL_SMOOTH && ev->delta_y > 0)) f = 0.9;
    G.zoom = fmax(0.1, fmin(5.0, G.zoom * f));
    G.pageScrollY = 0; G.pageScrollX = 0;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    renderCanvas(); updateStatus();
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
                    if (gdk_pixbuf_loader_write(loader, data, len, &err)) {
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
        if (pg && G.selImg >= 0 && G.selImg < (int)pg->images.size()) {
            pushUndo();
            ImgEl* img = &pg->images[G.selImg];
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
        if (pg && G.selImg >= 0 && G.selImg < (int)pg->images.size()) {
            pushUndo();
            ImgEl* img = &pg->images[G.selImg];
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

    // Delete
    if (ev->keyval == GDK_KEY_Delete || ev->keyval == GDK_KEY_BackSpace) {
        if (G.selImg >= 0 && G.selImg < (int)pg->images.size()) {
            pg->images.erase(pg->images.begin()+G.selImg);
            G.selImg=-1;
            NoteData* n=curNote(); if(n)n->dirty=1;
            renderCanvas(); updateStatus();
            return TRUE;
        }
        if (G.selTxt >= 0 && G.selTxt < (int)pg->texts.size()) {
            pg->texts.erase(pg->texts.begin()+G.selTxt);
            G.selTxt=-1;
            NoteData* n=curNote(); if(n)n->dirty=1;
            renderCanvas(); updateStatus();
            return TRUE;
        }
        if (!pg->strokes.empty()) {
            pg->strokes.pop_back();
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
static void on_tool_select(GtkButton*, gpointer) { hideTextEntry(); G.tool=4; G.selImg=-1; G.selTxt=-1; updateStatus(); updatePropPanel(); }

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
    G.zoom=fmin(5.0, G.zoom*1.25);
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    renderCanvas(); updateStatus();
}
static void on_zoomout(GtkButton*, gpointer) {
    G.zoom=fmax(0.1, G.zoom/1.25);
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    renderCanvas(); updateStatus();
}
static void on_zoomfit(GtkButton*, gpointer) {
    PageData* pg=curPage(); if(!pg||!G.drawingArea) return;
    int aw=gtk_widget_get_allocated_width(G.drawingArea);
    int ah=gtk_widget_get_allocated_height(G.drawingArea);
    G.zoom=fmin((double)(aw-G.margins[0]-G.margins[2]-10)/pg->pw, (double)(ah-G.margins[1]-G.margins[3]-10)/pg->ph);
    G.zoom=fmax(0.1,fmin(5.0,G.zoom));
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    renderCanvas(); updateStatus();
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
    if(G.selImg>=0 && G.selImg<(int)pg->images.size()){
        pushUndo();
        pg->images.erase(pg->images.begin()+G.selImg);G.selImg=-1;
        NoteData* n=curNote();if(n)n->dirty=1;renderCanvas();updateStatus();rebuildThumbs();
    }
    else if(G.selTxt>=0 && G.selTxt<(int)pg->texts.size()){
        pushUndo();
        pg->texts.erase(pg->texts.begin()+G.selTxt);G.selTxt=-1;
        NoteData* n=curNote();if(n)n->dirty=1;renderCanvas();updateStatus();rebuildThumbs();
    }
    else if(!pg->strokes.empty()){
        pushUndo();
        pg->strokes.pop_back();
        NoteData* n=curNote();if(n)n->dirty=1;renderCanvas();updateStatus();rebuildThumbs();
    }
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
    gtk_container_foreach(GTK_CONTAINER(G.noteList), (GtkCallback)gtk_widget_destroy, nullptr);
    for (size_t i = 0; i < G.notes.size(); i++) {
        // 搜尋過濾
        if (!G.searchTerm.empty()) {
            std::string nameLower = G.notes[i].name;
            std::string searchLower = G.searchTerm;
            // 轉小寫比較
            for (auto& c : nameLower) c = tolower(c);
            for (auto& c : searchLower) c = tolower(c);
            bool match = nameLower.find(searchLower) != std::string::npos;
            // 也搜尋內容
            if (!match) {
                for (auto& pg : G.notes[i].pages) {
                    for (auto& t : pg.texts) {
                        std::string txtLower = t.text;
                        for (auto& c : txtLower) c = tolower(c);
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
        g_signal_connect(renBtn, "clicked", G_CALLBACK(on_row_rename_clicked), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(hbox), renBtn, FALSE, FALSE, 0);

        // Delete button
        GtkWidget* delBtn = gtk_button_new_with_label("\xe2\x9c\x96");
        gtk_widget_set_tooltip_text(delBtn, "\xe5\x88\xaa\xe9\x99\xa4");
        gtk_widget_set_size_request(delBtn, 24, 24);
        GtkStyleContext* dsc = gtk_widget_get_style_context(delBtn);
        gtk_style_context_add_class(dsc, "destructive-action");
        g_signal_connect(delBtn, "clicked", G_CALLBACK(on_row_delete_clicked), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(hbox), delBtn, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(row), hbox);
        gtk_widget_show_all(row);
        if ((int)i == G.selNote) {
            GtkStyleContext* rsc = gtk_widget_get_style_context(row);
            gtk_style_context_add_class(rsc, GTK_STYLE_CLASS_SUGGESTED_ACTION);
        }
        g_object_set_data(G_OBJECT(row), "idx", GINT_TO_POINTER(i));
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
    gtk_container_foreach(GTK_CONTAINER(G.pageThumbs), (GtkCallback)gtk_widget_destroy, nullptr);

    // 清理舊縮圖 surface
    for (auto* s : G.pageThumbSurf) { if (s) cairo_surface_destroy(s); }
    G.pageThumbSurf.clear();

    NoteData* n = curNote();
    if (!n) return;

    int thumbW = 130;
    for (size_t i = 0; i < n->pages.size(); i++) {
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
            (int)i == G.selPage ? " ◀" : "");
        gtk_label_set_text(GTK_LABEL(lbl), pageLbl);
        gtk_widget_set_halign(lbl, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_widget_show_all(row);

        if ((int)i == G.selPage) {
            GtkStyleContext* rsc = gtk_widget_get_style_context(row);
            gtk_style_context_add_class(rsc, GTK_STYLE_CLASS_SUGGESTED_ACTION);
        }
        g_object_set_data(G_OBJECT(row), "idx", GINT_TO_POINTER(i));
        gtk_list_box_insert(GTK_LIST_BOX(G.pageThumbs), row, -1);
    }
}

static void on_note_activated(GtkListBox*, GtkListBoxRow* row, gpointer) {
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "idx"));
    if (idx < 0 || idx >= (int)G.notes.size()) return;
    hideTextEntry();
    if (G.selNote >= 0 && G.selNote < (int)G.notes.size() && G.notes[G.selNote].dirty) {
        logInfo("Auto-save: %s", G.notes[G.selNote].name.c_str());
        G.notes[G.selNote].dirty = 0;
    }
    G.selNote = idx; G.selPage = 0;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    G.selImg = -1; G.selTxt = -1;
    rebuildNoteList(); renderCanvas(); updateStatus();
}

// Note context menu
static void on_note_rename(GtkMenuItem*, gpointer user_data) {
    int idx = GPOINTER_TO_INT(user_data);
    if (idx < 0 || idx >= (int)G.notes.size()) return;
    NoteData* nd = &G.notes[idx];

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
    if (idx < 0 || idx >= (int)G.notes.size()) return;
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
        G.notes[idx].name.c_str());
    gtk_dialog_add_buttons(GTK_DIALOG(dlg),
        "取消", GTK_RESPONSE_CANCEL,
        "刪除", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_CANCEL);

    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (resp == GTK_RESPONSE_ACCEPT) {
        // Delete the .onote file from disk as well
        std::string fn = note_filename(&G.notes[idx]);
        std::error_code ec;
        fs::remove(fn, ec);
        if (ec) {
            logInfo("刪除檔案失敗: %s: %s", fn.c_str(), ec.message().c_str());
        }

        G.notes.erase(G.notes.begin() + idx);
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
    if (idx < 0 || idx >= (int)G.notes.size()) return FALSE;

    NoteData* nd = &G.notes[idx];

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
    std::string dir = exeDir + "/data/notes";

    // Ensure directory exists
    std::error_code ec;
    fs::create_directories(fs::path(dir), ec);
    if (ec) {
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
static std::string wstring_to_utf8(const std::wstring& wstr) {
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], len, NULL, NULL);
    return result;
}

// Helper: Convert UTF-8 string to wide string (for Windows file paths)
static std::wstring utf8_to_wide(const std::string& utf8) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], len);
    if (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

// Helper: Open file with wide string path, returns FILE* or nullptr
static FILE* wfopen_utf8(const std::string& path_utf8, const wchar_t* mode) {
    std::wstring wpath = utf8_to_wide(path_utf8);
    return _wfopen(wpath.c_str(), mode);
}

// Helper: Get exe directory using Windows API (most reliable)
static std::string get_exe_dir() {
    wchar_t wPath[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, wPath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::wstring ws(wPath);
        size_t pos = ws.find_last_of(L"\\/");
        if (pos != std::string::npos) {
            std::wstring wDir = ws.substr(0, pos);
            return wstring_to_utf8(wDir);
        }
    }
    // Fallback
    return ".";
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
            std::string esc = t->text;
            for (auto& c : esc) { if (c == '\n') c = ' '; if (c == '|') c = '-'; }
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

        if (curPage >= 0 && curPage < (int)nd->pages.size()) {
            PageData* pg = &nd->pages[curPage];
            if (line.substr(0, 3) == "pw=") { pg->pw = atof(line.c_str()+3); continue; }
            if (line.substr(0, 3) == "ph=") { pg->ph = atof(line.c_str()+3); continue; }

            if (line.substr(0, 2) == "s ") {
                pg->strokes.push_back(StrokeData());
                curStroke = &pg->strokes.back();
                char* str = (char*)line.c_str();
                char* tok;
                tok = strstr((const char*)str, "w="); if(tok) curStroke->w = atof(tok+2);
                tok = strstr((const char*)str, "r="); if(tok) curStroke->r = atof(tok+2);
                tok = strstr((const char*)str, "g="); if(tok) curStroke->g = atof(tok+2);
                tok = strstr((const char*)str, "b="); if(tok) curStroke->b = atof(tok+2);
                tok = strstr((const char*)str, "a="); if(tok) curStroke->a = atof(tok+2);
                tok = strstr((const char*)str, "t="); if(tok) curStroke->tool = atoi(tok+2);
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
                char* tok;
                tok = strstr((char*)line.c_str(), "x="); if(tok) t->x = atof(tok+2);
                tok = strstr((char*)line.c_str(), "y="); if(tok) t->y = atof(tok+2);
                tok = strstr((char*)line.c_str(), "fs="); if(tok) t->fontSize = atof(tok+3);
                tok = strstr((char*)line.c_str(), "r="); if(tok) t->r = atof(tok+2);
                tok = strstr((char*)line.c_str(), "g="); if(tok) t->g = atof(tok+2);
                tok = strstr((char*)line.c_str(), "b="); if(tok) t->b = atof(tok+2);
                tok = strstr((char*)line.c_str(), "txt="); if(tok) t->text = tok+4;
                continue;
            }

            if (line.substr(0, 4) == "img ") {
                pg->images.push_back(ImgEl());
                ImgEl* img = &pg->images.back();
                char* tok;
                tok = strstr((char*)line.c_str(), "x="); if(tok) img->x = atof(tok+2);
                tok = strstr((char*)line.c_str(), "y="); if(tok) img->y = atof(tok+2);
                tok = strstr((char*)line.c_str(), "w="); if(tok) img->w = atof(tok+2);
                tok = strstr((char*)line.c_str(), "h="); if(tok) img->h = atof(tok+2);
                tok = strstr((char*)line.c_str(), "src="); if(tok) img->srcFile = tok+4;
                continue;
            }

            if (line.substr(0, 3) == "bg ") {
                char* tok = strstr((char*)line.c_str(), "src=");
                if (tok) {
                    pg->bgFile = tok + 4;
                    tok = strstr((char*)line.c_str(), "w="); if(tok) pg->bgW = atof(tok+2);
                    tok = strstr((char*)line.c_str(), "h="); if(tok) pg->bgH = atof(tok+2);
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
            std::string escaped = t->text;
            for (auto& c : escaped) { if (c == '\n') c = ' '; if (c == '|') c = '-'; }
            fprintf(f, "t %zu x=%g y=%g fs=%g r=%g g=%g b=%g txt=%s\n",
                    ti, t->x, t->y, t->fontSize, t->r, t->g, t->b, escaped.c_str());
        }

        // Save image sources
        for (size_t ii = 0; ii < pg->images.size(); ii++) {
            ImgEl* img = &pg->images[ii];
            if (!img->srcFile.empty()) {
                // Sanitize path - only store filename, not full path
                std::string safePath = sanitize_resource_path(img->srcFile);
                fprintf(f, "img %zu x=%g y=%g w=%g h=%g src=%s\n",
                        ii, img->x, img->y, img->w, img->h, safePath.c_str());
            }
        }

        // Save background source
        if (!pg->bgFile.empty()) {
            std::string safePath = sanitize_resource_path(pg->bgFile);
            fprintf(f, "bg src=%s w=%g h=%g\n", safePath.c_str(), pg->bgW, pg->bgH);
        }
    }

    fclose(f);
    nd->dirty = 0;
    return true;
}

static bool load_note_from_file(const std::string& fn_utf8, NoteData* nd) {
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

        if (curPage >= 0 && curPage < (int)nd->pages.size()) {
            PageData* pg = &nd->pages[curPage];

            if (sline.substr(0, 3) == "pw=") { pg->pw = atof(sline.c_str()+3); continue; }
            if (sline.substr(0, 3) == "ph=") { pg->ph = atof(sline.c_str()+3); continue; }

            if (sline.substr(0, 2) == "s ") {
                pg->strokes.push_back(StrokeData());
                curStroke = &pg->strokes.back();
                // Parse params
                char* str = (char*)sline.c_str();
                char* tok;
                tok = strstr((const char*)str, "w="); if(tok) curStroke->w = atof(tok+2);
                tok = strstr((const char*)str, "r="); if(tok) curStroke->r = atof(tok+2);
                tok = strstr((const char*)str, "g="); if(tok) curStroke->g = atof(tok+2);
                tok = strstr((const char*)str, "b="); if(tok) curStroke->b = atof(tok+2);
                tok = strstr((const char*)str, "a="); if(tok) curStroke->a = atof(tok+2);
                tok = strstr((const char*)str, "t="); if(tok) curStroke->tool = atoi(tok+2);
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
                tok = strstr((const char*)sline.c_str(), "txt="); if(tok) t->text = tok+4;
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
                tok = strstr((const char*)sline.c_str(), "src="); if(tok) img->srcFile = tok+4;

                // Resolve path relative to note directory and validate
                std::string imgPath = img->srcFile;
                if (!is_safe_path(imgPath)) {
                    // Path traversal detected - sanitize
                    imgPath = sanitize_resource_path(imgPath);
                    img->srcFile = imgPath;
                }
                // Try relative to note directory first
                std::string saveDir = get_save_dir();
                std::string fullPath = saveDir + "/" + imgPath;
                if (!fs::exists(fullPath)) {
                    // Try original path
                    fullPath = imgPath;
                }

                // Reload image if file exists
                if (!imgPath.empty() && fs::exists(fullPath)) {
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
                    pg->bgFile = tok + 4;
                    tok = strstr((const char*)sline.c_str(), "w="); if(tok) pg->bgW = atof(tok+2);
                    tok = strstr((const char*)sline.c_str(), "h="); if(tok) pg->bgH = atof(tok+2);

                    // Validate and resolve background path
                    std::string bgPath = pg->bgFile;
                    if (!is_safe_path(bgPath)) {
                        bgPath = sanitize_resource_path(bgPath);
                        pg->bgFile = bgPath;
                    }
                    std::string saveDir = get_save_dir();
                    std::string fullPath = saveDir + "/" + bgPath;
                    if (!fs::exists(fullPath)) {
                        fullPath = bgPath;
                    }

                    // Reload background
                    if (!bgPath.empty() && fs::exists(fullPath)) {
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
        char errMsg[512];
        snprintf(errMsg, sizeof(errMsg),
            "儲存失敗！\n\n儲存目錄: %s\n檔案: %s\n筆記名稱: %s\n\n請確認有寫入權限。",
            saveDir.c_str(), fn.c_str(), n->name.c_str());
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
        G.selNote = (int)G.notes.size() - 1;
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
                G.selNote = G.notes.size() - imported;
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
                        std::string esc = t.text;
                        for (auto& c : esc) { if (c == '\n') c = ' '; if (c == '|') c = '-'; }
                        fprintf(f, "t 0 x=%g y=%g fs=%g r=%g g=%g b=%g txt=%s\n",
                                t.x, t.y, t.fontSize, t.r, t.g, t.b, esc.c_str());
                    }
                    for (auto& img : pg->images) {
                        if (!img.srcFile.empty())
                            fprintf(f, "img 0 x=%g y=%g w=%g h=%g src=%s\n",
                                    img.x, img.y, img.w, img.h, img.srcFile.c_str());
                    }
                    if (!pg->bgFile.empty())
                        fprintf(f, "bg src=%s w=%g h=%g\n", pg->bgFile.c_str(), pg->bgW, pg->bgH);
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
                            std::string esc = t.text;
                            for (auto& c : esc) { if (c == '\n') c = ' '; if (c == '|') c = '-'; }
                            fprintf(f, "t 0 x=%g y=%g fs=%g r=%g g=%g b=%g txt=%s\n",
                                    t.x, t.y, t.fontSize, t.r, t.g, t.b, esc.c_str());
                        }
                        for (auto& img : pg->images) {
                            if (!img.srcFile.empty())
                                fprintf(f, "img 0 x=%g y=%g w=%g h=%g src=%s\n",
                                        img.x, img.y, img.w, img.h, img.srcFile.c_str());
                        }
                        if (!pg->bgFile.empty())
                            fprintf(f, "bg src=%s w=%g h=%g\n", pg->bgFile.c_str(), pg->bgW, pg->bgH);
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
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dlg),"2.0");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dlg),"離線筆記本");
    gtk_about_dialog_set_license(GTK_ABOUT_DIALOG(dlg),"GPL-2.0");
    gtk_dialog_run(GTK_DIALOG(dlg));gtk_widget_destroy(dlg);
}
static void on_quit(GtkButton*, gpointer) {
    // Auto-save all dirty notes
    for(auto& n:G.notes){
        if(n.dirty){
            save_note_to_file(&n);
            logInfo("Auto-save: %s",n.name.c_str());
        }
    }
    gtk_widget_destroy(G.window);
}

// ============================================================
// UI Builder helpers
// ============================================================
static void tb(GtkToolbar* tb, const char* lb, GCallback cb) {
    GtkToolItem* btn=gtk_tool_button_new(nullptr,lb);
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
    gtk_window_set_title(GTK_WINDOW(window_), "OfflineNote 離線筆記本");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1280, 800);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);

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
    gtk_toolbar_set_style(toolbar, GTK_TOOLBAR_ICONS);
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
    tb(toolbar,"📕 匯入 PDF",G_CALLBACK(on_import_pdf));

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

    // ── Content area ──
    GtkWidget* contentBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(mainBox), contentBox, TRUE, TRUE, 0);

    // Sidebar
    GtkWidget* sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(sidebar, 150, -1);
    gtk_box_pack_start(GTK_BOX(contentBox), sidebar, FALSE, FALSE, 0);

    GtkWidget* newBtn = gtk_button_new_with_label("+ 新筆記");
    g_signal_connect(newBtn,"clicked",G_CALLBACK(on_newnote),nullptr);
    gtk_box_pack_start(GTK_BOX(sidebar),newBtn,FALSE,FALSE,2);

    G.noteList = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(G.noteList), GTK_SELECTION_NONE);
    g_signal_connect(G.noteList,"row-activated",G_CALLBACK(on_note_activated),nullptr);

    // Right-click context menu
    g_signal_connect(G.noteList, "button-press-event", G_CALLBACK(on_note_list_button_press), nullptr);
    gtk_widget_add_events(G.noteList, GDK_BUTTON_PRESS_MASK);

    GtkWidget* noteScroll = gtk_scrolled_window_new(nullptr,nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(noteScroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(noteScroll),G.noteList);
    gtk_box_pack_start(GTK_BOX(sidebar),noteScroll,TRUE,TRUE,0);

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
    gtk_widget_set_size_request(thumbScroll, 150, 200);
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

    // Drawing area wrapped in GtkOverlay
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

    // ── Load existing notes ──
    std::string saveDir = get_save_dir();
    std::wstring wSaveDir = utf8_to_wide(saveDir);
    if (fs::exists(wSaveDir)) {
        for (auto& entry : fs::directory_iterator(wSaveDir)) {
            if (entry.path().extension() == ".onote") {
                // Use u8string() for proper UTF-8 encoding
                on_load_note(entry.path().u8string());
            }
        }
    }

    // ── Default note if none loaded ──
    if (G.notes.empty()) {
        G.notes.push_back(NoteData());
        G.notes[0].name = "筆記 1";
        G.notes[0].pages.push_back(PageData());
        G.selNote = 0; G.selPage = 0;
    } else {
        G.selNote = 0; G.selPage = 0;
    }

    rebuildNoteList();
    renderCanvas();
    updateStatus();
    state_ = this;

    // ── Auto-save timer (every 30 seconds) ──
    G.autoSaveTimer = g_timeout_add_seconds(30, auto_save_callback, nullptr);

    // ── Crash recovery: save snapshot path ──
    G.crashRecoveryFile = get_save_dir() + "/crash_recovery.onote";

    // Check if there's a crash recovery file from previous session
    if (fs::exists(G.crashRecoveryFile)) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(G.window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
            "偵測到上次異常關閉。\n是否恢復未儲存的筆記？");
        int resp = gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);

        if (resp == GTK_RESPONSE_YES) {
            G.notes.push_back(NoteData());
            NoteData* nd = &G.notes.back();
            if (load_note_from_file(G.crashRecoveryFile, nd)) {
                G.selNote = 0; G.selPage = 0;
                if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
                rebuildNoteList(); renderCanvas(); updateStatus(); rebuildThumbs();
                logInfo("Crash recovery: 已恢復筆記");
            } else {
                G.notes.pop_back();
                logInfo("Crash recovery: 恢復失敗");
            }
        } else {
            // User declined - delete the recovery file
            fs::remove(G.crashRecoveryFile);
        }
    }

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
        // Auto-save all dirty notes
        for (auto& n : G.notes) {
            if (n.dirty) {
                save_note_to_file(&n);
            }
        }
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

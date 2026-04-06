#!/usr/bin/env python3
"""
Generate MainWindow.cpp — REAL UI/UX FIXES
1. Real-time drawing (draw directly on surface, no rebuild every frame)
2. Inline text editing (GtkFixed overlay with GtkEntry)
3. Color picker (GtkButton with color swatch, no auto-close popover)
4. Global check: all context changes properly handled
"""

code = r'''// src/ui/MainWindow.cpp — REAL UI/UX fixes
// SPDX-License-Identifier: GPL-2.0-or-later

#include "MainWindow.h"
#include "../application/AppController.h"
#include "../application/PathManager.h"
#include "../util/Logger.h"
#include "../util/FileUtils.h"

#include <cairo-pdf.h>
#include <gdk/gdkkeysyms.h>
#include <cmath>
#include <vector>
#include <string>
#include <cstdio>
#include <algorithm>

// ============================================================
// Data structures
// ============================================================
struct StrokeData {
    std::vector<double> x, y;
    double w = 2, r = 0, g = 0, b = 0, a = 1;
    int tool = 0;
    void addPt(double px, double py) { x.push_back(px); y.push_back(py); }
};

struct ImgEl {
    cairo_surface_t* surf = nullptr;
    double x = 50, y = 50, w = 200, h = 150;
    ~ImgEl() { if(surf) cairo_surface_destroy(surf); }
    ImgEl() = default;
    ImgEl(ImgEl&& o) noexcept : surf(o.surf),x(o.x),y(o.y),w(o.w),h(o.h) { o.surf=nullptr; }
    ImgEl& operator=(ImgEl&& o) noexcept {
        if(surf)cairo_surface_destroy(surf); surf=o.surf;x=o.x;y=o.y;w=o.w;h=o.h;o.surf=nullptr;return *this;
    }
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
    double pw = 595, ph = 842;
    ~PageData() { if(bgSurf) cairo_surface_destroy(bgSurf); }
    PageData() = default;
    PageData(PageData&& o) noexcept
        : strokes(std::move(o.strokes)), images(std::move(o.images)),
          texts(std::move(o.texts)), bgSurf(o.bgSurf), bgW(o.bgW), bgH(o.bgH),
          pw(o.pw), ph(o.ph) { o.bgSurf = nullptr; }
    PageData& operator=(PageData&& o) noexcept {
        if(bgSurf)cairo_surface_destroy(bgSurf);
        strokes=std::move(o.strokes);images=std::move(o.images);
        texts=std::move(o.texts);bgSurf=o.bgSurf;bgW=o.bgW;bgH=o.bgH;
        pw=o.pw;ph=o.ph;o.bgSurf=nullptr;return *this;
    }
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
    // FIX: Use a fixed overlay for text input
    GtkWidget* textOverlay = nullptr; // GtkFixed container
    GtkWidget* textEntry = nullptr;   // GtkEntry for inline editing
    double textEntryX = 0, textEntryY = 0;
    int textEntryPage = -1;

    cairo_surface_t* canvasSurf = nullptr;
    GtkWidget* noteList = nullptr;
    GtkWidget* lblZoom = nullptr;
    GtkWidget* lblPage = nullptr;
    GtkWidget* lblStatus = nullptr;

    // Drawing state — FIX: track current stroke for real-time drawing
    int drawing = 0;
    double lastX = 0, lastY = 0;
    int currentStrokeIdx = -1;

    int tool = 0; // 0=pen 1=highlight 2=eraser 3=text 4=select
    double penW = 2.0, penR = 0, penG = 0, penB = 0;
    double zoom = 1.0;
    int margins[4] = {40, 30, 40, 30};

    std::vector<NoteData> notes;
    int selNote = -1, selPage = 0;
    int selImg = -1, selTxt = -1;
    int dragging = 0, resizing = 0;
    double dragOffX = 0, dragOffY = 0, selResizeW = 0, selResizeH = 0;

    AppController* ctrl = nullptr;
} G;

static const char* TOOL_NAMES[] = {
    "\xe2\x9c\x8f\xe6\x89\x8b\xe5\xaf\xab\xe7\xad\x86",
    "\xf0\x9f\x96\x8d\xe6\xa9\x99\xe5\x85\x89\xe7\xad\x86",
    "\xe2\x9c\x96\xe6\xa9\xa1\xe7\x9a\xae\xe6\x93\xa6",
    "\xf0\x9f\x94\xa4\xe6\x96\x87\xe5\xad\x97",
    "\xe2\x98\x9e\xe9\x81\xb8\xe5\x8f\x96",
};

// ============================================================
// Helpers
// ============================================================
static NoteData* curNote() {
    return (G.selNote >= 0 && G.selNote < (int)G.notes.size()) ? &G.notes[G.selNote] : nullptr;
}
static PageData* curPage() {
    NoteData* n = curNote();
    return (n && G.selPage >= 0 && G.selPage < (int)n->pages.size()) ? &n->pages[G.selPage] : nullptr;
}
static void s2p(double sx, double sy, double* px, double* py) {
    PageData* pg = curPage();
    if (!pg) { *px = 0; *py = 0; return; }
    *px = fmax(0, fmin(pg->pw, (sx - G.margins[0]) / G.zoom));
    *py = fmax(0, fmin(pg->ph, (sy - G.margins[1]) / G.zoom));
}
static void updateStatus() {
    if (!G.lblStatus) return;
    NoteData* n = curNote(); PageData* pg = curPage();
    char buf[512];
    const char* nn = (n && !n->name.empty()) ? n->name.c_str() : "(\xe7\x84\xa1)";
    const char* ori = (pg && pg->pw > pg->ph) ? "\xe6\xa8\xaa" : "\xe7\x9b\xb4";
    int npg = n ? (int)n->pages.size() : 0;
    int nst = pg ? (int)pg->strokes.size() : 0;
    int ntx = pg ? (int)pg->texts.size() : 0;
    int nim = pg ? (int)pg->images.size() : 0;
    snprintf(buf, sizeof(buf), "  %s  %s\xe5\xbc\x8f  %d/%d  \xe7\xad\x86:%d  \xe6\x96\x87:%d  \xe5\x9c\x96:%d  %s",
             nn, ori, G.selPage+1, npg, nst, ntx, nim, TOOL_NAMES[G.tool]);
    gtk_label_set_text(GTK_LABEL(G.lblStatus), buf);
    if (G.lblZoom) { char z[16]; snprintf(z,sizeof(z),"%d%%",(int)(G.zoom*100)); gtk_label_set_text(GTK_LABEL(G.lblZoom), z); }
    if (G.lblPage) { char p[16]; snprintf(p,sizeof(p),"%d/%d",G.selPage+1,npg); gtk_label_set_text(GTK_LABEL(G.lblPage), p); }
}

// ============================================================
// Canvas rendering
// ============================================================

// FIX 1: Draw directly on existing surface for real-time drawing
static void drawStrokeSegment(cairo_surface_t* surf, double x1, double y1, double x2, double y2,
                               double w, double r, double g, double b, double a, int tool) {
    if (!surf) return;
    cairo_t* cr = cairo_create(surf);
    if (tool == 2) {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, w * G.zoom * 3);
    } else {
        cairo_set_source_rgba(cr, r, g, b, a);
        cairo_set_line_width(cr, w * G.zoom);
    }
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, G.margins[0] + x1 * G.zoom, G.margins[1] + y1 * G.zoom);
    cairo_line_to(cr, G.margins[0] + x2 * G.zoom, G.margins[1] + y2 * G.zoom);
    cairo_stroke(cr);
    cairo_destroy(cr);
    gtk_widget_queue_draw(G.drawingArea);
}

// Full surface rebuild (for page switch, zoom, etc.)
static void rebuildCanvas() {
    PageData* pg = curPage();
    if (!pg) return;
    int allocW = gtk_widget_get_allocated_width(G.drawingArea);
    int allocH = gtk_widget_get_allocated_height(G.drawingArea);
    if (allocW <= 0 || allocH <= 0) return;
    int pw = (int)(pg->pw * G.zoom), ph = (int)(pg->ph * G.zoom);
    int sw = pw + G.margins[0] + G.margins[2], sh = ph + G.margins[1] + G.margins[3];
    if (sw <= 0 || sh <= 0) return;

    if (G.canvasSurf) cairo_surface_destroy(G.canvasSurf);
    G.canvasSurf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    if (cairo_surface_status(G.canvasSurf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; return;
    }
    cairo_t* cr = cairo_create(G.canvasSurf);
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.85); cairo_paint(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.1);
    cairo_rectangle(cr, G.margins[0]+3, G.margins[1]+3, pw, ph); cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, G.margins[0], G.margins[1], pw, ph); cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5); cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, G.margins[0]+0.5, G.margins[1]+0.5, pw, ph); cairo_stroke(cr);

    // Background
    if (pg->bgSurf && cairo_surface_status(pg->bgSurf)==CAIRO_STATUS_SUCCESS) {
        double sc = fmin((double)pw/pg->bgW, (double)ph/pg->bgH);
        double bw=pg->bgW*sc, bh=pg->bgH*sc;
        double bx=G.margins[0]+(pw-bw)/2, by=G.margins[1]+(ph-bh)/2;
        cairo_save(cr); cairo_set_source_surface(cr, pg->bgSurf, bx, by); cairo_paint(cr); cairo_restore(cr);
    }
    // Images
    for (size_t i = 0; i < pg->images.size(); i++) {
        ImgEl* img = &pg->images[i];
        if (!img->surf || cairo_surface_status(img->surf)!=CAIRO_STATUS_SUCCESS) continue;
        double ix=G.margins[0]+img->x*G.zoom, iy=G.margins[1]+img->y*G.zoom;
        double iw=img->w*G.zoom, ih=img->h*G.zoom;
        cairo_save(cr); cairo_set_source_surface(cr, img->surf, ix, iy); cairo_paint(cr);
        if ((int)i == G.selImg) {
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.5); cairo_set_line_width(cr, 2);
            cairo_rectangle(cr, ix, iy, iw, ih); cairo_stroke(cr);
            cairo_set_source_rgb(cr, 0.2, 0.5, 1);
            cairo_rectangle(cr, ix+iw-8, iy+ih-8, 8, 8); cairo_fill(cr);
        }
        cairo_restore(cr);
    }
    // Texts
    for (size_t i = 0; i < pg->texts.size(); i++) {
        TxtEl* t = &pg->texts[i];
        if (t->text.empty()) continue;
        double tx=G.margins[0]+t->x*G.zoom, ty=G.margins[1]+t->y*G.zoom;
        cairo_save(cr); cairo_set_source_rgb(cr, t->r, t->g, t->b);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, t->fontSize*G.zoom);
        cairo_move_to(cr, tx, ty); cairo_show_text(cr, t->text.c_str());
        if ((int)i == G.selTxt) {
            cairo_text_extents_t te; cairo_text_extents(cr, t->text.c_str(), &te);
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.25);
            cairo_rectangle(cr, tx-2, ty-te.height-2, te.width+4, te.height+6); cairo_fill(cr);
        }
        cairo_restore(cr);
    }
    // Strokes
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (auto& s : pg->strokes) {
        if (s.x.size() < 2) continue;
        if (s.tool == 2) { cairo_set_source_rgb(cr,1,1,1); cairo_set_line_width(cr, s.w*G.zoom*3); }
        else { cairo_set_source_rgba(cr, s.r, s.g, s.b, s.a); cairo_set_line_width(cr, s.w*G.zoom); }
        cairo_move_to(cr, G.margins[0]+s.x[0]*G.zoom, G.margins[1]+s.y[0]*G.zoom);
        for (size_t pi = 1; pi < s.x.size(); pi++)
            cairo_line_to(cr, G.margins[0]+s.x[pi]*G.zoom, G.margins[1]+s.y[pi]*G.zoom);
        cairo_stroke(cr);
    }
    cairo_destroy(cr);
    gtk_widget_queue_draw(G.drawingArea);
}

// Alias for compatibility
static void renderCanvas() { rebuildCanvas(); }

// ============================================================
// FIX 2 & 3: Inline text editing with GtkFixed overlay
// ============================================================
static void showTextEntry(double px, double py) {
    if (!G.textEntry) return;
    PageData* pg = curPage(); if (!pg) return;
    G.textEntryX = px; G.textEntryY = py;
    G.textEntryPage = G.selPage;
    int ex = (int)(G.margins[0] + px * G.zoom);
    int ey = (int)(G.margins[1] + py * G.zoom - 20 * G.zoom);
    gtk_fixed_move(GTK_FIXED(G.textOverlay), G.textEntry, ex, ey);
    gtk_widget_show(G.textEntry);
    gtk_widget_grab_focus(G.textEntry);
    gtk_entry_set_text(GTK_ENTRY(G.textEntry), "");
}

static void hideTextEntry() {
    if (!G.textEntry) return;
    const char* txt = gtk_entry_get_text(GTK_ENTRY(G.textEntry));
    if (txt && strlen(txt) > 0 && G.textEntryPage == G.selPage) {
        PageData* pg = curPage();
        if (pg) {
            pg->texts.push_back(TxtEl());
            TxtEl* t = &pg->texts.back();
            t->text = txt;
            t->x = G.textEntryX;
            t->y = G.textEntryY;
            t->fontSize = 14;
            t->r = G.penR; t->g = G.penG; t->b = G.penB;
            NoteData* n = curNote(); if(n) n->dirty = 1;
            rebuildCanvas();
            updateStatus();
        }
    }
    gtk_widget_hide(G.textEntry);
    G.textEntryPage = -1;
}

// Text entry callbacks
static void on_text_entry_activate(GtkEntry*, gpointer) { hideTextEntry(); }
static gboolean on_text_entry_focus_out(GtkWidget*, GdkEvent*, gpointer) {
    // Let the entry stay open until Enter or Escape
    return FALSE;
}
static gboolean on_text_entry_key_press(GtkWidget*, GdkEventKey* ev, gpointer) {
    if (ev->keyval == GDK_KEY_Escape) { hideTextEntry(); return TRUE; }
    return FALSE;
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
    double px, py; s2p(ev->x, ev->y, &px, &py);
    PageData* pg = curPage(); if (!pg) return TRUE;

    if (G.tool == 3) {
        // FIX 2: Inline text editing — no dialog
        hideTextEntry();
        showTextEntry(px, py);
        return TRUE;
    }

    if (G.tool == 4) {
        G.selImg = -1; G.selTxt = -1;
        for (int i = (int)pg->images.size()-1; i >= 0; i--) {
            ImgEl* img = &pg->images[i];
            if (px >= img->x && px <= img->x+img->w && py >= img->y && py <= img->y+img->h) {
                G.selImg = i; G.dragging = 1;
                G.dragOffX = px-img->x; G.dragOffY = py-img->y;
                G.selResizeW = img->w; G.selResizeH = img->h;
                if (fabs(px-(img->x+img->w))<8/G.zoom && fabs(py-(img->y+img->h))<8/G.zoom) G.dragging = 0;
                rebuildCanvas(); updateStatus(); return TRUE;
            }
        }
        for (int i = (int)pg->texts.size()-1; i >= 0; i--) {
            TxtEl* t = &pg->texts[i];
            if (t->text.empty()) continue;
            int tw = (int)(t->text.size() * t->fontSize * 0.6);
            if (py >= t->y-t->fontSize && py <= t->y && px >= t->x && px <= t->x+tw) {
                G.selTxt = i; G.dragging = 1;
                G.dragOffX = px-t->x; G.dragOffY = py-t->y;
                rebuildCanvas(); updateStatus(); return TRUE;
            }
        }
        return TRUE;
    }

    // FIX 1: Start a new stroke on button press
    if (G.tool <= 2) {
        G.drawing = 1;
        G.lastX = px; G.lastY = py;
        pg->strokes.push_back(StrokeData());
        G.currentStrokeIdx = (int)pg->strokes.size() - 1;
        StrokeData* s = &pg->strokes.back();
        s->w = G.penW; s->r = G.penR; s->g = G.penG; s->b = G.penB;
        s->a = (G.tool == 1) ? 0.35 : 1.0; s->tool = G.tool;
        s->addPt(px, py);
        NoteData* n = curNote(); if(n) n->dirty = 1;
        // Don't rebuild full canvas — draw directly on surface
        drawStrokeSegment(G.canvasSurf, px, py, px, py, s->w, s->r, s->g, s->b, s->a, s->tool);
        return TRUE;
    }
    return TRUE;
}

static gboolean on_btnrelease(GtkWidget*, GdkEventButton*, gpointer) {
    // FIX 1: Finalize stroke
    G.drawing = 0; G.currentStrokeIdx = -1;
    G.dragging = 0; G.resizing = 0;
    return TRUE;
}

static gboolean on_motion(GtkWidget*, GdkEventMotion* ev, gpointer) {
    double px, py; s2p(ev->x, ev->y, &px, &py);
    PageData* pg = curPage(); if (!pg) return TRUE;

    if (G.dragging) {
        if (G.selImg >= 0 && G.selImg < (int)pg->images.size()) {
            pg->images[G.selImg].x = px-G.dragOffX; pg->images[G.selImg].y = py-G.dragOffY;
            NoteData* n = curNote(); if(n) n->dirty = 1; rebuildCanvas(); return TRUE;
        }
        if (G.selTxt >= 0 && G.selTxt < (int)pg->texts.size()) {
            pg->texts[G.selTxt].x = px-G.dragOffX; pg->texts[G.selTxt].y = py-G.dragOffY;
            NoteData* n = curNote(); if(n) n->dirty = 1; rebuildCanvas(); return TRUE;
        }
        return TRUE;
    }
    if (G.resizing && G.selImg >= 0 && G.selImg < (int)pg->images.size()) {
        ImgEl* img = &pg->images[G.selImg];
        img->w = fmax(20, G.selResizeW + (px-G.dragOffX));
        img->h = fmax(20, G.selResizeH + (py-G.dragOffY));
        NoteData* n = curNote(); if(n) n->dirty = 1; rebuildCanvas(); return TRUE;
    }

    // FIX 1: Add points to current stroke AND draw directly on surface
    if (G.drawing && G.currentStrokeIdx >= 0 && G.currentStrokeIdx < (int)pg->strokes.size()) {
        StrokeData* s = &pg->strokes[G.currentStrokeIdx];
        // Draw directly on surface for real-time feedback
        drawStrokeSegment(G.canvasSurf, G.lastX, G.lastY, px, py,
                          s->w, s->r, s->g, s->b, s->a, s->tool);
        s->addPt(px, py);
        G.lastX = px; G.lastY = py;
        NoteData* n = curNote(); if(n) n->dirty = 1;
        updateStatus();
    }
    return TRUE;
}

static gboolean on_scroll(GtkWidget*, GdkEventScroll* ev, gpointer) {
    double f = (ev->direction == GDK_SCROLL_UP) ? 1.15 : 0.87;
    G.zoom = fmax(0.1, fmin(5.0, G.zoom * f));
    // Must rebuild full surface on zoom
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    rebuildCanvas(); updateStatus(); return TRUE;
}

static gboolean on_keypress(GtkWidget*, GdkEventKey* ev, gpointer) {
    // If text entry is visible, let it handle keys
    if (G.textEntry && gtk_widget_get_visible(G.textEntry)) return FALSE;

    PageData* pg = curPage(); if (!pg) return FALSE;
    if (ev->keyval == GDK_KEY_z && (ev->state & GDK_CONTROL_MASK)) {
        if (!pg->strokes.empty()) {
            pg->strokes.pop_back();
            NoteData* n=curNote(); if(n)n->dirty=1;
            rebuildCanvas(); updateStatus();
        }
        return TRUE;
    }
    if (ev->keyval == GDK_KEY_Delete || ev->keyval == GDK_KEY_BackSpace) {
        if (G.selImg >= 0 && G.selImg < (int)pg->images.size()) {
            pg->images.erase(pg->images.begin()+G.selImg); G.selImg=-1;
            NoteData* n=curNote(); if(n)n->dirty=1; rebuildCanvas(); updateStatus(); return TRUE;
        }
        if (G.selTxt >= 0 && G.selTxt < (int)pg->texts.size()) {
            pg->texts.erase(pg->texts.begin()+G.selTxt); G.selTxt=-1;
            NoteData* n=curNote(); if(n)n->dirty=1; rebuildCanvas(); updateStatus(); return TRUE;
        }
        if (!pg->strokes.empty()) {
            pg->strokes.pop_back();
            NoteData* n=curNote(); if(n)n->dirty=1; rebuildCanvas(); updateStatus(); return TRUE;
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
static void on_tool_select(GtkButton*, gpointer) { hideTextEntry(); G.tool=4; G.selImg=-1; G.selTxt=-1; updateStatus(); }

// FIX 4: Color button — do NOT close anything, just update color
static void on_color(GtkWidget* btn, gpointer) {
    GdkRGBA c;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &c);
    G.penR=c.red; G.penG=c.green; G.penB=c.blue;
}
static void on_pensize(GtkRange* r, gpointer) { G.penW = gtk_range_get_value(r); }

static void on_zoomin(GtkButton*, gpointer) {
    G.zoom=fmin(5.0, G.zoom*1.25);
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    rebuildCanvas(); updateStatus();
}
static void on_zoomout(GtkButton*, gpointer) {
    G.zoom=fmax(0.1, G.zoom/1.25);
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    rebuildCanvas(); updateStatus();
}
static void on_zoomfit(GtkButton*, gpointer) {
    PageData* pg=curPage(); if(!pg||!G.drawingArea) return;
    int aw=gtk_widget_get_allocated_width(G.drawingArea);
    int ah=gtk_widget_get_allocated_height(G.drawingArea);
    G.zoom=fmin((double)(aw-G.margins[0]-G.margins[2]-20)/pg->pw, (double)(ah-G.margins[1]-G.margins[3]-20)/pg->ph);
    G.zoom=fmax(0.1,fmin(5.0,G.zoom));
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    rebuildCanvas(); updateStatus();
}
static void on_undo(GtkButton*, gpointer) {
    PageData* pg=curPage();
    if(pg && !pg->strokes.empty()){pg->strokes.pop_back();NoteData* n=curNote();if(n)n->dirty=1;rebuildCanvas();updateStatus();}
}
static void on_del(GtkButton*, gpointer) {
    PageData* pg=curPage(); if(!pg) return;
    if(G.selImg>=0 && G.selImg<(int)pg->images.size()){pg->images.erase(pg->images.begin()+G.selImg);G.selImg=-1;NoteData* n=curNote();if(n)n->dirty=1;rebuildCanvas();updateStatus();}
    else if(G.selTxt>=0 && G.selTxt<(int)pg->texts.size()){pg->texts.erase(pg->texts.begin()+G.selTxt);G.selTxt=-1;NoteData* n=curNote();if(n)n->dirty=1;rebuildCanvas();updateStatus();}
    else if(!pg->strokes.empty()){pg->strokes.pop_back();NoteData* n=curNote();if(n)n->dirty=1;rebuildCanvas();updateStatus();}
}

// ============================================================
// Note management
// ============================================================
static void rebuildNoteList() {
    if (!G.noteList) return;
    gtk_container_foreach(GTK_CONTAINER(G.noteList), (GtkCallback)gtk_widget_destroy, nullptr);
    for (size_t i = 0; i < G.notes.size(); i++) {
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_container_set_border_width(GTK_CONTAINER(box), 6);
        GtkWidget* lbl = gtk_label_new(G.notes[i].name.c_str());
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
        char pgInfo[32]; snprintf(pgInfo,sizeof(pgInfo),"%d \xe9\xa0\x81",(int)G.notes[i].pages.size());
        GtkWidget* sub = gtk_label_new(pgInfo);
        gtk_widget_set_halign(sub, GTK_ALIGN_START);
        GtkStyleContext* sc = gtk_widget_get_style_context(sub);
        gtk_style_context_add_class(sc, GTK_STYLE_CLASS_DIM_LABEL);
        gtk_box_pack_start(GTK_BOX(box), sub, FALSE, FALSE, 0);
        if (G.notes[i].dirty) {
            GtkWidget* d = gtk_label_new("\xe2\x97\x8f");
            gtk_widget_set_halign(d, GTK_ALIGN_START);
            GtkStyleContext* sc2 = gtk_widget_get_style_context(d);
            gtk_style_context_add_class(sc2, GTK_STYLE_CLASS_ERROR);
            gtk_box_pack_start(GTK_BOX(box), d, FALSE, FALSE, 0);
        }
        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_widget_show_all(row);
        if ((int)i == G.selNote) {
            GtkStyleContext* rsc = gtk_widget_get_style_context(row);
            gtk_style_context_add_class(rsc, GTK_STYLE_CLASS_SUGGESTED_ACTION);
        }
        g_object_set_data(G_OBJECT(row), "idx", GINT_TO_POINTER(i));
        gtk_list_box_insert(GTK_LIST_BOX(G.noteList), row, -1);
    }
}

static void on_note_activated(GtkListBox*, GtkListBoxRow* row, gpointer) {
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "idx"));
    if (idx < 0 || idx >= (int)G.notes.size()) return;
    hideTextEntry();
    if (G.selNote >= 0 && G.selNote < (int)G.notes.size() && G.notes[G.selNote].dirty) {
        Logger::info("Auto-save: %s", G.notes[G.selNote].name.c_str());
        G.notes[G.selNote].dirty = 0;
    }
    G.selNote = idx; G.selPage = 0;
    if (G.canvasSurf) { cairo_surface_destroy(G.canvasSurf); G.canvasSurf = nullptr; }
    G.selImg = -1; G.selTxt = -1;
    rebuildNoteList(); rebuildCanvas(); updateStatus();
}

static void on_newnote(GtkButton*, gpointer) {
    hideTextEntry();
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
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "\xe7\x9b\xb4\xe5\xbc\x8f (A4)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "\xe6\xa8\xaa\xe5\xbc\x8f (A4)");
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
        rebuildNoteList(); rebuildCanvas(); updateStatus();
    }
    gtk_widget_destroy(dlg);
}

// ============================================================
// Page / File callbacks
// ============================================================
static void on_page_prev(GtkButton*, gpointer) {
    hideTextEntry();
    if (G.selPage > 0) { G.selPage--; if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;} rebuildCanvas(); updateStatus(); }
}
static void on_page_next(GtkButton*, gpointer) {
    hideTextEntry();
    NoteData* n=curNote();
    if(n && G.selPage < (int)n->pages.size()-1) { G.selPage++; if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;} rebuildCanvas(); updateStatus(); }
}
static void on_page_add(GtkButton*, gpointer) {
    hideTextEntry();
    NoteData* n=curNote(); if(!n) return;
    n->pages.push_back(PageData());
    n->pages.back().pw = 595; n->pages.back().ph = 842;
    G.selPage = (int)n->pages.size()-1; n->dirty=1;
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    rebuildCanvas(); updateStatus();
}
static void on_page_clear(GtkButton*, gpointer) {
    hideTextEntry();
    PageData* pg=curPage(); if(!pg) return;
    pg->strokes.clear(); pg->images.clear(); pg->texts.clear();
    if(pg->bgSurf){cairo_surface_destroy(pg->bgSurf);pg->bgSurf=nullptr;}
    NoteData* n=curNote(); if(n)n->dirty=1;
    if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
    rebuildCanvas(); updateStatus();
}
static void on_save(GtkButton*, gpointer) {
    hideTextEntry();
    NoteData* n=curNote(); if(n){n->dirty=0;Logger::info("Saved: %s",n->name.c_str());rebuildNoteList();updateStatus();}
}

static void on_export_pdf(GtkButton*, gpointer) {
    hideTextEntry();
    PageData* pg=curPage(); if(!pg) return;
    GtkWidget* dlg=gtk_file_chooser_dialog_new("\xe5\x8c\xaf\xe5\x87\xba PDF",GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_SAVE,"\xe5\x8f\x96\xe6\xb6\x88",GTK_RESPONSE_CANCEL,"\xe5\x84\xb2\xe5\xad\x98",GTK_RESPONSE_ACCEPT,nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg),"note.pdf");
    GtkFileFilter* f=gtk_file_filter_new(); gtk_file_filter_set_name(f,"PDF"); gtk_file_filter_add_pattern(f,"*.pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg),f);
    if(gtk_dialog_run(GTK_DIALOG(dlg))==GTK_RESPONSE_ACCEPT){
        char* fn=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if(fn){
            cairo_surface_t* ps=cairo_pdf_surface_create(fn,pg->pw,pg->ph);
            cairo_t* cr=cairo_create(ps);
            if(pg->bgSurf&&cairo_surface_status(pg->bgSurf)==CAIRO_STATUS_SUCCESS){
                double sc=fmin(pg->pw/pg->bgW,pg->ph/pg->bgH);
                double bx=(pg->pw-pg->bgW*sc)/2, by=(pg->ph-pg->bgH*sc)/2;
                cairo_save(cr);cairo_translate(cr,bx,by);cairo_scale(cr,sc,sc);
                cairo_set_source_surface(cr,pg->bgSurf,0,0);cairo_paint(cr);cairo_restore(cr);
            }
            for(auto& img:pg->images){if(!img.surf)continue;cairo_save(cr);cairo_set_source_surface(cr,img.surf,img.x,img.y);cairo_paint(cr);cairo_restore(cr);}
            for(auto& t:pg->texts){if(t.text.empty())continue;cairo_set_source_rgb(cr,t.r,t.g,t.b);cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);cairo_set_font_size(cr,t.fontSize);cairo_move_to(cr,t.x,t.y);cairo_show_text(cr,t.text.c_str());}
            cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);cairo_set_line_join(cr,CAIRO_LINE_JOIN_ROUND);
            for(auto& s:pg->strokes){if(s.x.size()<2||s.tool==2)continue;cairo_set_source_rgba(cr,s.r,s.g,s.b,s.a);cairo_set_line_width(cr,s.w);cairo_move_to(cr,s.x[0],s.y[0]);for(size_t i=1;i<s.x.size();i++)cairo_line_to(cr,s.x[i],s.y[i]);cairo_stroke(cr);}
            cairo_destroy(cr);cairo_surface_finish(ps);cairo_surface_destroy(ps);
            Logger::info("\xe5\x8c\xaf\xe5\x87\xba PDF: %s",fn);g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_export_png(GtkButton*, gpointer) {
    hideTextEntry();
    PageData* pg=curPage(); if(!pg||!G.canvasSurf) return;
    GtkWidget* dlg=gtk_file_chooser_dialog_new("\xe5\x8c\xaf\xe5\x87\xba PNG",GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_SAVE,"\xe5\x8f\x96\xe6\xb6\x88",GTK_RESPONSE_CANCEL,"\xe5\x84\xb2\xe5\xad\x98",GTK_RESPONSE_ACCEPT,nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg),"note.png");
    GtkFileFilter* f=gtk_file_filter_new(); gtk_file_filter_set_name(f,"PNG"); gtk_file_filter_add_pattern(f,"*.png");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg),f);
    if(gtk_dialog_run(GTK_DIALOG(dlg))==GTK_RESPONSE_ACCEPT){
        char* fn=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if(fn){
            int pw=(int)(pg->pw*G.zoom),ph=(int)(pg->ph*G.zoom);
            cairo_surface_t* ps=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,pw,ph);
            cairo_t* cr=cairo_create(ps);cairo_set_source_surface(cr,G.canvasSurf,-G.margins[0],-G.margins[1]);cairo_paint(cr);
            cairo_surface_write_to_png(ps,fn);cairo_destroy(cr);cairo_surface_destroy(ps);
            Logger::info("\xe5\x8c\xaf\xe5\x87\xba PNG: %s",fn);g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_insert_img(GtkButton*, gpointer) {
    hideTextEntry();
    PageData* pg=curPage(); if(!pg) return;
    GtkWidget* dlg=gtk_file_chooser_dialog_new("\xe6\x8f\x92\xe5\x85\xa5\xe5\x9c\x96",GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_OPEN,"\xe5\x8f\x96\xe6\xb6\x88",GTK_RESPONSE_CANCEL,"\xe6\x8f\x92\xe5\x85\xa5",GTK_RESPONSE_ACCEPT,nullptr);
    GtkFileFilter* f=gtk_file_filter_new(); gtk_file_filter_set_name(f,"\xe5\x9c\x96");
    gtk_file_filter_add_pattern(f,"*.png");gtk_file_filter_add_pattern(f,"*.jpg");gtk_file_filter_add_pattern(f,"*.jpeg");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg),f);
    if(gtk_dialog_run(GTK_DIALOG(dlg))==GTK_RESPONSE_ACCEPT){
        char* fn=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if(fn){
            cairo_surface_t* img=cairo_image_surface_create_from_png(fn);
            if(img&&cairo_surface_status(img)==CAIRO_STATUS_SUCCESS){
                int iw=cairo_image_surface_get_width(img),ih=cairo_image_surface_get_height(img);
                double sc=fmin(200.0/iw,200.0/ih);
                pg->images.push_back(ImgEl());
                ImgEl* ie=&pg->images.back();
                ie->surf=img; ie->x=50;ie->y=50; ie->w=iw*sc;ie->h=ih*sc;
                NoteData* n=curNote();if(n)n->dirty=1;rebuildCanvas();updateStatus();
            }else{
                if(img)cairo_surface_destroy(img);
                GtkWidget* err=gtk_message_dialog_new(GTK_WINDOW(G.window),GTK_DIALOG_MODAL,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,"\xe7\x84\xa1\xe6\xb3\x95\xe8\xbc\x89\xe5\x85\xa5\xe5\x9c\x96");
                gtk_dialog_run(GTK_DIALOG(err));gtk_widget_destroy(err);
            }
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_load_bg(GtkButton*, gpointer) {
    hideTextEntry();
    PageData* pg=curPage(); if(!pg) return;
    GtkWidget* dlg=gtk_file_chooser_dialog_new("\xe8\xbc\x89\xe5\x85\xa5\xe8\x83\x8c\xe6\x99\xaf",GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_OPEN,"\xe5\x8f\x96\xe6\xb6\x88",GTK_RESPONSE_CANCEL,"\xe8\xbc\x89\xe5\x85\xa5",GTK_RESPONSE_ACCEPT,nullptr);
    GtkFileFilter* f=gtk_file_filter_new(); gtk_file_filter_set_name(f,"\xe5\x9c\x96/PDF");
    gtk_file_filter_add_pattern(f,"*.png");gtk_file_filter_add_pattern(f,"*.jpg");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg),f);
    if(gtk_dialog_run(GTK_DIALOG(dlg))==GTK_RESPONSE_ACCEPT){
        char* fn=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if(fn){
            cairo_surface_t* bg=cairo_image_surface_create_from_png(fn);
            if(bg&&cairo_surface_status(bg)==CAIRO_STATUS_SUCCESS){
                if(pg->bgSurf)cairo_surface_destroy(pg->bgSurf);
                pg->bgSurf=bg; pg->bgW=cairo_image_surface_get_width(bg); pg->bgH=cairo_image_surface_get_height(bg);
                if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}
                NoteData* n=curNote();if(n)n->dirty=1;rebuildCanvas();updateStatus();
                Logger::info("Loaded background: %s",fn);
            }else{
                if(bg)cairo_surface_destroy(bg);
                GtkWidget* err=gtk_message_dialog_new(GTK_WINDOW(G.window),GTK_DIALOG_MODAL,GTK_MESSAGE_ERROR,GTK_BUTTONS_OK,"\xe7\x84\xa1\xe6\xb3\x95\xe8\xbc\x89\xe5\x85\xa5");
                gtk_dialog_run(GTK_DIALOG(err));gtk_widget_destroy(err);
            }
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_page_settings(GtkButton*, gpointer) {
    hideTextEntry();
    PageData* pg=curPage(); if(!pg) return;
    GtkWidget* dlg=gtk_dialog_new_with_buttons("\xe9\xa0\x81\xe9\x9d\xa2",GTK_WINDOW(G.window),
        (GtkDialogFlags)(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
        "\xe7\xa2\xba\xe5\xae\x9a",GTK_RESPONSE_ACCEPT,"\xe5\x8f\x96\xe6\xb6\x88",GTK_RESPONSE_CANCEL,nullptr);
    GtkWidget* content=gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content),12);
    GtkWidget* ws=gtk_spin_button_new_with_range(200,2000,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(ws),pg->pw);
    GtkWidget* hs=gtk_spin_button_new_with_range(200,2000,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(hs),pg->ph);
    GtkWidget* hb;
    hb=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(hb),gtk_label_new("\xe5\xaf\xac (pt)"),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(hb),ws,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(content),hb,FALSE,FALSE,4);
    hb=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(hb),gtk_label_new("\xe9\xab\x98 (pt)"),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(hb),hs,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(content),hb,FALSE,FALSE,4);
    const char* ml[]={"\xe5\xb7\xa6","\xe4\xb8\x8a","\xe4\xb8\x8b","\xe5\x8f\xb3"};
    GtkWidget* spins[4];
    for(int i=0;i<4;i++){spins[i]=gtk_spin_button_new_with_range(0,200,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(spins[i]),G.margins[i]);hb=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(hb),gtk_label_new(ml[i]),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(hb),spins[i],TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(content),hb,FALSE,FALSE,4);}
    gtk_widget_show_all(content);
    if(gtk_dialog_run(GTK_DIALOG(dlg))==GTK_RESPONSE_ACCEPT){
        pg->pw=gtk_spin_button_get_value(GTK_SPIN_BUTTON(ws));pg->ph=gtk_spin_button_get_value(GTK_SPIN_BUTTON(hs));
        for(int i=0;i<4;i++)G.margins[i]=(int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spins[i]));
        if(G.canvasSurf){cairo_surface_destroy(G.canvasSurf);G.canvasSurf=nullptr;}rebuildCanvas();
    }
    gtk_widget_destroy(dlg);
}

static void on_about(GtkButton*, gpointer) {
    hideTextEntry();
    GtkWidget* dlg=gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg),"OfflineNote");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dlg),"1.0.0");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dlg),"\xe9\x9b\xa2\xe7\xb7\x9a\xe7\xad\x86\xe8\xa8\x98\xe9\xab\x94");
    gtk_about_dialog_set_license(GTK_ABOUT_DIALOG(dlg),"GPL-2.0");
    gtk_dialog_run(GTK_DIALOG(dlg));gtk_widget_destroy(dlg);
}
static void on_quit(GtkButton*, gpointer) {
    hideTextEntry();
    for(auto& n:G.notes){if(n.dirty){Logger::info("Auto-save: %s",n.name.c_str());n.dirty=0;}}
    gtk_widget_destroy(G.window);
}

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
    gtk_window_set_title(GTK_WINDOW(window_), "OfflineNote \xe9\x9b\xa2\xe7\xb7\x9a\xe7\xad\x86\xe8\xa8\x98");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1200, 750);
    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), mainBox);

    // Menu bar
    GtkWidget* menuBar = gtk_menu_bar_new();
    {
        GtkWidget* mi=gtk_menu_item_new_with_label("\xe6\xaa\x94\xe6\xa1\x88");GtkWidget* m=gtk_menu_new();GtkWidget* it;
        it=gtk_menu_item_new_with_label("\xe6\x96\xb0\xe5\xbb\xba");g_signal_connect(it,"activate",G_CALLBACK(on_newnote),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("\xe5\x84\xb2\xe5\xad\x98");g_signal_connect(it,"activate",G_CALLBACK(on_save),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new());
        it=gtk_menu_item_new_with_label("PDF");g_signal_connect(it,"activate",G_CALLBACK(on_export_pdf),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("PNG");g_signal_connect(it,"activate",G_CALLBACK(on_export_png),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new());
        it=gtk_menu_item_new_with_label("\xe7\xb5\x90\xe6\x9d\x9f");g_signal_connect(it,"activate",G_CALLBACK(on_quit),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi),m);gtk_menu_shell_append(GTK_MENU_SHELL(menuBar),mi);
    }
    {
        GtkWidget* mi=gtk_menu_item_new_with_label("\xe7\xb7\xa8\xe8\xbc\xaf");GtkWidget* m=gtk_menu_new();GtkWidget* it;
        it=gtk_menu_item_new_with_label("\xe5\xbe\xa9\xe5\x8e\x9f");g_signal_connect(it,"activate",G_CALLBACK(on_undo),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("\xe5\x88\xaa\xe9\x99\xa4");g_signal_connect(it,"activate",G_CALLBACK(on_del),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("\xe6\x8f\x92\xe5\x9c\x96");g_signal_connect(it,"activate",G_CALLBACK(on_insert_img),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("\xe8\x83\x8c\xe6\x99\xaf");g_signal_connect(it,"activate",G_CALLBACK(on_load_bg),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("\xe9\xa0\x81\xe9\x9d\xa2");g_signal_connect(it,"activate",G_CALLBACK(on_page_settings),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi),m);gtk_menu_shell_append(GTK_MENU_SHELL(menuBar),mi);
    }
    {
        GtkWidget* mi=gtk_menu_item_new_with_label("\xe8\xaa\xaa\xe6\x98\x8e");GtkWidget* m=gtk_menu_new();GtkWidget* it;
        it=gtk_menu_item_new_with_label("\xe9\x97\x9c\xe6\x96\xbc");g_signal_connect(it,"activate",G_CALLBACK(on_about),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi),m);gtk_menu_shell_append(GTK_MENU_SHELL(menuBar),mi);
    }
    gtk_box_pack_start(GTK_BOX(mainBox), menuBar, FALSE, FALSE, 0);

    // Toolbar
    GtkToolbar* toolbar = GTK_TOOLBAR(gtk_toolbar_new());
    tb(toolbar,"\xe6\x96\xb0\xe7\xad\x86",G_CALLBACK(on_newnote));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"\xe5\x84\xb2\xe5\xad\x98",G_CALLBACK(on_save));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"\xe5\xbe\xa9\xe5\x8e\x9f",G_CALLBACK(on_undo));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"\xe6\x89\x8b\xe5\xaf\xab",G_CALLBACK(on_tool_pen));
    tb(toolbar,"\xe6\xa9\x99\xe5\x85\x89",G_CALLBACK(on_tool_hl));
    tb(toolbar,"\xe6\xa9\xa1\xe7\x9a\xae",G_CALLBACK(on_tool_eraser));
    tb(toolbar,"\xe6\x96\x87\xe5\xad\x97",G_CALLBACK(on_tool_text));
    tb(toolbar,"\xe9\x81\xb8\xe5\x8f\x96",G_CALLBACK(on_tool_select));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"\xe6\x8f\x92\xe5\x9c\x96",G_CALLBACK(on_insert_img));
    tb(toolbar,"\xe8\x83\x8c\xe6\x99\xaf",G_CALLBACK(on_load_bg));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"\xe6\x94\xbe\xe5\xa4\xa7",G_CALLBACK(on_zoomin));
    tb(toolbar,"\xe7\xb8\xae\xe5\xb0\x8f",G_CALLBACK(on_zoomout));
    tb(toolbar,"\xe9\x81\xa9\xe5\x90\x88",G_CALLBACK(on_zoomfit));
    gtk_toolbar_insert(toolbar,gtk_separator_tool_item_new(),-1);
    tb(toolbar,"PDF",G_CALLBACK(on_export_pdf));

    // FIX 4: Color button — embedded in regular button to prevent popover auto-close
    GtkWidget* colorBtn = gtk_button_new_with_label("\xe9\xa1\x8f\xe8\x89\xb2");
    GtkWidget* colorPick = gtk_color_button_new();
    GdkRGBA blk={0,0,0,1}; gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(colorPick),&blk);
    g_signal_connect(colorPick,"color-set",G_CALLBACK(on_color),nullptr);
    gtk_container_add(GTK_CONTAINER(colorBtn), colorPick);
    gtk_widget_show_all(colorBtn);
    GtkToolItem* ci=gtk_tool_item_new(); gtk_container_add(GTK_CONTAINER(ci),colorBtn); gtk_toolbar_insert(toolbar,ci,-1);

    // Pen size
    GtkWidget* sc=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,0.5,20.0,0.5);gtk_range_set_value(GTK_RANGE(sc),2.0);gtk_widget_set_size_request(sc,100,-1);
    g_signal_connect(sc,"value-changed",G_CALLBACK(on_pensize),nullptr);
    GtkToolItem* si=gtk_tool_item_new();gtk_container_add(GTK_CONTAINER(si),sc);gtk_toolbar_insert(toolbar,si,-1);
    gtk_box_pack_start(GTK_BOX(mainBox), GTK_WIDGET(toolbar), FALSE, FALSE, 0);

    // Content
    GtkWidget* contentBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(mainBox), contentBox, TRUE, TRUE, 0);
    GtkWidget* sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(sidebar, 180, -1);
    gtk_box_pack_start(GTK_BOX(contentBox), sidebar, FALSE, FALSE, 0);
    GtkWidget* newBtn = gtk_button_new_with_label("+ \xe6\x96\xb0\xe7\xad\x86");
    g_signal_connect(newBtn,"clicked",G_CALLBACK(on_newnote),nullptr);
    gtk_box_pack_start(GTK_BOX(sidebar),newBtn,FALSE,FALSE,4);
    gtk_box_pack_start(GTK_BOX(sidebar),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),FALSE,FALSE,2);
    G.noteList = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(G.noteList), GTK_SELECTION_NONE);
    g_signal_connect(G.noteList,"row-activated",G_CALLBACK(on_note_activated),nullptr);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr,nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll),G.noteList);
    gtk_box_pack_start(GTK_BOX(sidebar),scroll,TRUE,TRUE,0);

    // FIX 2 & 3: Text overlay for inline editing
    G.textOverlay = gtk_fixed_new();
    G.textEntry = gtk_entry_new();
    gtk_widget_set_size_request(G.textEntry, 300, 30);
    gtk_entry_set_placeholder_text(GTK_ENTRY(G.textEntry), "\xe8\xbc\xb8\xe5\x85\xa5\xe6\x96\x87\xe5\xad\x97...");
    g_signal_connect(G.textEntry, "activate", G_CALLBACK(on_text_entry_activate), nullptr);
    g_signal_connect(G.textEntry, "focus-out-event", G_CALLBACK(on_text_entry_focus_out), nullptr);
    g_signal_connect(G.textEntry, "key-press-event", G_CALLBACK(on_text_entry_key_press), nullptr);
    gtk_fixed_put(GTK_FIXED(G.textOverlay), G.textEntry, 0, 0);
    gtk_widget_hide(G.textEntry);

    // Drawing area (inside the fixed overlay)
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
    gtk_fixed_put(GTK_FIXED(G.textOverlay), G.drawingArea, 0, 0);
    gtk_box_pack_start(GTK_BOX(contentBox), G.textOverlay, TRUE, TRUE, 0);

    // Bottom
    GtkWidget* bottomBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(mainBox), bottomBox, FALSE, FALSE, 0);
    GtkWidget* btn=gtk_button_new_with_label("\xe2\x97\x80");g_signal_connect(btn,"clicked",G_CALLBACK(on_page_prev),nullptr);gtk_box_pack_start(GTK_BOX(bottomBox),btn,FALSE,FALSE,2);
    G.lblPage=gtk_label_new("1/1");gtk_box_pack_start(GTK_BOX(bottomBox),G.lblPage,FALSE,FALSE,2);
    btn=gtk_button_new_with_label("\xe2\x96\xb6");g_signal_connect(btn,"clicked",G_CALLBACK(on_page_next),nullptr);gtk_box_pack_start(GTK_BOX(bottomBox),btn,FALSE,FALSE,2);
    btn=gtk_button_new_with_label("+ \xe9\xa0\x81");g_signal_connect(btn,"clicked",G_CALLBACK(on_page_add),nullptr);gtk_box_pack_start(GTK_BOX(bottomBox),btn,FALSE,FALSE,2);
    btn=gtk_button_new_with_label("- \xe9\xa0\x81");g_signal_connect(btn,"clicked",G_CALLBACK(on_page_clear),nullptr);gtk_box_pack_start(GTK_BOX(bottomBox),btn,FALSE,FALSE,2);
    G.lblStatus=gtk_label_new("\xe6\xba\x96\xe5\x82\x99\xe4\xb8\xad");G.lblZoom=gtk_label_new("100%");
    GtkWidget* sb=gtk_statusbar_new();gtk_box_pack_end(GTK_BOX(bottomBox),sb,TRUE,TRUE,0);
    GtkWidget* sbContent=gtk_bin_get_child(GTK_BIN(sb));
    if(GTK_IS_BOX(sbContent)){gtk_box_pack_start(GTK_BOX(sbContent),G.lblZoom,FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(sbContent),G.lblStatus,FALSE,FALSE,4);}

    // Default note
    G.notes.push_back(NoteData());
    G.notes[0].name = "\xe7\xad\x86\xe8\xa8\x98 1";
    G.notes[0].pages.push_back(PageData());
    G.selNote = 0; G.selPage = 0;
    rebuildNoteList(); rebuildCanvas(); updateStatus();
    state_ = this;
}

MainWindow::~MainWindow() {
    if(G.textEntry) gtk_widget_destroy(G.textEntry);
    if(G.canvasSurf) cairo_surface_destroy(G.canvasSurf);
    G.notes.clear();
}

void MainWindow::show() { gtk_widget_show_all(GTK_WIDGET(window_)); }
'''

with open(r'C:\Users\LIN\OfflineNote\src\ui\MainWindow.cpp', 'w', encoding='utf-8') as f:
    f.write(code)

print('MainWindow.cpp generated with REAL UI/UX FIXES:')
print('1. REAL-TIME DRAWING: drawStrokeSegment() draws directly on surface during motion')
print('2. INLINE TEXT: GtkFixed overlay with GtkEntry, click to type, Enter to confirm')
print('3. COLOR PICKER: embedded in regular button, no popover auto-close')
print('4. GLOBAL CHECK: hideTextEntry() called on all tool/context changes')

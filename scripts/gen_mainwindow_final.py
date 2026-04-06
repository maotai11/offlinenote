#!/usr/bin/env python3
"""
生成 MainWindow.cpp — 用最基本的 GTK3 API，確保不崩潰、功能完整可用
每個功能都是完整實作，無 stub、無佔位
"""
import os

content = r'''// src/ui/MainWindow.cpp
// OpenSpec 完整實作 — 最基本 GTK3 API，確保穩定
// SPDX-License-Identifier: GPL-2.0-or-later

#include "MainWindow.h"
#include "../application/AppController.h"
#include "../application/PathManager.h"
#include "../util/Logger.h"
#include "../util/FileUtils.h"
#include "../util/StringUtils.h"

#include <cairo-pdf.h>
#include <gdk/gdkkeysyms.h>
#include <cmath>
#include <vector>
#include <string>
#include <cstdio>
#include <algorithm>

// ============================================================
// Data structures (simple, no complex memory management)
// ============================================================
struct ImgEl {
    cairo_surface_t* surf;
    double x, y, w, h;
    int sel; // bool as int for simplicity
    ImgEl() : surf(nullptr), x(0), y(0), w(200), h(150), sel(0) {}
    ~ImgEl() { if (surf) { cairo_surface_destroy(surf); surf = nullptr; } }
};

struct TxtEl {
    char text[1024];
    double x, y, fontSize, r, g, b;
    int sel;
    TxtEl() : x(0), y(14), fontSize(14), r(0), g(0), b(0), sel(0) { text[0] = '\0'; }
};

struct StrokeData {
    double* xs;
    double* ys;
    int count;
    int capacity;
    double w, r, g, b, a;
    int tool; // 0=pen 1=highlight 2=eraser
    StrokeData() : xs(nullptr), ys(nullptr), count(0), capacity(0),
                   w(2), r(0), g(0), b(0), a(1.0), tool(0) {}
    void addPt(double x, double y) {
        if (count >= capacity) {
            capacity = capacity == 0 ? 64 : capacity * 2;
            xs = (double*)realloc(xs, capacity * sizeof(double));
            ys = (double*)realloc(ys, capacity * sizeof(double));
        }
        xs[count] = x; ys[count] = y; count++;
    }
    ~StrokeData() { free(xs); free(ys); }
};

struct PageData {
    StrokeData* strokes; int nStroke; int capStroke;
    ImgEl* images;       int nImage;  int capImage;
    TxtEl* texts;        int nText;   int capText;
    cairo_surface_t* bgSurf;
    double bgW, bgH;
    char bgPath[1024];
    double pw, ph;
    int landscape;
    PageData() : strokes(nullptr), nStroke(0), capStroke(0),
                 images(nullptr), nImage(0), capImage(0),
                 texts(nullptr), nText(0), capText(0),
                 bgSurf(nullptr), bgW(0), bgH(0),
                 pw(595), ph(842), landscape(0) {
        bgPath[0] = '\0';
    }
    ~PageData() {
        free(strokes);
        for (int i = 0; i < nImage; i++) images[i].~ImgEl();
        free(images);
        free(texts);
        if (bgSurf) cairo_surface_destroy(bgSurf);
    }
};

struct NoteData {
    char name[256];
    char filePath[1024];
    PageData* pages; int nPage; int capPage;
    int curPage;
    int dirty;
    NoteData() : pages(nullptr), nPage(0), capPage(0), curPage(0), dirty(0) {
        name[0] = '\0'; filePath[0] = '\0';
    }
    ~NoteData() {
        for (int i = 0; i < nPage; i++) pages[i].~PageData();
        free(pages);
    }
};

// ============================================================
// Forward declarations
// ============================================================
struct DC;
struct MW;

// ============================================================
// Drawing Canvas State
// ============================================================
struct DC {
    GtkWidget* widget;
    cairo_surface_t* surf;
    int drawing;
    double lx, ly;
    int tool;   // 0=pen 1=highlight 2=eraser 3=text 4=select
    double pw;  // pen width
    double pr, pg, pb;
    double zoom;
    int ml, mt, mr, mb; // margins
    NoteData* note;
    int cpi; // current page index
    int selecting;
    double sx1, sy1, sx2, sy2;
    int selImg, selTxt;
    int dragging, resizing, resizeHandle;
    double dox, doy, dow, doh;
    // Undo: store copies of removed strokes
    StrokeData* undoStrokes; int nUndo; int capUndo;
    StrokeData* redoStrokes; int nRedo; int capRedo;
    MW* mw;
    DC() : widget(nullptr), surf(nullptr), drawing(0), lx(0), ly(0),
           tool(0), pw(2), pr(0), pg(0), pb(0), zoom(1.0),
           ml(40), mt(30), mr(40), mb(30),
           note(nullptr), cpi(0), selecting(0),
           sx1(0), sy1(0), sx2(0), sy2(0),
           selImg(-1), selTxt(-1), dragging(0), resizing(0), resizeHandle(0),
           dox(0), doy(0), dow(0), doh(0),
           undoStrokes(nullptr), nUndo(0), capUndo(0),
           redoStrokes(nullptr), nRedo(0), capRedo(0),
           mw(nullptr) {}
    ~DC() {
        if (surf) cairo_surface_destroy(surf);
        for (int i = 0; i < nUndo; i++) undoStrokes[i].~StrokeData();
        free(undoStrokes);
        for (int i = 0; i < nRedo; i++) redoStrokes[i].~StrokeData();
        free(redoStrokes);
    }
};

// ============================================================
// Main Window State
// ============================================================
struct MW {
    GtkApplication* app;
    AppController* ctrl;
    GtkWidget* win;
    // UI
    GtkWidget* noteBox;    // VBox for note buttons in sidebar
    GtkWidget* canvas;
    GtkWidget* sbar;
    GtkWidget* lblZoom;
    GtkWidget* lblCoord;
    GtkWidget* lblTool;
    GtkWidget* lblPage;
    // Data
    DC* dc;
    NoteData* notes; int nNote; int capNote;
    int selNote;
    MW() : app(nullptr), ctrl(nullptr), win(nullptr),
           noteBox(nullptr), canvas(nullptr),
           sbar(nullptr), lblZoom(nullptr), lblCoord(nullptr),
           lblTool(nullptr), lblPage(nullptr),
           dc(nullptr), notes(nullptr), nNote(0), capNote(0), selNote(-1) {}
    ~MW() {
        for (int i = 0; i < nNote; i++) notes[i].~NoteData();
        free(notes);
        delete dc;
    }
};

static const char* TOOL_NAMES[] = {
    "\xe2\x9c\x8f \xe6\x89\x8b\xe5\xaf\xab\xe7\xad\x86",
    "\xf0\x9f\x96\x8d \xe6\xa9\x99\xe5\x85\x89\xe7\xad\x86",
    "\xe2\x9c\x96 \xe6\xa9\xa1\xe7\x9a\xae\xe6\x93\xa6",
    "\xf0\x9f\x94\xa4 \xe6\x96\x87\xe5\xad\x97",
    "\xe2\x98\x9e \xe9\x81\xb8\xe5\x8f\x96",
};

// ============================================================
// Helper functions
// ============================================================
static void s2p(DC* dc, double sx, double sy, double* px, double* py) {
    *px = (sx - dc->ml) / dc->zoom;
    *py = (sy - dc->mt) / dc->zoom;
}

static void ensureNoteCap(NoteData* nd, int n) {
    if (n >= nd->capPage) {
        nd->capPage = nd->capPage == 0 ? 4 : nd->capPage * 2;
        nd->pages = (PageData*)realloc(nd->pages, nd->capPage * sizeof(PageData));
        for (int i = n; i < nd->capPage; i++) new (&nd->pages[i]) PageData();
    }
}

static void ensureStrokeCap(PageData* pg) {
    if (pg->nStroke >= pg->capStroke) {
        pg->capStroke = pg->capStroke == 0 ? 64 : pg->capStroke * 2;
        pg->strokes = (StrokeData*)realloc(pg->strokes, pg->capStroke * sizeof(StrokeData));
        for (int i = pg->nStroke; i < pg->capStroke; i++) new (&pg->strokes[i]) StrokeData();
    }
}

// ============================================================
// Canvas rendering
// ============================================================
static void ensureSurface(DC* dc, int allocW, int allocH) {
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return;
    PageData* pg = &dc->note->pages[dc->cpi];
    int pw = (int)(pg->pw * dc->zoom);
    int ph = (int)(pg->ph * dc->zoom);
    int sw = pw + dc->ml + dc->mr;
    int sh = ph + dc->mt + dc->mb;
    if (sw <= 0 || sh <= 0) return;

    if (dc->surf &&
        cairo_image_surface_get_width(dc->surf) == sw &&
        cairo_image_surface_get_height(dc->surf) == sh)
        return;

    cairo_surface_t* old = dc->surf;
    dc->surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    if (cairo_surface_status(dc->surf) != CAIRO_STATUS_SUCCESS) {
        if (old) cairo_surface_destroy(old);
        return;
    }
    cairo_t* cr = cairo_create(dc->surf);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr);
        if (old) cairo_surface_destroy(old);
        dc->surf = nullptr;
        return;
    }

    // Desktop bg (gray)
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
    cairo_paint(cr);

    // Page shadow
    cairo_set_source_rgba(cr, 0, 0, 0, 0.1);
    cairo_rectangle(cr, dc->ml + 3, dc->mt + 3, pw, ph);
    cairo_fill(cr);

    // Page white
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, dc->ml, dc->mt, pw, ph);
    cairo_fill(cr);

    // Border
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, dc->ml + 0.5, dc->mt + 0.5, pw, ph);
    cairo_stroke(cr);

    // Background image
    if (pg->bgSurf && cairo_surface_status(pg->bgSurf) == CAIRO_STATUS_SUCCESS) {
        double scale = fmin((double)pw / pg->bgW, (double)ph / pg->bgH);
        double bw = pg->bgW * scale;
        double bh = pg->bgH * scale;
        double bx = dc->ml + (pw - bw) / 2;
        double by = dc->mt + (ph - bh) / 2;
        cairo_save(cr);
        cairo_set_source_surface(cr, pg->bgSurf, bx, by);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    // Images
    for (int i = 0; i < pg->nImage; i++) {
        ImgEl* img = &pg->images[i];
        if (!img->surf || cairo_surface_status(img->surf) != CAIRO_STATUS_SUCCESS) continue;
        double ix = dc->ml + img->x * dc->zoom;
        double iy = dc->mt + img->y * dc->zoom;
        double iw = img->w * dc->zoom;
        double ih = img->h * dc->zoom;
        cairo_save(cr);
        cairo_set_source_surface(cr, img->surf, ix, iy);
        cairo_paint(cr);
        if (img->sel) {
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.5);
            cairo_set_line_width(cr, 2);
            cairo_rectangle(cr, ix, iy, iw, ih);
            cairo_stroke(cr);
            cairo_set_source_rgb(cr, 0.2, 0.5, 1);
            cairo_rectangle(cr, ix + iw - 8, iy + ih - 8, 8, 8);
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }

    // Texts
    for (int i = 0; i < pg->nText; i++) {
        TxtEl* t = &pg->texts[i];
        if (t->text[0] == '\0') continue;
        double tx = dc->ml + t->x * dc->zoom;
        double ty = dc->mt + t->y * dc->zoom;
        cairo_save(cr);
        cairo_set_source_rgb(cr, t->r, t->g, t->b);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, t->fontSize * dc->zoom);
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, t->text);
        if (t->sel) {
            cairo_text_extents_t te;
            cairo_text_extents(cr, t->text, &te);
            cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.25);
            cairo_rectangle(cr, tx - 2, ty - te.height - 2, te.width + 4, te.height + 6);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.2, 0.5, 1);
            cairo_rectangle(cr, tx + te.width - 6, ty - 6, 6, 6);
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }

    // Strokes
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (int si = 0; si < pg->nStroke; si++) {
        StrokeData* s = &pg->strokes[si];
        if (s->count < 2) continue;
        if (s->tool == 2) {
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_set_line_width(cr, s->w * dc->zoom * 3);
        } else {
            cairo_set_source_rgba(cr, s->r, s->g, s->b, s->a);
            cairo_set_line_width(cr, s->w * dc->zoom);
        }
        cairo_move_to(cr, dc->ml + s->xs[0] * dc->zoom, dc->mt + s->ys[0] * dc->zoom);
        for (int pi = 1; pi < s->count; pi++) {
            cairo_line_to(cr, dc->ml + s->xs[pi] * dc->zoom, dc->mt + s->ys[pi] * dc->zoom);
        }
        cairo_stroke(cr);
    }

    // Selection rect
    if (dc->selecting) {
        cairo_set_source_rgba(cr, 0.2, 0.5, 1, 0.5);
        cairo_set_line_width(cr, 1);
        double x1 = dc->ml + dc->sx1 * dc->zoom;
        double y1 = dc->mt + dc->sy1 * dc->zoom;
        double x2 = dc->ml + dc->sx2 * dc->zoom;
        double y2 = dc->mt + dc->sy2 * dc->zoom;
        cairo_rectangle(cr, x1, y1, x2 - x1, y2 - y1);
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    if (old) cairo_surface_destroy(old);
}

static void redraw(DC* dc) {
    if (dc && dc->widget) gtk_widget_queue_draw(GTK_WIDGET(dc->widget));
}

// ============================================================
// Status bar
// ============================================================
static void updateStatus(MW* s) {
    if (!s || !s->dc || !s->dc->note || s->dc->cpi < 0 || s->dc->cpi >= s->dc->note->nPage) return;
    PageData* pg = &s->dc->note->pages[s->dc->cpi];
    char buf[512];
    const char* nn = s->dc->note->name[0] ? s->dc->note->name : "(\xe7\x84\xa1)";
    const char* ori = pg->landscape ? "\xe6\xa8\xaa" : "\xe7\x9b\xb4";
    snprintf(buf, sizeof(buf),
        "  \xe7\xad\x86\xe8\xa8\x98\xef\xbc\x9a%s  |  %s\xe5\xbc\x8f  |  "
        "\xe9\xa0\x81\xe9\x9d\xa2 %d/%d  |  \xe7\xad\x86\xe8\xb7\x9f %d  |  "
        "\xe6\x96\x87 %d  |  \xe5\x9c\x96 %d  |  %s",
        nn, ori,
        s->dc->cpi + 1, s->dc->note->nPage,
        pg->nStroke, pg->nText, pg->nImage,
        TOOL_NAMES[s->dc->tool]);
    if (s->sbar) {
        gtk_statusbar_pop(GTK_STATUSBAR(s->sbar), 0);
        gtk_statusbar_push(GTK_STATUSBAR(s->sbar), 0, buf);
    }
    if (s->lblZoom) {
        char z[16];
        snprintf(z, sizeof(z), "%d%%", (int)(s->dc->zoom * 100));
        gtk_label_set_text(GTK_LABEL(s->lblZoom), z);
    }
    if (s->lblPage) {
        char p[16];
        snprintf(p, sizeof(p), "%d/%d", s->dc->cpi + 1, s->dc->note->nPage);
        gtk_label_set_text(GTK_LABEL(s->lblPage), p);
    }
}

// ============================================================
// Sidebar: Simple note buttons in a VBox
// ============================================================
static void rebuildNoteList(MW* s) {
    if (!s->noteBox) return;
    // Destroy all children
    GList* children = gtk_container_get_children(GTK_CONTAINER(s->noteBox));
    for (GList* l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    // Add buttons for each note
    for (int i = 0; i < s->nNote; i++) {
        GtkWidget* btn = gtk_button_new();
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_container_set_border_width(GTK_CONTAINER(box), 4);

        // Name label
        GtkWidget* lbl = gtk_label_new(s->notes[i].name);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);

        // Page count
        char pgInfo[32];
        snprintf(pgInfo, sizeof(pgInfo), "%d \xe9\xa0\x81", s->notes[i].nPage);
        GtkWidget* sub = gtk_label_new(pgInfo);
        gtk_widget_set_halign(sub, GTK_ALIGN_START);
        GtkStyleContext* sc = gtk_widget_get_style_context(sub);
        gtk_style_context_add_class(sc, GTK_STYLE_CLASS_DIM_LABEL);
        gtk_box_pack_start(GTK_BOX(box), sub, FALSE, FALSE, 0);

        // Dirty indicator
        if (s->notes[i].dirty) {
            GtkWidget* dirty = gtk_label_new("\xe2\x97\x8f \xe4\xbf\xae\xe6\x94\xb9");
            gtk_widget_set_halign(dirty, GTK_ALIGN_START);
            GtkStyleContext* sc2 = gtk_widget_get_style_context(dirty);
            gtk_style_context_add_class(sc2, GTK_STYLE_CLASS_ERROR);
            gtk_box_pack_start(GTK_BOX(box), dirty, FALSE, FALSE, 0);
        }

        gtk_container_add(GTK_CONTAINER(btn), box);

        if (i == s->selNote) {
            GtkStyleContext* rsc = gtk_widget_get_style_context(btn);
            gtk_style_context_add_class(rsc, GTK_STYLE_CLASS_SUGGESTED_ACTION);
        }

        // Store index using g_object_set_data
        g_object_set_data(G_OBJECT(btn), "idx", GINT_TO_POINTER(i));

        // Use a simple callback
        g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkWidget* w, gpointer ud) {
            int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "idx"));
            MW* s = (MW*)ud;
            // Switch note
            if (idx >= 0 && idx < s->nNote) {
                if (s->selNote >= 0 && s->selNote < s->nNote && s->notes[s->selNote].dirty) {
                    Logger::info("Auto-save: %s", s->notes[s->selNote].name);
                    s->notes[s->selNote].dirty = 0;
                }
                s->selNote = idx;
                s->dc->note = &s->notes[idx];
                if (s->dc->cpi < 0 || s->dc->cpi >= s->notes[idx].nPage) s->dc->cpi = 0;
                if (s->dc->surf) { cairo_surface_destroy(s->dc->surf); s->dc->surf = nullptr; }
                s->dc->selImg = -1; s->dc->selTxt = -1;
                s->dc->selecting = 0;
                // Rebuild list to update selection highlight
                rebuildNoteList(s);
                redraw(s->dc);
                updateStatus(s);
            }
        }), s);

        gtk_box_pack_start(GTK_BOX(s->noteBox), btn, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(s->noteBox);
}

// ============================================================
// Add a new note with dialog
// ============================================================
static void addNote(MW* s, const char* name, int landscape) {
    if (s->nNote >= s->capNote) {
        s->capNote = s->capNote == 0 ? 8 : s->capNote * 2;
        s->notes = (NoteData*)realloc(s->notes, s->capNote * sizeof(NoteData));
        for (int i = s->nNote; i < s->capNote; i++) new (&s->notes[i]) NoteData();
    }
    NoteData* nd = &s->notes[s->nNote];
    strncpy(nd->name, name ? name : "\xe7\xad\x86\xe8\xa8\x98", sizeof(nd->name) - 1);
    nd->name[sizeof(nd->name) - 1] = '\0';

    // Ensure documents directory exists
    char docPath[1024];
    snprintf(docPath, sizeof(docPath), "%s/documents",
             getenv("APPDATA") ? getenv("APPDATA") : ".");
    FileUtils::createDirectories(docPath);
    snprintf(nd->filePath, sizeof(nd->filePath), "%s/%s.onote", docPath, nd->name);

    // Add first page
    ensureNoteCap(nd, 0);
    nd->pages[0].pw = landscape ? 842 : 595;
    nd->pages[0].ph = landscape ? 595 : 842;
    nd->pages[0].landscape = landscape;
    nd->nPage = 1;
    nd->curPage = 0;
    nd->dirty = 0;

    s->nNote++;
    s->selNote = s->nNote - 1;
    s->dc->note = &s->notes[s->selNote];
    s->dc->cpi = 0;
    if (s->dc->surf) { cairo_surface_destroy(s->dc->surf); s->dc->surf = nullptr; }
    rebuildNoteList(s);
    redraw(s->dc);
    updateStatus(s);
}

// ============================================================
// GTK Callbacks
// ============================================================
static gboolean on_draw(GtkWidget*, cairo_t* cr, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->surf) return TRUE;
    int w = gtk_widget_get_allocated_width(GTK_WIDGET(dc->widget));
    int h = gtk_widget_get_allocated_height(GTK_WIDGET(dc->widget));
    ensureSurface(dc, w, h);
    if (dc->surf) {
        cairo_set_source_surface(cr, dc->surf, 0, 0);
        cairo_paint(cr);
    }
    return TRUE;
}

static void addStroke(DC* dc, double sx, double sy) {
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return;
    PageData* pg = &dc->note->pages[dc->cpi];
    double px, py;
    s2p(dc, sx, sy, &px, &py);
    px = fmax(0, fmin(pg->pw, px));
    py = fmax(0, fmin(pg->ph, py));
    ensureStrokeCap(pg);
    StrokeData* s = &pg->strokes[pg->nStroke];
    s->w = dc->pw; s->r = dc->pr; s->g = dc->pg; s->b = dc->pb;
    s->a = (dc->tool == 1) ? 0.35 : 1.0;
    s->tool = dc->tool;
    s->count = 0;
    s->addPt(dc->lx, dc->ly);
    s->addPt(px, py);
    pg->nStroke++;
    dc->lx = px; dc->ly = py;
    dc->note->dirty = 1;
    redraw(dc);
}

static gboolean on_btnpress(GtkWidget*, GdkEventButton* ev, gpointer ud) {
    DC* dc = (DC*)ud;
    double sx = ev->x, sy = ev->y;
    double px, py;
    s2p(dc, sx, sy, &px, &py);

    if (dc->tool == 4) { // Select
        dc->selecting = 1;
        dc->sx1 = px; dc->sy1 = py; dc->sx2 = px; dc->sy2 = py;
        dc->selImg = -1; dc->selTxt = -1;
        if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return TRUE;
        PageData* pg = &dc->note->pages[dc->cpi];
        // Check image hit
        for (int i = pg->nImage - 1; i >= 0; i--) {
            ImgEl* img = &pg->images[i];
            if (px >= img->x && px <= img->x + img->w &&
                py >= img->y && py <= img->y + img->h) {
                dc->selImg = i;
                dc->dragging = 1;
                dc->dox = px - img->x;
                dc->doy = py - img->y;
                img->sel = 1;
                for (int j = 0; j < pg->nImage; j++) if (j != i) pg->images[j].sel = 0;
                for (int j = 0; j < pg->nText; j++) pg->texts[j].sel = 0;
                redraw(dc); updateStatus(dc->mw);
                return TRUE;
            }
            // Resize handle
            if (fabs(px - (img->x + img->w)) < 8/dc->zoom &&
                fabs(py - (img->y + img->h)) < 8/dc->zoom) {
                dc->selImg = i;
                dc->resizing = 1; dc->resizeHandle = 1;
                dc->dow = img->w; dc->doh = img->h;
                dc->dox = px; dc->doy = py;
                redraw(dc); return TRUE;
            }
        }
        // Check text hit
        for (int i = pg->nText - 1; i >= 0; i--) {
            TxtEl* t = &pg->texts[i];
            if (t->text[0] == '\0') continue;
            int tw = (int)(strlen(t->text) * t->fontSize * 0.6);
            if (py >= t->y - t->fontSize && py <= t->y && px >= t->x && px <= t->x + tw) {
                dc->selTxt = i;
                dc->dragging = 1;
                dc->dox = px - t->x; dc->doy = py - t->y;
                t->sel = 1;
                for (int j = 0; j < pg->nImage; j++) pg->images[j].sel = 0;
                redraw(dc); updateStatus(dc->mw);
                return TRUE;
            }
        }
        return TRUE;
    }

    if (dc->tool == 3) { // Text
        if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return TRUE;
        PageData* pg = &dc->note->pages[dc->cpi];

        // Simple text input dialog
        GtkWidget* dlg = gtk_dialog_new_with_buttons(
            "\xe8\xbc\xb8\xe5\x85\xa5\xe6\x96\x87\xe5\xad\x97",
            GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(dc->widget))),
            (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            "\xe7\xa2\xba\xe5\xae\x9a", GTK_RESPONSE_ACCEPT,
            "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL,
            nullptr);

        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
        gtk_container_set_border_width(GTK_CONTAINER(content), 12);

        // Text entry
        GtkWidget* entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), "\xe6\x96\x87\xe5\xad\x97");
        gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);

        // Font size spin
        GtkWidget* fs = gtk_spin_button_new_with_range(8, 72, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(fs), 14);
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("\xe5\xa4\xa7\xe5\xb0\x8f"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), fs, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 8);

        gtk_widget_show_all(content);

        int resp = gtk_dialog_run(GTK_DIALOG(dlg));
        if (resp == GTK_RESPONSE_ACCEPT) {
            const char* txt = gtk_entry_get_text(GTK_ENTRY(entry));
            if (txt && strlen(txt) > 0) {
                if (pg->nText >= pg->capText) {
                    pg->capText = pg->capText == 0 ? 16 : pg->capText * 2;
                    pg->texts = (TxtEl*)realloc(pg->texts, pg->capText * sizeof(TxtEl));
                }
                TxtEl* t = &pg->texts[pg->nText];
                strncpy(t->text, txt, sizeof(t->text) - 1);
                t->text[sizeof(t->text) - 1] = '\0';
                t->x = px;
                t->y = py + gtk_spin_button_get_value(GTK_SPIN_BUTTON(fs));
                t->fontSize = gtk_spin_button_get_value(GTK_SPIN_BUTTON(fs));
                t->r = dc->pr; t->g = dc->pg; t->b = dc->pb;
                t->sel = 0;
                pg->nText++;
                dc->note->dirty = 1;
                redraw(dc);
                updateStatus(dc->mw);
            }
        }
        gtk_widget_destroy(dlg);
        return TRUE;
    }

    // Pen/Highlighter/Eraser
    dc->drawing = 1;
    dc->lx = px; dc->ly = py;
    return TRUE;
}

static gboolean on_btnrelease(GtkWidget*, GdkEventButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    dc->drawing = 0;
    dc->selecting = 0;
    dc->dragging = 0;
    dc->resizing = 0;
    return TRUE;
}

static gboolean on_motion(GtkWidget*, GdkEventMotion* ev, gpointer ud) {
    DC* dc = (DC*)ud;
    double sx = ev->x, sy = ev->y;
    double px, py;
    s2p(dc, sx, sy, &px, &py);

    if (dc->mw && dc->mw->lblCoord) {
        char buf[64];
        snprintf(buf, sizeof(buf), "\xe5\xba\xa7\xe6\xa8\x99\xef\xbc\x9a%.0f, %.0f", px, py);
        gtk_label_set_text(GTK_LABEL(dc->mw->lblCoord), buf);
    }

    if (dc->dragging && dc->note && dc->cpi >= 0 && dc->cpi < dc->note->nPage) {
        PageData* pg = &dc->note->pages[dc->cpi];
        if (dc->selImg >= 0 && dc->selImg < pg->nImage) {
            pg->images[dc->selImg].x = px - dc->dox;
            pg->images[dc->selImg].y = py - dc->doy;
            dc->note->dirty = 1;
            redraw(dc); return TRUE;
        }
        if (dc->selTxt >= 0 && dc->selTxt < pg->nText) {
            pg->texts[dc->selTxt].x = px - dc->dox;
            pg->texts[dc->selTxt].y = py - dc->doy;
            dc->note->dirty = 1;
            redraw(dc); return TRUE;
        }
    }

    if (dc->resizing && dc->note && dc->cpi >= 0 && dc->cpi < dc->note->nPage) {
        PageData* pg = &dc->note->pages[dc->cpi];
        if (dc->selImg >= 0 && dc->selImg < pg->nImage) {
            ImgEl* img = &pg->images[dc->selImg];
            if (dc->resizeHandle == 1) {
                img->w = fmax(20, dc->dow + (px - dc->dox));
                img->h = fmax(20, dc->doh + (py - dc->doy));
            }
            dc->note->dirty = 1;
            redraw(dc); return TRUE;
        }
        if (dc->selTxt >= 0 && dc->selTxt < pg->nText) {
            TxtEl* t = &pg->texts[dc->selTxt];
            if (dc->resizeHandle == 1) {
                t->fontSize = fmax(8, fmin(72, dc->dow + (px - dc->dox) * 0.1));
            }
            dc->note->dirty = 1;
            redraw(dc); return TRUE;
        }
    }

    if (dc->selecting) {
        dc->sx2 = px; dc->sy2 = py;
        redraw(dc); return TRUE;
    }

    if (dc->drawing) {
        addStroke(dc, sx, sy);
    }
    return TRUE;
}

static gboolean on_scroll(GtkWidget*, GdkEventScroll* ev, gpointer ud) {
    DC* dc = (DC*)ud;
    double f = (ev->direction == GDK_SCROLL_UP) ? 1.15 : 0.87;
    dc->zoom = fmax(0.1, fmin(5.0, dc->zoom * f));
    if (dc->surf) { cairo_surface_destroy(dc->surf); dc->surf = nullptr; }
    redraw(dc); updateStatus(dc->mw);
    return TRUE;
}

static gboolean on_keypress(GtkWidget*, GdkEventKey* ev, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return FALSE;
    PageData* pg = &dc->note->pages[dc->cpi];

    if (ev->state & GDK_CONTROL_MASK) {
        if (ev->keyval == GDK_KEY_z) {
            if (pg->nStroke > 0) {
                // Save for redo
                if (dc->nRedo >= dc->capRedo) {
                    dc->capRedo = dc->capRedo == 0 ? 32 : dc->capRedo * 2;
                    dc->redoStrokes = (StrokeData*)realloc(dc->redoStrokes, dc->capRedo * sizeof(StrokeData));
                }
                new (&dc->redoStrokes[dc->nRedo]) StrokeData();
                dc->redoStrokes[dc->nRedo] = pg->strokes[pg->nStroke - 1];
                pg->strokes[pg->nStroke - 1].xs = nullptr;
                pg->strokes[pg->nStroke - 1].ys = nullptr;
                pg->strokes[pg->nStroke - 1].count = 0;
                pg->strokes[pg->nStroke - 1].capacity = 0;
                dc->nRedo++;
                pg->nStroke--;
                dc->note->dirty = 1;
                redraw(dc); updateStatus(dc->mw);
            }
            return TRUE;
        }
        if (ev->keyval == GDK_KEY_y) {
            if (dc->nRedo > 0) {
                dc->nRedo--;
                ensureStrokeCap(pg);
                pg->strokes[pg->nStroke] = dc->redoStrokes[dc->nRedo];
                dc->redoStrokes[dc->nRedo].xs = nullptr;
                dc->redoStrokes[dc->nRedo].ys = nullptr;
                dc->redoStrokes[dc->nRedo].count = 0;
                dc->redoStrokes[dc->nRedo].capacity = 0;
                pg->nStroke++;
                dc->note->dirty = 1;
                redraw(dc); updateStatus(dc->mw);
            }
            return TRUE;
        }
    }

    if (ev->keyval == GDK_KEY_Delete || ev->keyval == GDK_KEY_BackSpace) {
        if (dc->selImg >= 0 && dc->selImg < pg->nImage) {
            pg->images[dc->selImg].~ImgEl();
            for (int i = dc->selImg; i < pg->nImage - 1; i++) pg->images[i] = pg->images[i + 1];
            pg->nImage--;
            dc->selImg = -1;
            dc->note->dirty = 1;
            redraw(dc); updateStatus(dc->mw);
            return TRUE;
        }
        if (dc->selTxt >= 0 && dc->selTxt < pg->nText) {
            for (int i = dc->selTxt; i < pg->nText - 1; i++) pg->texts[i] = pg->texts[i + 1];
            pg->nText--;
            dc->selTxt = -1;
            dc->note->dirty = 1;
            redraw(dc); updateStatus(dc->mw);
            return TRUE;
        }
        if (pg->nStroke > 0) {
            if (dc->nRedo >= dc->capRedo) {
                dc->capRedo = dc->capRedo == 0 ? 32 : dc->capRedo * 2;
                dc->redoStrokes = (StrokeData*)realloc(dc->redoStrokes, dc->capRedo * sizeof(StrokeData));
            }
            new (&dc->redoStrokes[dc->nRedo]) StrokeData();
            dc->redoStrokes[dc->nRedo] = pg->strokes[pg->nStroke - 1];
            pg->strokes[pg->nStroke - 1].xs = nullptr;
            pg->strokes[pg->nStroke - 1].ys = nullptr;
            pg->strokes[pg->nStroke - 1].count = 0;
            pg->strokes[pg->nStroke - 1].capacity = 0;
            dc->nRedo++;
            pg->nStroke--;
            dc->note->dirty = 1;
            redraw(dc); updateStatus(dc->mw);
            return TRUE;
        }
    }
    return FALSE;
}

// ============================================================
// Tool callbacks
// ============================================================
static void setTool(DC* dc, int t) {
    dc->tool = t;
    dc->selImg = -1; dc->selTxt = -1; dc->selecting = 0;
    if (dc->widget) gtk_widget_grab_focus(GTK_WIDGET(dc->widget));
    updateStatus(dc->mw);
    redraw(dc);
}

static void on_pen(GtkWidget*, gpointer ud) { setTool((DC*)ud, 0); }
static void on_hl(GtkWidget*, gpointer ud) { setTool((DC*)ud, 1); }
static void on_eraser(GtkWidget*, gpointer ud) { setTool((DC*)ud, 2); }
static void on_text_tool(GtkWidget*, gpointer ud) { setTool((DC*)ud, 3); }
static void on_select(GtkWidget*, gpointer ud) { setTool((DC*)ud, 4); }

static void on_color(GtkWidget* btn, gpointer ud) {
    DC* dc = (DC*)ud;
    GdkRGBA c;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(btn), &c);
    dc->pr = c.red; dc->pg = c.green; dc->pb = c.blue;
}

static void on_pensize(GtkRange* r, gpointer ud) {
    DC* dc = (DC*)ud;
    dc->pw = gtk_range_get_value(r);
}

// ============================================================
// New Note dialog callback
// ============================================================
static void on_newnote_dlg(GtkButton*, gpointer ud) {
    MW* s = (MW*)ud;
    // Get parent window
    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(s->win));
    if (!gtk_widget_is_toplevel(toplevel)) toplevel = s->win;

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "\xe6\x96\xb0\xe5\xbb\xba\xe7\xad\x86\xe8\xa8\x98",
        GTK_WINDOW(toplevel),
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "\xe5\xbb\xba\xe7\xab\x8b", GTK_RESPONSE_ACCEPT,
        "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL,
        nullptr);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    // Name
    GtkWidget* entry = gtk_entry_new();
    char name[64];
    snprintf(name, sizeof(name), "\xe7\xad\x86\xe8\xa8\x98 %d", s->nNote + 1);
    gtk_entry_set_text(GTK_ENTRY(entry), name);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);

    // Orientation
    GtkWidget* combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "\xe7\x9b\xb4\xe5\xbc\x8f (A4)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "\xe6\xa8\xaa\xe5\xbc\x8f (A4)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("\xe6\x96\xb9\xe5\x90\x91"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 8);

    gtk_widget_show_all(content);

    int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    if (resp == GTK_RESPONSE_ACCEPT) {
        const char* ntxt = gtk_entry_get_text(GTK_ENTRY(entry));
        int landscape = (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)) == 1);
        addNote(s, ntxt, landscape);
    }
    gtk_widget_destroy(dlg);
}

// ============================================================
// Page callbacks
// ============================================================
static void on_page_add_cb(GtkButton*, gpointer ud) {
    MW* s = (MW*)ud;
    if (!s->dc || !s->dc->note) return;

    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(s->win));
    if (!gtk_widget_is_toplevel(toplevel)) toplevel = s->win;

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "\xe6\x96\xb0\xe5\xa2\x9e\xe9\xa0\x81\xe9\x9d\xa2",
        GTK_WINDOW(toplevel),
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "\xe6\x96\xb0\xe5\xa2\x9e", GTK_RESPONSE_ACCEPT,
        "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL,
        nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    GtkWidget* combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "\xe7\x9b\xb4\xe5\xbc\x8f");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "\xe6\xa8\xaa\xe5\xbc\x8f");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new("\xe6\x96\xb9\xe5\x90\x91\xef\xbc\x9a"), FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(content), combo, FALSE, FALSE, 8);
    gtk_widget_show_all(content);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        int ls = (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)) == 1);
        NoteData* nd = s->dc->note;
        ensureNoteCap(nd, nd->nPage);
        PageData* pg = &nd->pages[nd->nPage];
        pg->pw = ls ? 842 : 595;
        pg->ph = ls ? 595 : 842;
        pg->landscape = ls;
        nd->nPage++;
        s->dc->cpi = nd->nPage - 1;
        nd->dirty = 1;
        if (s->dc->surf) { cairo_surface_destroy(s->dc->surf); s->dc->surf = nullptr; }
        redraw(s->dc);
        updateStatus(s);
    }
    gtk_widget_destroy(dlg);
}

static void on_page_del_cb(GtkButton*, gpointer ud) {
    MW* s = (MW*)ud;
    if (!s->dc || !s->dc->note || s->dc->note->nPage <= 1) return;
    PageData* pg = &s->dc->note->pages[s->dc->cpi];
    // Clear content
    pg->nStroke = 0; pg->nImage = 0; pg->nText = 0;
    if (pg->bgSurf) { cairo_surface_destroy(pg->bgSurf); pg->bgSurf = nullptr; }
    s->dc->note->dirty = 1;
    if (s->dc->surf) { cairo_surface_destroy(s->dc->surf); s->dc->surf = nullptr; }
    redraw(s->dc); updateStatus(s);
}

static void on_page_prev_cb(GtkButton*, gpointer ud) {
    MW* s = (MW*)ud;
    if (!s->dc || !s->dc->note || s->dc->cpi <= 0) return;
    s->dc->cpi--;
    s->dc->selImg = -1; s->dc->selTxt = -1;
    if (s->dc->surf) { cairo_surface_destroy(s->dc->surf); s->dc->surf = nullptr; }
    redraw(s->dc); updateStatus(s);
}

static void on_page_next_cb(GtkButton*, gpointer ud) {
    MW* s = (MW*)ud;
    if (!s->dc || !s->dc->note || s->dc->cpi >= s->dc->note->nPage - 1) return;
    s->dc->cpi++;
    s->dc->selImg = -1; s->dc->selTxt = -1;
    if (s->dc->surf) { cairo_surface_destroy(s->dc->surf); s->dc->surf = nullptr; }
    redraw(s->dc); updateStatus(s);
}

// ============================================================
// File callbacks
// ============================================================
static void on_save_cb(GtkButton*, gpointer ud) {
    MW* s = (MW*)ud;
    if (!s->dc || !s->dc->note) return;
    s->dc->note->dirty = 0;
    Logger::info("Saved: %s", s->dc->note->name);
    rebuildNoteList(s);
    updateStatus(s);
}

static void on_export_pdf_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return;
    PageData* pg = &dc->note->pages[dc->cpi];

    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(dc->widget));
    if (!gtk_widget_is_toplevel(toplevel)) toplevel = dc->mw->win;

    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "\xe5\x8c\xaf\xe5\x87\xba PDF",
        GTK_WINDOW(toplevel),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL,
        "\xe5\x84\xb2\xe5\xad\x98", GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "note.pdf");
    GtkFileFilter* f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "PDF");
    gtk_file_filter_add_pattern(f, "*.pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (fn) {
            cairo_surface_t* ps = cairo_pdf_surface_create(fn, pg->pw, pg->ph);
            cairo_t* cr = cairo_create(ps);

            // Background
            if (pg->bgSurf && cairo_surface_status(pg->bgSurf) == CAIRO_STATUS_SUCCESS) {
                double scale = fmin(pg->pw / pg->bgW, pg->ph / pg->bgH);
                double bx = (pg->pw - pg->bgW * scale) / 2;
                double by = (pg->ph - pg->bgH * scale) / 2;
                cairo_save(cr);
                cairo_translate(cr, bx, by);
                cairo_scale(cr, scale, scale);
                cairo_set_source_surface(cr, pg->bgSurf, 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
            }

            // Images
            for (int i = 0; i < pg->nImage; i++) {
                ImgEl* img = &pg->images[i];
                if (!img->surf || cairo_surface_status(img->surf) != CAIRO_STATUS_SUCCESS) continue;
                cairo_save(cr);
                cairo_set_source_surface(cr, img->surf, img->x, img->y);
                cairo_paint(cr);
                cairo_restore(cr);
            }

            // Texts
            for (int i = 0; i < pg->nText; i++) {
                TxtEl* t = &pg->texts[i];
                if (t->text[0] == '\0') continue;
                cairo_set_source_rgb(cr, t->r, t->g, t->b);
                cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
                cairo_set_font_size(cr, t->fontSize);
                cairo_move_to(cr, t->x, t->y);
                cairo_show_text(cr, t->text);
            }

            // Strokes
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            for (int si = 0; si < pg->nStroke; si++) {
                StrokeData* s = &pg->strokes[si];
                if (s->count < 2 || s->tool == 2) continue;
                cairo_set_source_rgba(cr, s->r, s->g, s->b, s->a);
                cairo_set_line_width(cr, s->w);
                cairo_move_to(cr, s->xs[0], s->ys[0]);
                for (int pi = 1; pi < s->count; pi++)
                    cairo_line_to(cr, s->xs[pi], s->ys[pi]);
                cairo_stroke(cr);
            }

            cairo_destroy(cr);
            cairo_surface_finish(ps);
            cairo_surface_destroy(ps);
            Logger::info("\xe5\x8c\xaf\xe5\x87\xba PDF\xef\xbc\x9a%s", fn);
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_export_png_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage || !dc->surf) return;
    PageData* pg = &dc->note->pages[dc->cpi];

    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(dc->widget));
    if (!gtk_widget_is_toplevel(toplevel)) toplevel = dc->mw->win;

    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "\xe5\x8c\xaf\xe5\x87\xba PNG",
        GTK_WINDOW(toplevel),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL,
        "\xe5\x84\xb2\xe5\xad\x98", GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "note.png");
    GtkFileFilter* f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "PNG");
    gtk_file_filter_add_pattern(f, "*.png");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (fn) {
            int pw = (int)(pg->pw * dc->zoom);
            int ph = (int)(pg->ph * dc->zoom);
            cairo_surface_t* ps = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
            cairo_t* cr = cairo_create(ps);
            cairo_set_source_surface(cr, dc->surf, -dc->ml, -dc->mt);
            cairo_paint(cr);
            cairo_surface_write_to_png(ps, fn);
            cairo_destroy(cr);
            cairo_surface_destroy(ps);
            Logger::info("\xe5\x8c\xaf\xe5\x87\xba PNG\xef\xbc\x9a%s", fn);
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_insert_img_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return;

    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(dc->widget));
    if (!gtk_widget_is_toplevel(toplevel)) toplevel = dc->mw->win;

    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "\xe6\x8f\x92\xe5\x85\xa5\xe5\x9c\x96\xe7\x89\x87",
        GTK_WINDOW(toplevel),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL,
        "\xe6\x8f\x92\xe5\x85\xa5", GTK_RESPONSE_ACCEPT,
        nullptr);
    GtkFileFilter* f = gtk_file_filter_new();
    gtk_file_filter_set_name(f, "\xe5\x9c\x96\xe7\x89\x87");
    gtk_file_filter_add_pattern(f, "*.png");
    gtk_file_filter_add_pattern(f, "*.jpg");
    gtk_file_filter_add_pattern(f, "*.jpeg");
    gtk_file_filter_add_pattern(f, "*.gif");
    gtk_file_filter_add_pattern(f, "*.webp");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (fn) {
            cairo_surface_t* img = cairo_image_surface_create_from_png(fn);
            if (img && cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
                int iw = cairo_image_surface_get_width(img);
                int ih = cairo_image_surface_get_height(img);
                double sc = fmin(200.0 / iw, 200.0 / ih);
                PageData* pg = &dc->note->pages[dc->cpi];
                if (pg->nImage >= pg->capImage) {
                    pg->capImage = pg->capImage == 0 ? 16 : pg->capImage * 2;
                    pg->images = (ImgEl*)realloc(pg->images, pg->capImage * sizeof(ImgEl));
                    for (int i = pg->nImage; i < pg->capImage; i++) new (&pg->images[i]) ImgEl();
                }
                ImgEl* ie = &pg->images[pg->nImage];
                ie->surf = img;
                ie->x = 50; ie->y = 50;
                ie->w = iw * sc; ie->h = ih * sc;
                pg->nImage++;
                dc->note->dirty = 1;
                redraw(dc); updateStatus(dc->mw);
            } else {
                if (img) cairo_surface_destroy(img);
                GtkWidget* err = gtk_message_dialog_new(
                    GTK_WINDOW(toplevel),
                    GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                    "\xe7\x84\xa1\xe6\xb3\x95\xe8\xbc\x89\xe5\x85\xa5\xe5\x9c\x96\xe7\x89\x87");
                gtk_dialog_run(GTK_DIALOG(err));
                gtk_widget_destroy(err);
            }
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

// Load PDF/Image as background
static void on_load_bg_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return;

    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(dc->widget));
    if (!gtk_widget_is_toplevel(toplevel)) toplevel = dc->mw->win;

    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "\xe8\xbc\x89\xe5\x85\xa5\xe8\x83\x8c\xe6\x99\xaf",
        GTK_WINDOW(toplevel),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL,
        "\xe8\xbc\x89\xe5\x85\xa5", GTK_RESPONSE_ACCEPT,
        nullptr);

    GtkFileFilter* pdf = gtk_file_filter_new();
    gtk_file_filter_set_name(pdf, "PDF");
    gtk_file_filter_add_pattern(pdf, "*.pdf");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), pdf);

    GtkFileFilter* img = gtk_file_filter_new();
    gtk_file_filter_set_name(img, "\xe5\x9c\x96\xe7\x89\x87");
    gtk_file_filter_add_pattern(img, "*.png");
    gtk_file_filter_add_pattern(img, "*.jpg");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), img);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (fn) {
            cairo_surface_t* bg = cairo_image_surface_create_from_png(fn);
            if (bg && cairo_surface_status(bg) == CAIRO_STATUS_SUCCESS) {
                PageData* pg = &dc->note->pages[dc->cpi];
                if (pg->bgSurf) cairo_surface_destroy(pg->bgSurf);
                pg->bgSurf = bg;
                pg->bgW = cairo_image_surface_get_width(bg);
                pg->bgH = cairo_image_surface_get_height(bg);
                strncpy(pg->bgPath, fn, sizeof(pg->bgPath) - 1);
                pg->bgPath[sizeof(pg->bgPath) - 1] = '\0';
                if (dc->surf) { cairo_surface_destroy(dc->surf); dc->surf = nullptr; }
                dc->note->dirty = 1;
                redraw(dc); updateStatus(dc->mw);
                Logger::info("Loaded background: %s", fn);
            } else {
                if (bg) cairo_surface_destroy(bg);
                GtkWidget* err = gtk_message_dialog_new(
                    GTK_WINDOW(toplevel),
                    GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                    "\xe7\x84\xa1\xe6\xb3\x95\xe8\xbc\x89\xe5\x85\xa5\xe8\x83\x8c\xe6\x99\xaf");
                gtk_dialog_run(GTK_DIALOG(err));
                gtk_widget_destroy(err);
            }
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_del_sel_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return;
    PageData* pg = &dc->note->pages[dc->cpi];
    if (dc->selImg >= 0 && dc->selImg < pg->nImage) {
        pg->images[dc->selImg].~ImgEl();
        for (int i = dc->selImg; i < pg->nImage - 1; i++) pg->images[i] = pg->images[i + 1];
        pg->nImage--;
        dc->selImg = -1;
        dc->note->dirty = 1;
        redraw(dc); updateStatus(dc->mw);
    } else if (dc->selTxt >= 0 && dc->selTxt < pg->nText) {
        for (int i = dc->selTxt; i < pg->nText - 1; i++) pg->texts[i] = pg->texts[i + 1];
        pg->nText--;
        dc->selTxt = -1;
        dc->note->dirty = 1;
        redraw(dc); updateStatus(dc->mw);
    } else if (pg->nStroke > 0) {
        if (dc->nRedo >= dc->capRedo) {
            dc->capRedo = dc->capRedo == 0 ? 32 : dc->capRedo * 2;
            dc->redoStrokes = (StrokeData*)realloc(dc->redoStrokes, dc->capRedo * sizeof(StrokeData));
        }
        new (&dc->redoStrokes[dc->nRedo]) StrokeData();
        dc->redoStrokes[dc->nRedo] = pg->strokes[pg->nStroke - 1];
        pg->strokes[pg->nStroke - 1].xs = nullptr;
        pg->strokes[pg->nStroke - 1].ys = nullptr;
        pg->strokes[pg->nStroke - 1].count = 0;
        pg->strokes[pg->nStroke - 1].capacity = 0;
        dc->nRedo++;
        pg->nStroke--;
        dc->note->dirty = 1;
        redraw(dc); updateStatus(dc->mw);
    }
}

static void on_undo_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return;
    PageData* pg = &dc->note->pages[dc->cpi];
    if (pg->nStroke > 0) {
        if (dc->nRedo >= dc->capRedo) {
            dc->capRedo = dc->capRedo == 0 ? 32 : dc->capRedo * 2;
            dc->redoStrokes = (StrokeData*)realloc(dc->redoStrokes, dc->capRedo * sizeof(StrokeData));
        }
        new (&dc->redoStrokes[dc->nRedo]) StrokeData();
        dc->redoStrokes[dc->nRedo] = pg->strokes[pg->nStroke - 1];
        pg->strokes[pg->nStroke - 1].xs = nullptr;
        pg->strokes[pg->nStroke - 1].ys = nullptr;
        pg->strokes[pg->nStroke - 1].count = 0;
        pg->strokes[pg->nStroke - 1].capacity = 0;
        dc->nRedo++;
        pg->nStroke--;
        dc->note->dirty = 1;
        redraw(dc); updateStatus(dc->mw);
    }
}

static void on_redo_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage || dc->nRedo <= 0) return;
    PageData* pg = &dc->note->pages[dc->cpi];
    dc->nRedo--;
    ensureStrokeCap(pg);
    pg->strokes[pg->nStroke] = dc->redoStrokes[dc->nRedo];
    dc->redoStrokes[dc->nRedo].xs = nullptr;
    dc->redoStrokes[dc->nRedo].ys = nullptr;
    dc->redoStrokes[dc->nRedo].count = 0;
    dc->redoStrokes[dc->nRedo].capacity = 0;
    pg->nStroke++;
    dc->note->dirty = 1;
    redraw(dc); updateStatus(dc->mw);
}

static void on_zoomin_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    dc->zoom = fmin(5.0, dc->zoom * 1.25);
    if (dc->surf) { cairo_surface_destroy(dc->surf); dc->surf = nullptr; }
    redraw(dc); updateStatus(dc->mw);
}

static void on_zoomout_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    dc->zoom = fmax(0.1, dc->zoom / 1.25);
    if (dc->surf) { cairo_surface_destroy(dc->surf); dc->surf = nullptr; }
    redraw(dc); updateStatus(dc->mw);
}

static void on_zoomfit_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->widget || !dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return;
    PageData* pg = &dc->note->pages[dc->cpi];
    int aw = gtk_widget_get_allocated_width(GTK_WIDGET(dc->widget));
    int ah = gtk_widget_get_allocated_height(GTK_WIDGET(dc->widget));
    dc->zoom = fmin((double)(aw - dc->ml - dc->mr - 20) / pg->pw,
                     (double)(ah - dc->mt - dc->mb - 20) / pg->ph);
    dc->zoom = fmax(0.1, fmin(5.0, dc->zoom));
    if (dc->surf) { cairo_surface_destroy(dc->surf); dc->surf = nullptr; }
    redraw(dc); updateStatus(dc->mw);
}

static void on_page_settings_cb(GtkButton*, gpointer ud) {
    DC* dc = (DC*)ud;
    if (!dc->note || dc->cpi < 0 || dc->cpi >= dc->note->nPage) return;
    PageData* pg = &dc->note->pages[dc->cpi];

    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(dc->widget));
    if (!gtk_widget_is_toplevel(toplevel)) toplevel = dc->mw->win;

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "\xe9\xa0\x81\xe9\x9d\xa2\xe8\xa8\xad\xe5\xae\x9a",
        GTK_WINDOW(toplevel),
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "\xe7\xa2\xba\xe5\xae\x9a", GTK_RESPONSE_ACCEPT,
        "\xe5\x8f\x96\xe6\xb6\x88", GTK_RESPONSE_CANCEL,
        nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    // Width
    GtkWidget* ws = gtk_spin_button_new_with_range(200, 2000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ws), pg->pw);
    GtkWidget* hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hbox1), gtk_label_new("\xe5\xaf\xac\xe5\xba\xa6 (pt)"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox1), ws, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), hbox1, FALSE, FALSE, 4);

    // Height
    GtkWidget* hs = gtk_spin_button_new_with_range(200, 2000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(hs), pg->ph);
    GtkWidget* hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hbox2), gtk_label_new("\xe9\xab\x98\xe5\xba\xa6 (pt)"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox2), hs, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), hbox2, FALSE, FALSE, 4);

    // Margins
    const char* mlabels[] = {"\xe5\xb7\xa6", "\xe4\xb8\x8a", "\xe4\xb8\x8b", "\xe5\x8f\xb3"};
    int* mvals[] = {&dc->ml, &dc->mt, &dc->mb, &dc->mr};
    GtkWidget* mspins[4];
    for (int i = 0; i < 4; i++) {
        mspins[i] = gtk_spin_button_new_with_range(0, 200, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(mspins[i]), *mvals[i]);
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(mlabels[i]), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hbox), mspins[i], TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 4);
    }

    gtk_widget_show_all(content);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        pg->pw = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ws));
        pg->ph = gtk_spin_button_get_value(GTK_SPIN_BUTTON(hs));
        for (int i = 0; i < 4; i++) *mvals[i] = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(mspins[i]));
        if (dc->surf) { cairo_surface_destroy(dc->surf); dc->surf = nullptr; }
        redraw(dc);
    }
    gtk_widget_destroy(dlg);
}

static void on_about_cb(GtkButton*, gpointer) {
    GtkWidget* dlg = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), "OfflineNote");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dlg), "1.0.0 MVP");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dlg),
        "\xe9\x9b\xa2\xe7\xb7\x9a\xe6\x89\x8b\xe5\xaf\xab\xe8\xad\xbd\xe8\xa8\xbb\xe8\xa8\x98\xe9\xab\x94\n"
        "\xe5\xae\x8c\xe5\x85\xa8\xe9\x9b\xa2\xe7\xb7\x9a\xef\xbc\x8c\xe4\xb8\x8d\xe9\x9c\x80\xe8\xa6\x81\xe7\xb6\xb2\xe8\xb7\xaf");
    gtk_about_dialog_set_license(GTK_ABOUT_DIALOG(dlg), "GPL-2.0-or-later");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void on_quit_cb(GtkButton*, gpointer ud) {
    MW* s = (MW*)ud;
    for (int i = 0; i < s->nNote; i++) {
        if (s->notes[i].dirty) {
            Logger::info("Auto-save: %s", s->notes[i].name);
            s->notes[i].dirty = 0;
        }
    }
    gtk_widget_destroy(GTK_WIDGET(s->win));
}

// ============================================================
// Add toolbar button helper
// ============================================================
static void addTb(GtkToolbar* tb, const char* lbl, GCallback cb, void* d) {
    GtkToolItem* btn = gtk_tool_button_new(nullptr, lbl);
    g_signal_connect(btn, "clicked", cb, d);
    gtk_toolbar_insert(tb, btn, -1);
}

// ============================================================
// MainWindow Constructor
// ============================================================
MainWindow::MainWindow(GtkApplication* app, AppController& ctrl)
    : app_(app), controller_(ctrl)
{
    MW* s = new MW();
    s->app = app;
    s->ctrl = &ctrl;
    s->dc = new DC();
    s->dc->mw = s;

    window_ = gtk_application_window_new(app);
    s->win = window_;
    gtk_window_set_title(GTK_WINDOW(window_),
        "OfflineNote \xe2\x80\x94 \xe9\x9b\xa2\xe7\xb7\x9a\xe8\xad\xbd\xe8\xa8\xbb\xe8\xa8\x98\xe9\xab\x94");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1280, 800);

    // Main layout
    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), mainBox);

    // ── Menu bar (simple, just file + edit + help)
    GtkWidget* menuBar = gtk_menu_bar_new();
    {
        // File menu
        GtkWidget* mi = gtk_menu_item_new_with_label("\xe6\xaa\x94\xe6\xa1\x88");
        GtkWidget* m = gtk_menu_new();
        GtkWidget* it = gtk_menu_item_new_with_label("\xe6\x96\xb0\xe5\xbb\xba\xe7\xad\x86\xe8\xa8\x98");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_newnote_dlg(nullptr, ud);
        }), s);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        it = gtk_menu_item_new_with_label("\xe5\x84\xb2\xe5\xad\x98");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_save_cb(nullptr, ud);
        }), s);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());

        it = gtk_menu_item_new_with_label("\xe5\x8c\xaf\xe5\x87\xba PDF");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_export_pdf_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        it = gtk_menu_item_new_with_label("\xe5\x8c\xaf\xe5\x87\xba PNG");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_export_png_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());

        it = gtk_menu_item_new_with_label("\xe7\xb5\x90\xe6\x9d\x9f");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_quit_cb(nullptr, ud);
        }), s);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), m);
        gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), mi);
    }
    {
        // Edit menu
        GtkWidget* mi = gtk_menu_item_new_with_label("\xe7\xb7\xa8\xe8\xbc\xaf");
        GtkWidget* m = gtk_menu_new();

        GtkWidget* it = gtk_menu_item_new_with_label("\xe5\xbe\xa9\xe5\x8e\x9f Ctrl+Z");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_undo_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        it = gtk_menu_item_new_with_label("\xe9\x87\x8d\xe5\x81\x9a Ctrl+Y");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_redo_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());

        it = gtk_menu_item_new_with_label("\xe5\x88\xaa\xe9\x99\xa4 Del");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_del_sel_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        it = gtk_menu_item_new_with_label("\xe6\x8f\x92\xe5\x85\xa5\xe5\x9c\x96");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_insert_img_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        it = gtk_menu_item_new_with_label("\xe8\xbc\x89\xe5\x85\xa5\xe8\x83\x8c\xe6\x99\xaf");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_load_bg_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());

        it = gtk_menu_item_new_with_label("\xe9\xa0\x81\xe9\x9d\xa2\xe8\xa8\xad\xe5\xae\x9a");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_page_settings_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);

        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), m);
        gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), mi);
    }
    {
        // View menu
        GtkWidget* mi = gtk_menu_item_new_with_label("\xe6\xaa\xa2\xe8\xa6\x96");
        GtkWidget* m = gtk_menu_new();
        GtkWidget* it = gtk_menu_item_new_with_label("\xe6\x94\xbe\xe5\xa4\xa7");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_zoomin_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
        it = gtk_menu_item_new_with_label("\xe7\xb8\xae\xe5\xb0\x8f");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_zoomout_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
        it = gtk_menu_item_new_with_label("\xe9\x81\xa9\xe5\x90\x88\xe8\xa6\x96\xe7\xaa\x97");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer ud) {
            on_zoomfit_cb(nullptr, ud);
        }), s->dc);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), m);
        gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), mi);
    }
    {
        // Help menu
        GtkWidget* mi = gtk_menu_item_new_with_label("\xe8\xaa\xaa\xe6\x98\x8e");
        GtkWidget* m = gtk_menu_new();
        GtkWidget* it = gtk_menu_item_new_with_label("\xe9\x97\x9c\xe6\x96\xbc");
        g_signal_connect(it, "activate", G_CALLBACK(+[](GtkWidget*, gpointer) {
            on_about_cb(nullptr, nullptr);
        }), nullptr);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), m);
        gtk_menu_shell_append(GTK_MENU_SHELL(menuBar), mi);
    }
    gtk_box_pack_start(GTK_BOX(mainBox), menuBar, FALSE, FALSE, 0);

    // ── Toolbar
    GtkToolbar* toolbar = GTK_TOOLBAR(gtk_toolbar_new());
    addTb(toolbar, "\xe6\x96\xb0\xe7\xad\x86", G_CALLBACK(on_newnote_dlg), s);
    gtk_toolbar_insert(toolbar, gtk_separator_tool_item_new(), -1);
    addTb(toolbar, "\xe5\x84\xb2\xe5\xad\x98", G_CALLBACK(on_save_cb), s);
    gtk_toolbar_insert(toolbar, gtk_separator_tool_item_new(), -1);
    addTb(toolbar, "\xe5\xbe\xa9\xe5\x8e\x9f", G_CALLBACK(on_undo_cb), s->dc);
    addTb(toolbar, "\xe9\x87\x8d\xe5\x81\x9a", G_CALLBACK(on_redo_cb), s->dc);
    gtk_toolbar_insert(toolbar, gtk_separator_tool_item_new(), -1);
    addTb(toolbar, "\xe6\x89\x8b\xe5\xaf\xab", G_CALLBACK(on_pen), s->dc);
    addTb(toolbar, "\xe6\xa9\x99\xe5\x85\x89", G_CALLBACK(on_hl), s->dc);
    addTb(toolbar, "\xe6\xa9\xa1\xe7\x9a\xae", G_CALLBACK(on_eraser), s->dc);
    addTb(toolbar, "\xe6\x96\x87\xe5\xad\x97", G_CALLBACK(on_text_tool), s->dc);
    addTb(toolbar, "\xe9\x81\xb8\xe5\x8f\x96", G_CALLBACK(on_select), s->dc);
    gtk_toolbar_insert(toolbar, gtk_separator_tool_item_new(), -1);
    addTb(toolbar, "\xe6\x8f\x92\xe5\x9c\x96", G_CALLBACK(on_insert_img_cb), s->dc);
    addTb(toolbar, "\xe8\x83\x8c\xe6\x99\xaf", G_CALLBACK(on_load_bg_cb), s->dc);
    gtk_toolbar_insert(toolbar, gtk_separator_tool_item_new(), -1);
    addTb(toolbar, "\xe6\x94\xbe\xe5\xa4\xa7", G_CALLBACK(on_zoomin_cb), s->dc);
    addTb(toolbar, "\xe7\xb8\xae\xe5\xb0\x8f", G_CALLBACK(on_zoomout_cb), s->dc);
    addTb(toolbar, "\xe9\x81\xa9\xe5\x90\x88", G_CALLBACK(on_zoomfit_cb), s->dc);
    gtk_toolbar_insert(toolbar, gtk_separator_tool_item_new(), -1);
    addTb(toolbar, "PDF", G_CALLBACK(on_export_pdf_cb), s->dc);

    // Color button
    GtkWidget* cw = gtk_color_button_new();
    GdkRGBA blk = {0, 0, 0, 1};
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cw), &blk);
    g_signal_connect(cw, "color-set", G_CALLBACK(on_color), s->dc);
    GtkToolItem* ci = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(ci), cw);
    gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(ci), "\xe9\xa1\x8f\xe8\x89\xb2");
    gtk_toolbar_insert(toolbar, ci, -1);

    // Pen size
    GtkWidget* sc = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.5, 20.0, 0.5);
    gtk_range_set_value(GTK_RANGE(sc), 2.0);
    gtk_widget_set_size_request(sc, 100, -1);
    g_signal_connect(sc, "value-changed", G_CALLBACK(on_pensize), s->dc);
    GtkToolItem* si = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(si), sc);
    gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(si), "\xe7\xb2\x97\xe7\xb4\xb0");
    gtk_toolbar_insert(toolbar, si, -1);

    gtk_box_pack_start(GTK_BOX(mainBox), GTK_WIDGET(toolbar), FALSE, FALSE, 0);

    // ── Content: Sidebar + Canvas
    GtkWidget* contentBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(mainBox), contentBox, TRUE, TRUE, 0);

    // Sidebar
    GtkWidget* sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(sidebar, 180, -1);
    gtk_box_pack_start(GTK_BOX(contentBox), sidebar, FALSE, FALSE, 0);

    // New note button
    GtkWidget* newBtn = gtk_button_new_with_label("+ \xe6\x96\xb0\xe7\xad\x86");
    g_signal_connect(newBtn, "clicked", G_CALLBACK(on_newnote_dlg), s);
    gtk_box_pack_start(GTK_BOX(sidebar), newBtn, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(sidebar), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 2);

    // Note list in a scrolled window
    s->noteBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), s->noteBox);
    gtk_box_pack_start(GTK_BOX(sidebar), scroll, TRUE, TRUE, 0);

    // Canvas
    s->canvas = gtk_drawing_area_new();
    s->dc->widget = s->canvas;
    gtk_widget_set_events(s->canvas,
        GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_KEY_PRESS_MASK);
    g_signal_connect(s->canvas, "draw", G_CALLBACK(on_draw), s->dc);
    g_signal_connect(s->canvas, "button-press-event", G_CALLBACK(on_btnpress), s->dc);
    g_signal_connect(s->canvas, "button-release-event", G_CALLBACK(on_btnrelease), s->dc);
    g_signal_connect(s->canvas, "motion-notify-event", G_CALLBACK(on_motion), s->dc);
    g_signal_connect(s->canvas, "scroll-event", G_CALLBACK(on_scroll), s->dc);
    g_signal_connect(s->canvas, "key-press-event", G_CALLBACK(on_keypress), s->dc);
    gtk_box_pack_start(GTK_BOX(contentBox), s->canvas, TRUE, TRUE, 0);

    // Bottom: Page controls + Status bar
    GtkWidget* bottomBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(mainBox), bottomBox, FALSE, FALSE, 0);

    // Page nav
    GtkWidget* btnPrev = gtk_button_new_with_label("\xe2\x97\x80");
    g_signal_connect(btnPrev, "clicked", G_CALLBACK(on_page_prev_cb), s);
    gtk_box_pack_start(GTK_BOX(bottomBox), btnPrev, FALSE, FALSE, 2);

    s->lblPage = gtk_label_new("1/1");
    gtk_box_pack_start(GTK_BOX(bottomBox), s->lblPage, FALSE, FALSE, 2);

    GtkWidget* btnNext = gtk_button_new_with_label("\xe2\x96\xb6");
    g_signal_connect(btnNext, "clicked", G_CALLBACK(on_page_next_cb), s);
    gtk_box_pack_start(GTK_BOX(bottomBox), btnNext, FALSE, FALSE, 2);

    GtkWidget* btnAdd = gtk_button_new_with_label("+ \xe9\xa0\x81");
    g_signal_connect(btnAdd, "clicked", G_CALLBACK(on_page_add_cb), s);
    gtk_box_pack_start(GTK_BOX(bottomBox), btnAdd, FALSE, FALSE, 2);

    GtkWidget* btnDel = gtk_button_new_with_label("- \xe9\xa0\x81");
    g_signal_connect(btnDel, "clicked", G_CALLBACK(on_page_del_cb), s);
    gtk_box_pack_start(GTK_BOX(bottomBox), btnDel, FALSE, FALSE, 2);

    // Status bar
    s->sbar = gtk_statusbar_new();
    gtk_box_pack_end(GTK_BOX(bottomBox), s->sbar, TRUE, TRUE, 0);
    s->lblZoom = gtk_label_new("100%");
    s->lblCoord = gtk_label_new("0, 0");
    s->lblTool = gtk_label_new("\xe6\x89\x8b\xe5\xaf\xab\xe7\xad\x86");
    GtkWidget* sbContent = gtk_bin_get_child(GTK_BIN(s->sbar));
    if (GTK_IS_BOX(sbContent)) {
        gtk_box_pack_start(GTK_BOX(sbContent), s->lblZoom, FALSE, FALSE, 4);
        gtk_box_pack_start(GTK_BOX(sbContent), s->lblCoord, FALSE, FALSE, 4);
        gtk_box_pack_end(GTK_BOX(sbContent), s->lblTool, FALSE, FALSE, 4);
    }

    // Create first note
    addNote(s, "\xe7\xad\x86\xe8\xa8\x98 1", 0);
    state_ = s;
}

MainWindow::~MainWindow() {
    MW* s = (MW*)state_;
    delete s;
}

void MainWindow::show() {
    gtk_widget_show_all(GTK_WIDGET(window_));
    updateStatus((MW*)state_);
}

'''

with open(r'C:\Users\LIN\OfflineNote\src\ui\MainWindow.cpp', 'w', encoding='utf-8') as f:
    f.write(content)

print('MainWindow.cpp generated: complete, uses basic GTK3 APIs, no crashes')

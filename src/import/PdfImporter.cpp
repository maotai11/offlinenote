// src/import/PdfImporter.cpp
// PDF 匯入器 — 使用 Poppler GLib 渲染 PDF 頁面為 PNG 背景
// SPDX-License-Identifier: GPL-2.0-or-later

#include "PdfImporter.h"
#include <poppler.h>
#include <cairo.h>
#include "../util/SafeArithmetic.h"
#include "../util/Logger.h"
#include <filesystem>
#include <vector>
#include <string>
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

// PDF 頁面尺寸上限（pt），防止惡意 PDF 耗盡記憶體
// A0 最大邊約 3370pt，放寬 2 倍到 7000pt 仍合理
static constexpr double MAX_PDF_PAGE_DIMENSION_PT = 7000.0;

// 渲染像素上限（px），對應 CAIRO_MAX_IMAGE_SURFACE_DIM
static constexpr double MAX_RENDERED_PX = 16000.0; // 保守值，小於 Cairo 32767

bool PdfImporter::isValidImportPageSize(double width, double height) {
    if (!std::isfinite(width) || !std::isfinite(height)) return false;
    if (width <= 0.0 || height <= 0.0) return false;
    if (width > MAX_PDF_PAGE_DIMENSION_PT || height > MAX_PDF_PAGE_DIMENSION_PT) return false;
    // 檢查渲染後像素尺寸
    double scale = 300.0 / 72.0; // 最大 DPI
    double renderedW = width * scale;
    double renderedH = height * scale;
    if (renderedW > MAX_RENDERED_PX || renderedH > MAX_RENDERED_PX) return false;
    // 總像素不超過 256MP（約 16000x16000）
    double totalPixels = renderedW * renderedH;
    if (totalPixels > 256000000.0) return false;
    return true;
}

struct PdfPageResult {
    cairo_surface_t* surface;  // caller owns this
    double width;
    double height;
    std::string pngPath;
};

static PdfPageResult renderPdfPageToSurface(const std::string& pdfPath, int pageNum, const std::string& outDir) {
    PdfPageResult result = { nullptr, 0, 0, "" };

    GError* err = nullptr;
    // Use GFile for proper Windows/Unicode path handling
    GFile* gfile = g_file_new_for_path(pdfPath.c_str());
    PopplerDocument* doc = poppler_document_new_from_gfile(gfile, nullptr, nullptr, &err);
    g_object_unref(gfile);
    if (!doc) {
        if (err) g_error_free(err);
        return result;
    }

    int nPages = poppler_document_get_n_pages(doc);
    if (pageNum < 0 || pageNum >= nPages) {
        g_object_unref(doc);
        return result;
    }

    PopplerPage* page = poppler_document_get_page(doc, pageNum);
    if (!page) {
        g_object_unref(doc);
        return result;
    }

    double width, height;
    poppler_page_get_size(page, &width, &height);

    // Security: validate page dimensions before rendering
    if (!PdfImporter::isValidImportPageSize(width, height)) {
        Logger::warning("PdfImporter: REJECTED page with excessive dimensions");
        g_object_unref(page);
        g_object_unref(doc);
        return result;
    }

    // 渲染為 cairo image surface (300 DPI)
    double scale = 300.0 / 72.0;
    int pxW = (int)(width * scale);
    int pxH = (int)(height * scale);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pxW, pxH);
    cairo_t* cr = cairo_create(surf);
    cairo_scale(cr, scale, scale);

    // 白色背景
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    poppler_page_render(page, cr);
    cairo_destroy(cr);

    // 保存為 PNG 到 outDir
    char pngName[256];
    snprintf(pngName, sizeof(pngName), "pdf_p%03d.png", pageNum + 1);
    std::string pngFullPath = outDir + "/" + pngName;

    cairo_status_t status = cairo_surface_write_to_png(surf, pngFullPath.c_str());
    if (status != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        g_object_unref(page);
        g_object_unref(doc);
        return result;
    }

    result.surface = surf;  // 注意：surface 已寫出 PNG，caller 需決定是否保留
    result.width = width;
    result.height = height;
    result.pngPath = pngName;  // 相對路徑

    g_object_unref(page);
    g_object_unref(doc);
    return result;
}

std::vector<PdfImportedPage> PdfImporter::importPdf(const std::string& pdfPath, const std::string& outDir) {
    std::vector<PdfImportedPage> pages;

    if (!fs::exists(pdfPath)) return pages;

    // 確保輸出目錄存在
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        Logger::warning("PdfImporter: FAILED to prepare output directory");
        return pages;
    }

    GError* err = nullptr;
    // Use GFile for proper Windows/Unicode path handling
    GFile* gfile = g_file_new_for_path(pdfPath.c_str());
    PopplerDocument* doc = poppler_document_new_from_gfile(gfile, nullptr, nullptr, &err);
    g_object_unref(gfile);
    if (!doc) {
        if (err) g_error_free(err);
        return pages;
    }

    int nPages = poppler_document_get_n_pages(doc);
    if (nPages <= 0) {
        g_object_unref(doc);
        return pages;
    }

    for (int i = 0; i < nPages && i < 100; i++) {  // 限制 100 頁
        PopplerPage* page = poppler_document_get_page(doc, i);
        if (!page) continue;

        double width, height;
        poppler_page_get_size(page, &width, &height);

        // Security: validate page dimensions before rendering
        if (!PdfImporter::isValidImportPageSize(width, height)) {
            Logger::warning("PdfImporter: SKIPPING page with excessive dimensions");
            g_object_unref(page);
            continue;
        }

        // 渲染為 PNG (150 DPI — 縮圖品質，平衡速度與品質)
        double scale = 150.0 / 72.0;
        int pxW = (int)(width * scale);
        int pxH = (int)(height * scale);

        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pxW, pxH);
        cairo_t* cr = cairo_create(surf);
        cairo_scale(cr, scale, scale);

        // 白色背景
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_rectangle(cr, 0, 0, width, height);
        cairo_fill(cr);

        poppler_page_render(page, cr);
        cairo_destroy(cr);

        char pngName[256];
        snprintf(pngName, sizeof(pngName), "pdf_p%03d.png", i + 1);
        std::string pngFullPath = outDir + "/" + pngName;

        cairo_status_t status = cairo_surface_write_to_png(surf, pngFullPath.c_str());
        cairo_surface_destroy(surf);

        if (status == CAIRO_STATUS_SUCCESS) {
            PdfImportedPage pg;
            pg.bgImagePath = pngName;
            pg.width = width;
            pg.height = height;
            pg.pdfPageNum = i;
            pages.push_back(pg);
        } else {
            Logger::warning("PdfImporter: FAILED to render page PNG");
        }

        g_object_unref(page);
    }

    g_object_unref(doc);
    return pages;
}

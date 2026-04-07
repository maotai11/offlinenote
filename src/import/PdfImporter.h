// src/import/PdfImporter.h
// PDF 匯入器標頭
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <vector>

struct PdfImportedPage {
    std::string bgImagePath;  // PNG 背景路徑（相對）
    double width;
    double height;
    int pdfPageNum;           // 原始 PDF 頁碼
};

class PdfImporter {
public:
    // 匯入 PDF，將每頁渲染為 PNG 到 outDir
    // 返回每頁的資訊（路徑、尺寸、頁碼）
    static std::vector<PdfImportedPage> importPdf(const std::string& pdfPath, const std::string& outDir);
};

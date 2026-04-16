// src/import/PdfImporter.h
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <vector>

struct PdfImportedPage {
    std::string bgImagePath;
    double width;
    double height;
    int pdfPageNum;
};

class PdfImporter {
public:
    static bool isValidImportPageSize(double widthPt, double heightPt);
    static std::vector<PdfImportedPage> importPdf(const std::string& pdfPath, const std::string& outDir);
};

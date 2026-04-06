// src/document/DocumentMetadata.h
// 文件元資料
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <ctime>

struct DocumentMetadata {
    std::string title;
    std::string author;
    std::time_t createdAt = 0;
    std::time_t modifiedAt = 0;
    std::string formatVersion = "1";

    DocumentMetadata() {
        createdAt = modifiedAt = std::time(nullptr);
    }

    void setTitle(const std::string& t) { title = t; }
    void setAuthor(const std::string& a) { author = a; }
    void markModified() { modifiedAt = std::time(nullptr); }
};

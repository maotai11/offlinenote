// src/document/PdfBackground.h
// PDF 頁面作為背景的描述
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>
#include <vector>
#include <string>

enum class EmbedMode {
    ExternalLink,  // 只儲存路徑
    Embedded       // 內嵌 PDF 資料（Phase 2）
};

class PdfBackground {
public:
    PdfBackground() = default;

    // ExternalLink 模式
    explicit PdfBackground(std::filesystem::path externalPath, int pageIndex)
        : embedMode_(EmbedMode::ExternalLink),
          externalPath_(std::move(externalPath)),
          pageIndex_(pageIndex) {}

    // Embedded 模式（Phase 2）
    PdfBackground(std::vector<uint8_t> embeddedData, int pageIndex)
        : embedMode_(EmbedMode::Embedded),
          embeddedData_(std::move(embeddedData)),
          pageIndex_(pageIndex) {}

    EmbedMode embedMode() const { return embedMode_; }

    const std::filesystem::path& externalPath() const { return externalPath_; }
    const std::vector<uint8_t>& embeddedData() const { return embeddedData_; }
    int pageIndex() const { return pageIndex_; }

    bool hasExternalPath() const { return embedMode_ == EmbedMode::ExternalLink && !externalPath_.empty(); }
    bool isEmbedded() const { return embedMode_ == EmbedMode::Embedded && !embeddedData_.empty(); }

private:
    EmbedMode embedMode_ = EmbedMode::ExternalLink;
    std::filesystem::path externalPath_;
    std::vector<uint8_t> embeddedData_;
    int pageIndex_ = 0;
};

// src/serialization/SafeDecompressor.cpp
// 安全解壓縮器 — 防護 zip bomb、壓縮炸彈
// SPDX-License-Identifier: GPL-2.0-or-later

#include "SafeDecompressor.h"
#include <zlib.h>
#include <fstream>
#include <limits>

// 安全限制
static constexpr size_t MAX_COMPRESSED_SIZE = 50 * 1024 * 1024;   // 50 MB 壓縮檔
static constexpr size_t MAX_DECOMPRESSED_SIZE = 500 * 1024 * 1024; // 500 MB 解壓縮後
static constexpr double MAX_COMPRESSION_RATIO = 100.0;             // 最大壓縮比 100:1

DecompressResult SafeDecompressor::decompress(const std::filesystem::path& path) {
    DecompressResult result;

    // 檢查檔案存在
    if (!std::filesystem::exists(path)) {
        result.errorMessage = "File does not exist";
        return result;
    }

    // 檢查壓縮檔大小
    auto fsize = std::filesystem::file_size(path);
    if (fsize > MAX_COMPRESSED_SIZE) {
        result.errorMessage = "Compressed file too large (zip bomb suspected)";
        return result;
    }

    // 讀取壓縮資料
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        result.errorMessage = "Cannot open file";
        return result;
    }

    std::streamsize compressedSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    // Reject empty files
    if (compressedSize <= 0) {
        result.errorMessage = "File is empty";
        return result;
    }

    std::vector<char> compressed(compressedSize);
    if (!ifs.read(compressed.data(), compressedSize)) {
        result.errorMessage = "Failed to read compressed data";
        return result;
    }
    ifs.close();

    // 使用 zlib 解壓縮，帶增量大小檢查
    z_stream strm = {};
    strm.avail_in = compressedSize;
    strm.next_in = reinterpret_cast<Bytef*>(compressed.data());

    // Initialize inflate with gzip support (windowBits 32 = auto-detect gzip or zlib)
    if (inflateInit2(&strm, 32 + 15) != Z_OK) {
        result.errorMessage = "Failed to initialize decompressor";
        return result;
    }

    // 增量解壓縮，每一步檢查大小
    std::vector<char> output;
    output.reserve(std::min<size_t>(compressedSize * 10, MAX_DECOMPRESSED_SIZE));

    char buf[32768];
    int ret;
    int iterations = 0;
    constexpr int MAX_ITERATIONS = 100000; // Safety limit
    do {
        iterations++;
        if (iterations > MAX_ITERATIONS) {
            inflateEnd(&strm);
            result.errorMessage = "Decompression iteration limit exceeded";
            return result;
        }
        strm.avail_out = sizeof(buf);
        strm.next_out = reinterpret_cast<Bytef*>(buf);

        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR || ret == Z_BUF_ERROR) {
            inflateEnd(&strm);
            result.errorMessage = "Decompression error (code " + std::to_string(ret) + ")";
            return result;
        }

        size_t got = sizeof(buf) - strm.avail_out;
        output.insert(output.end(), buf, buf + got);

        // Zip bomb 檢查 1: 解壓縮後大小限制
        if (output.size() > MAX_DECOMPRESSED_SIZE) {
            inflateEnd(&strm);
            result.errorMessage = "Decompressed data too large (zip bomb suspected)";
            return result;
        }

        // Zip bomb 檢查 2: 壓縮比限制
        if (output.size() > compressedSize * MAX_COMPRESSION_RATIO) {
            inflateEnd(&strm);
            result.errorMessage = "Compression ratio exceeded (zip bomb suspected)";
            return result;
        }
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);

    result.data = std::move(output);
    result.success = true;
    return result;
}

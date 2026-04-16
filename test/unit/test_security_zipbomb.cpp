// test/unit/test_security_zipbomb.cpp
#include "../catch_amalgamated.hpp"
#include "../../src/serialization/SafeDecompressor.h"
#include "../../src/util/SafeArithmetic.h"
#include "../../src/import/PdfImporter.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace fs = std::filesystem;

static std::vector<unsigned char> deflatePayload(const std::string& input,
                                                 int windowBits,
                                                 const Bytef* dictionary = nullptr,
                                                 uInt dictionarySize = 0) {
    z_stream stream = {};
    REQUIRE(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) == Z_OK);

    if (dictionary != nullptr && dictionarySize > 0) {
        REQUIRE(deflateSetDictionary(&stream, dictionary, dictionarySize) == Z_OK);
    }

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());

    std::vector<unsigned char> output;
    unsigned char buffer[256];

    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        ret = deflate(&stream, Z_FINISH);
        REQUIRE(ret == Z_OK || ret == Z_STREAM_END);

        const size_t produced = sizeof(buffer) - stream.avail_out;
        output.insert(output.end(), buffer, buffer + produced);
    }

    REQUIRE(deflateEnd(&stream) == Z_OK);
    return output;
}

TEST_CASE("SafeDecompressor rejects empty file", "[security][decompress]") {
    const fs::path testPath = fs::current_path() / "test_empty.gz";
    std::ofstream ofs(testPath, std::ios::binary);
    ofs.close();

    auto result = SafeDecompressor::decompress(testPath);
    REQUIRE(!result.success);
}

TEST_CASE("SafeDecompressor rejects invalid data", "[security][decompress]") {
    const fs::path testPath = fs::current_path() / "test_invalid.gz";
    std::ofstream ofs(testPath, std::ios::binary);
    const char garbage[] = "\xDE\xAD\xBE\xEF\x00\x01\x02\x03";
    ofs.write(garbage, sizeof(garbage) - 1);
    ofs.close();

    auto result = SafeDecompressor::decompress(testPath);
    REQUIRE(!result.success);
}

TEST_CASE("SafeDecompressor handles valid small gzip", "[security][decompress]") {
    const fs::path testPath = fs::current_path() / "test_valid.gz";
    const auto gzipData = deflatePayload("Hello\n", 16 + MAX_WBITS);

    std::ofstream ofs(testPath, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(gzipData.data()), static_cast<std::streamsize>(gzipData.size()));
    ofs.close();

    auto result = SafeDecompressor::decompress(testPath);
    REQUIRE(result.success);
    REQUIRE(std::string(result.data.begin(), result.data.end()) == "Hello\n");
}

TEST_CASE("SafeDecompressor rejects dictionary-compressed stream", "[security][decompress]") {
    const fs::path testPath = fs::current_path() / "test_dict.z";
    static const unsigned char dictionary[] = "common-prefix-for-dictionary";
    const auto compressed = deflatePayload("common-prefix-for-dictionary + payload", MAX_WBITS, dictionary, sizeof(dictionary) - 1);

    std::ofstream ofs(testPath, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
    ofs.close();

    auto result = SafeDecompressor::decompress(testPath);
    REQUIRE(!result.success);
    REQUIRE(result.errorMessage.find("dictionary") != std::string::npos);
}

TEST_CASE("PdfImporter enforces import page size limits", "[security][pdf]") {
    REQUIRE(CAIRO_MAX_IMAGE_SURFACE_DIM == 32767.0);
    REQUIRE(MAX_PAGE_COORDINATE_PT == 5000.0);

    REQUIRE(PdfImporter::isValidImportPageSize(3370.0, 2384.0));
    REQUIRE(!PdfImporter::isValidImportPageSize(7001.0, 100.0));
    REQUIRE(!PdfImporter::isValidImportPageSize(4000.0, 4000.0));
    REQUIRE(!PdfImporter::isValidImportPageSize(std::nan(""), 100.0));
}

TEST_CASE("Color from hex string parsing", "[security][color]") {
    Color c1 = Color::fromHexString("#FF0000");
    REQUIRE(c1.r == 0xFF);
    REQUIRE(c1.g == 0x00);
    REQUIRE(c1.b == 0x00);
    REQUIRE(c1.a == 255);

    Color c2 = Color::fromHexString("#00FF0080");
    REQUIRE(c2.r == 0x00);
    REQUIRE(c2.g == 0xFF);
    REQUIRE(c2.b == 0x00);
    REQUIRE(c2.a == 0x80);

    Color c3 = Color::fromHexString("invalid");
    REQUIRE(c3.r == 0);
}

TEST_CASE("SafeFloat clamp functions", "[security][float]") {
    REQUIRE(SafeFloat::clampPageCoordinate(0.0) == 0.0);
    REQUIRE(SafeFloat::clampPageCoordinate(6000.0) == 5000.0);
    REQUIRE(SafeFloat::clampPageCoordinate(-6000.0) == -5000.0);
    REQUIRE(SafeFloat::clampPageCoordinate(std::nan("")) == 0.0);

    REQUIRE(SafeFloat::clampPressure(0.5) == 0.5);
    REQUIRE(SafeFloat::clampPressure(-0.1) == 0.0);
    REQUIRE(SafeFloat::clampPressure(1.5) == 1.0);
    REQUIRE(SafeFloat::clampPressure(std::nan("")) == 0.5);
}

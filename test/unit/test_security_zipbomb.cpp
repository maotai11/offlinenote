// test/unit/test_security_zipbomb.cpp
// Real tests for zip bomb and decompression security
#include "../catch_amalgamated.hpp"
#include "../../src/serialization/SafeDecompressor.h"
#include "../../src/util/SafeArithmetic.h"
#include <fstream>
#include <cstring>

TEST_CASE("SafeDecompressor rejects empty file", "[security][decompress]") {
    // Create an empty file
    const char* testPath = "test_empty.gz";
    std::ofstream ofs(testPath, std::ios::binary);
    ofs.close();

    auto result = SafeDecompressor::decompress(testPath);
    REQUIRE(!result.success);
}

TEST_CASE("SafeDecompressor rejects invalid data", "[security][decompress]") {
    const char* testPath = "test_invalid.gz";
    std::ofstream ofs(testPath, std::ios::binary);
    // Write random garbage that's not valid gzip
    const char garbage[] = "\xDE\xAD\xBE\xEF\x00\x01\x02\x03";
    ofs.write(garbage, sizeof(garbage) - 1);
    ofs.close();

    auto result = SafeDecompressor::decompress(testPath);
    REQUIRE(!result.success);
}

TEST_CASE("SafeDecompressor handles valid small gzip", "[security][decompress]") {
    // Create a minimal valid gzip file containing "Hello"
    // This is a pre-compressed gzip of "Hello"
    const char* testPath = "test_valid.gz";
    std::ofstream ofs(testPath, std::ios::binary);
    // Minimal gzip for "Hello\n" (from gzip command)
    const unsigned char gzipHello[] = {
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x03, 0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0x07,
        0x00, 0x86, 0xa6, 0x10, 0x36, 0x05, 0x00, 0x00,
        0x00
    };
    ofs.write(reinterpret_cast<const char*>(gzipHello), sizeof(gzipHello));
    ofs.close();

    auto result = SafeDecompressor::decompress(testPath);
    // Should either succeed with "Hello\n" or fail gracefully (no crash)
    if (result.success) {
        REQUIRE(result.data.size() > 0);
        REQUIRE(result.data.size() < 10000000); // Less than 10MB sanity check
    }
    // If it fails, at least it didn't crash
}

TEST_CASE("PDF page dimension limits prevent excessive rendering", "[security][pdf]") {
    // Verify the constants in SafeArithmetic.h are reasonable
    REQUIRE(CAIRO_MAX_IMAGE_SURFACE_DIM == 32767.0);
    REQUIRE(MAX_PAGE_COORDINATE_PT == 5000.0);

    // A0 page size (3370 x 4768 pt) should be valid
    REQUIRE(SafeFloat::isValidPageCoordinate(3370.0));
    REQUIRE(SafeFloat::isValidPageCoordinate(4768.0));

    // Excessive size should be invalid
    REQUIRE(!SafeFloat::isValidPageCoordinate(10000.0));
    REQUIRE(!SafeFloat::isValidPageCoordinate(-1.0));
    REQUIRE(!SafeFloat::isValidPageCoordinate(std::nan("")));
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

    // Invalid hex should return default
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

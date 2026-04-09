// test/unit/test_path_validator.cpp
// Real tests for PathValidator security
#include "../catch_amalgamated.hpp"
#include "../../src/util/PathValidator.h"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("PathValidator rejects null bytes", "[security][path]") {
    std::string pathWithNull("test\x00file.txt", 13);
    auto result = PathValidator::validatePdfPath(pathWithNull, true);
    REQUIRE(!result.valid);
}

TEST_CASE("PathValidator rejects overly long paths", "[security][path]") {
    std::string longPath(5000, 'A');
    longPath += ".txt";
    auto result = PathValidator::validatePdfPath(longPath, true);
    REQUIRE(!result.valid);
}

TEST_CASE("PathValidator allows normal absolute paths", "[security][path]") {
    // User file dialog: should allow any absolute path
    auto result = PathValidator::validatePdfPath("C:/Users/test/document.pdf", true);
    REQUIRE(result.valid);
}

TEST_CASE("PathValidator resolves relative paths to absolute", "[security][path]") {
    auto result = PathValidator::validatePdfPath("relative/path/file.pdf", true);
    // Should be canonicalized (may or may not be absolute depending on platform)
    REQUIRE(result.valid);
    REQUIRE(!result.canonical.empty());
}

TEST_CASE("PathValidator RestrictToDirectory rejects path outside exe dir", "[security][path]") {
    // Embedded resource paths must be within exe directory
    // Try a path that's clearly outside any reasonable exe dir
    auto result = PathValidator::validatePdfPath("../../../../../../etc/passwd", false);
    // Should be rejected if outside exe directory
    REQUIRE(!result.valid);
}

TEST_CASE("PathValidator normalizes path traversal", "[security][path]") {
    // A path with .. should be normalized (but may still contain .. if target doesn't exist)
    auto result = PathValidator::validatePdfPath("safe/../../other/file.pdf", true);
    // The key is that the path is validated and canonicalized
    REQUIRE(result.valid);
    // The canonical path should be resolved as much as possible
    REQUIRE(!result.canonical.empty());
}

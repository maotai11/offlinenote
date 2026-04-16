#include "../catch_amalgamated.hpp"
#include "../../src/util/LegacyNoteResourceHelper.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("LegacyNoteResourceHelper preserves safe relative subdirectories", "[legacy][resource]") {
    const std::string stored = LegacyNoteResourceHelper::sanitizeStoredPath("note_pdf/page_1.png");
    REQUIRE(stored == "note_pdf/page_1.png");
}

TEST_CASE("LegacyNoteResourceHelper rejects traversal when resolving stored paths", "[legacy][resource][security]") {
    const fs::path root = fs::current_path() / "resource_helper_root";
    fs::create_directories(root);
    const fs::path notePath = root / "demo.onote";

    REQUIRE(LegacyNoteResourceHelper::resolveForNote(notePath, "../secret.txt").empty());
}

TEST_CASE("LegacyNoteResourceHelper keeps bundled note resources relative", "[legacy][resource]") {
    const fs::path root = fs::current_path() / "resource_helper_bundled";
    const fs::path noteDir = root / "notes";
    const fs::path notePath = noteDir / "demo.onote";
    const fs::path bundled = noteDir / "demo_pdf" / "page_1.png";

    fs::create_directories(bundled.parent_path());
    std::ofstream(bundled, std::ios::binary) << "png";

    const std::string stored = LegacyNoteResourceHelper::stageForNote(notePath, bundled, notePath);
    REQUIRE(stored == "demo_pdf/page_1.png");
}

TEST_CASE("LegacyNoteResourceHelper copies external files into note assets", "[legacy][resource]") {
    const fs::path root = fs::current_path() / "resource_helper_external";
    const fs::path sourceDir = root / "source";
    const fs::path noteDir = root / "notes";
    const fs::path notePath = noteDir / "demo.onote";
    const fs::path external = sourceDir / "external image.png";

    fs::create_directories(sourceDir);
    fs::create_directories(noteDir);
    std::ofstream(external, std::ios::binary) << "image-bytes";

    const std::string stored = LegacyNoteResourceHelper::stageForNote(notePath, external, notePath);
    REQUIRE(stored.find("demo_assets/") == 0);

    const fs::path resolved = LegacyNoteResourceHelper::resolveForNote(notePath, stored);
    REQUIRE(!resolved.empty());
    REQUIRE(fs::exists(resolved));

    std::ifstream copied(resolved, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(copied)), std::istreambuf_iterator<char>());
    REQUIRE(contents == "image-bytes");
}

// test/unit/test_atomic_write.cpp
#include "../catch_amalgamated.hpp"
#include "../../src/document/Document.h"
#include "../../src/platform/AtomicRename.h"
#include "../../src/serialization/NoteSerializer.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("atomicRename replaces an existing file without leaving the source behind", "[filesystem]") {
    const fs::path baseDir = fs::current_path() / "atomic_rename_test";
    const fs::path source = baseDir / "source.tmp";
    const fs::path dest = baseDir / "dest.txt";
    std::error_code ec;

    fs::remove_all(baseDir, ec);
    fs::create_directories(baseDir, ec);
    REQUIRE(!ec);

    {
        std::ofstream ofs(dest, std::ios::binary);
        ofs << "old";
    }
    {
        std::ofstream ofs(source, std::ios::binary);
        ofs << "new";
    }

    REQUIRE(atomicRename(source, dest));
    REQUIRE(fs::exists(dest));
    REQUIRE(!fs::exists(source));

    std::ifstream ifs(dest, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    REQUIRE(content == "new");

    fs::remove_all(baseDir, ec);
}

TEST_CASE("NoteSerializer overwrites an existing note without leaving a temp file", "[serialization][filesystem]") {
    const fs::path notePath = fs::current_path() / "atomic_serializer_note.xml";
    const fs::path tempPath = notePath.string() + ".tmp";
    std::error_code ec;
    fs::remove(notePath, ec);
    fs::remove(tempPath, ec);

    Document initial;
    initial.metadata().setTitle("first");

    Document updated;
    updated.metadata().setTitle("second");
    updated.addPage(PageSize::a4Portrait(), Orientation::Portrait);

    NoteSerializer serializer;
    REQUIRE(serializer.serialize(initial, notePath));
    REQUIRE(serializer.serialize(updated, notePath));
    REQUIRE(fs::exists(notePath));
    REQUIRE(!fs::exists(tempPath));

    std::ifstream ifs(notePath, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    REQUIRE(content.find("second") != std::string::npos);
    REQUIRE(content.find("first") == std::string::npos);

    fs::remove(notePath, ec);
    fs::remove(tempPath, ec);
}

#include "../catch_amalgamated.hpp"
#include "../../src/application/AppController.h"
#include "../../src/document/Stroke.h"
#include <filesystem>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;

TEST_CASE("AppController saveDocument writes a real note document", "[app][serialization]") {
    const fs::path notePath = fs::current_path() / "appcontroller_roundtrip.onote";
    std::error_code ec;
    fs::remove(notePath, ec);
    fs::remove(notePath.string() + ".tmp", ec);

    AppController writer;
    auto doc = writer.createNewDocument();
    REQUIRE(doc != nullptr);

    doc->metadata().setTitle("Controller Save");
    doc->metadata().setAuthor("offline-user");

    auto page = doc->addPage(PageSize::a4Portrait(), Orientation::Portrait);
    auto stroke = std::make_shared<Stroke>();
    stroke->addPoint({12.0, 34.0});
    stroke->addPoint({56.0, 78.0});
    page->currentLayer().addStroke(stroke);

    REQUIRE(writer.saveDocument(notePath));
    REQUIRE(fs::exists(notePath));

    std::ifstream saved(notePath, std::ios::binary);
    const std::string savedText((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    REQUIRE(savedText.find("<offlinenote") != std::string::npos);
    REQUIRE(savedText.find("Controller Save") != std::string::npos);

    AppController reader;
    REQUIRE(reader.openDocument(notePath));

    auto loaded = reader.currentDocument();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->metadata().title == "Controller Save");
    REQUIRE(loaded->metadata().author == "offline-user");
    REQUIRE(loaded->pageCount() == 1);
    REQUIRE(loaded->getPage(0) != nullptr);
    REQUIRE(loaded->getPage(0)->currentLayer().strokes().size() == 1);
    REQUIRE(loaded->getPage(0)->currentLayer().strokes().front()->points().size() == 2);

    fs::remove(notePath, ec);
    fs::remove(notePath.string() + ".tmp", ec);
}

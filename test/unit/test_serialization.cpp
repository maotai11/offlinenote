#include "../catch_amalgamated.hpp"
#include "../../src/document/Document.h"
#include "../../src/document/Stroke.h"
#include "../../src/serialization/NoteDeserializer.h"
#include "../../src/serialization/NoteSerializer.h"
#include <filesystem>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;

TEST_CASE("NoteDeserializer rebuilds document metadata pages and strokes", "[serialization]") {
    const fs::path notePath = fs::current_path() / "test_note.xml";
    std::ofstream ofs(notePath, std::ios::binary);
    ofs <<
        "<?xml version=\"1.0\"?>"
        "<offlinenote title=\"Test Note\" author=\"tester\" formatVersion=\"1\">"
        "  <page width=\"595.28\" height=\"841.89\">"
        "    <stroke width=\"2.5\" opacity=\"0.75\" tool=\"highlighter\" r=\"1\" g=\"0.5\" b=\"0\" a=\"1\">"
        "      0,0 100,100 200,50"
        "    </stroke>"
        "  </page>"
        "</offlinenote>";
    ofs.close();

    NoteDeserializer deserializer;
    std::shared_ptr<Document> doc = deserializer.deserialize(notePath);

    REQUIRE(doc != nullptr);
    REQUIRE(doc->pageCount() == 1);
    REQUIRE(doc->metadata().title == "Test Note");
    REQUIRE(doc->metadata().author == "tester");
    REQUIRE(doc->metadata().formatVersion == "1");

    auto page = doc->getPage(0);
    REQUIRE(page != nullptr);
    REQUIRE(page->layers().size() == 1);
    REQUIRE(page->currentLayer().strokes().size() == 1);

    auto stroke = page->currentLayer().strokes().front();
    REQUIRE(stroke != nullptr);
    REQUIRE(stroke->points().size() == 3);
    REQUIRE(stroke->properties().width == 2.5);
    REQUIRE(stroke->properties().opacity == 0.75);
    REQUIRE(stroke->properties().toolType == ToolType::Highlighter);
}

TEST_CASE("NoteSerializer round-trips escaped metadata and strokes", "[serialization]") {
    Document doc;
    doc.metadata().setTitle("A&B <Test> \"note\"");
    doc.metadata().setAuthor("tester 'quoted'");
    doc.metadata().formatVersion = "2";

    auto page = doc.addPage(PageSize{640.0, 480.0}, Orientation::Landscape);
    auto stroke = std::make_shared<Stroke>();
    ToolProperties props;
    props.width = 3.25;
    props.opacity = 0.5;
    props.toolType = ToolType::Eraser;
    props.color = Color::fromInt(12, 34, 56, 200);
    stroke->setProperties(props);
    stroke->addPoint({10.5, 20.25});
    stroke->addPoint({30.0, 40.0});
    page->currentLayer().addStroke(stroke);
    doc.clearDirty();

    const fs::path notePath = fs::current_path() / "roundtrip_note.xml";
    NoteSerializer serializer;
    REQUIRE(serializer.serialize(doc, notePath));

    NoteDeserializer deserializer;
    std::shared_ptr<Document> loaded = deserializer.deserialize(notePath);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->metadata().title == "A&B <Test> \"note\"");
    REQUIRE(loaded->metadata().author == "tester 'quoted'");
    REQUIRE(loaded->metadata().formatVersion == "2");
    REQUIRE(loaded->pageCount() == 1);
    REQUIRE(loaded->getPage(0)->currentLayer().strokes().size() == 1);

    const auto& loadedStroke = loaded->getPage(0)->currentLayer().strokes().front();
    REQUIRE(loadedStroke->properties().toolType == ToolType::Eraser);
    REQUIRE(loadedStroke->points().size() == 2);
}

TEST_CASE("NoteDeserializer rejects unsupported page elements", "[serialization][security]") {
    const fs::path notePath = fs::current_path() / "unsupported_element.xml";
    std::ofstream ofs(notePath, std::ios::binary);
    ofs <<
        "<?xml version=\"1.0\"?>"
        "<offlinenote title=\"Unsupported\">"
        "  <page width=\"595.28\" height=\"841.89\">"
        "    <image x=\"10\" y=\"20\" />"
        "  </page>"
        "</offlinenote>";
    ofs.close();

    NoteDeserializer deserializer;
    REQUIRE(deserializer.deserialize(notePath) == nullptr);
}

TEST_CASE("NoteDeserializer rejects invalid page sizes", "[serialization][security]") {
    const fs::path notePath = fs::current_path() / "invalid_page.xml";
    std::ofstream ofs(notePath, std::ios::binary);
    ofs <<
        "<?xml version=\"1.0\"?>"
        "<offlinenote title=\"Invalid Page\">"
        "  <page width=\"999999\" height=\"841.89\">"
        "    <stroke width=\"2\">0,0 10,10</stroke>"
        "  </page>"
        "</offlinenote>";
    ofs.close();

    NoteDeserializer deserializer;
    REQUIRE(deserializer.deserialize(notePath) == nullptr);
}

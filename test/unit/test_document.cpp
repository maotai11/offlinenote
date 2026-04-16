// test/unit/test_document.cpp
#include "../catch_amalgamated.hpp"
#include "../../src/document/Document.h"

TEST_CASE("Document tracks page lifecycle and dirty state", "[document]") {
    Document doc;

    REQUIRE(doc.pageCount() == 0);
    REQUIRE(doc.currentPage() == nullptr);
    REQUIRE_FALSE(doc.isDirty());

    auto firstPage = doc.addPage();
    REQUIRE(firstPage != nullptr);
    REQUIRE(doc.pageCount() == 1);
    REQUIRE(doc.currentPage() == firstPage);
    REQUIRE(doc.isDirty());

    doc.clearDirty();
    REQUIRE_FALSE(doc.isDirty());

    auto secondPage = doc.addPage(PageSize::a4Landscape(), Orientation::Landscape);
    REQUIRE(secondPage != nullptr);
    REQUIRE(doc.pageCount() == 2);
    REQUIRE(doc.isDirty());

    doc.clearDirty();
    doc.setCurrentPage(1);
    REQUIRE(doc.currentPageIndex() == 1);

    doc.deletePage(1);
    REQUIRE(doc.pageCount() == 1);
    REQUIRE(doc.currentPageIndex() == 0);
    REQUIRE(doc.isDirty());
}

// test/unit/test_stroke.cpp
#include "../catch_amalgamated.hpp"
#include "../../src/document/Stroke.h"

TEST_CASE("Stroke addPoint initializes bounding box from the first point", "[stroke]") {
    Stroke stroke;

    stroke.addPoint({10.0, 20.0});

    REQUIRE(stroke.points().size() == 1);
    REQUIRE(stroke.boundingBox().minX == Approx(10.0));
    REQUIRE(stroke.boundingBox().maxX == Approx(10.0));
    REQUIRE(stroke.boundingBox().minY == Approx(20.0));
    REQUIRE(stroke.boundingBox().maxY == Approx(20.0));

    stroke.addPoint({25.0, 5.0});

    REQUIRE(stroke.boundingBox().minX == Approx(10.0));
    REQUIRE(stroke.boundingBox().maxX == Approx(25.0));
    REQUIRE(stroke.boundingBox().minY == Approx(5.0));
    REQUIRE(stroke.boundingBox().maxY == Approx(20.0));
}

// test/unit/test_stroke_validation.cpp
#include "../catch_amalgamated.hpp"
#include "../../src/document/Stroke.h"

#include <limits>

TEST_CASE("Stroke rejects non-finite and out-of-range points", "[stroke][validation]") {
    Stroke stroke;

    stroke.addPoint({std::numeric_limits<double>::infinity(), 1.0});
    stroke.addPoint({1.0, std::numeric_limits<double>::quiet_NaN()});
    stroke.addPoint({MAX_PAGE_COORDINATE_PT + 1.0, 0.0});
    stroke.addPoint({0.0, -(MAX_PAGE_COORDINATE_PT + 1.0)});

    REQUIRE(stroke.points().empty());

    stroke.addPoint({MAX_PAGE_COORDINATE_PT, -MAX_PAGE_COORDINATE_PT});
    REQUIRE(stroke.points().size() == 1);
    REQUIRE(stroke.boundingBox().minX == Approx(MAX_PAGE_COORDINATE_PT));
    REQUIRE(stroke.boundingBox().maxX == Approx(MAX_PAGE_COORDINATE_PT));
    REQUIRE(stroke.boundingBox().minY == Approx(-MAX_PAGE_COORDINATE_PT));
    REQUIRE(stroke.boundingBox().maxY == Approx(-MAX_PAGE_COORDINATE_PT));
}

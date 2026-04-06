// test/unit/test_security_xxe.cpp
#include "../catch_amalgamated.hpp"
#include <libxml/parser.h>
#include <libxml/xmlversion.h>

static_assert(XML_PARSE_NOENT == 2, "NOENT mismatch");
static_assert(XML_PARSE_DTDLOAD == 4, "DTDLOAD mismatch");
static_assert(XML_PARSE_NONET == 2048, "NONET mismatch");
static_assert(LIBXML_VERSION >= 20900, "libxml2 >= 2.9 required");

TEST_CASE("libxml2 version check", "[security]") {
    int major = LIBXML_VERSION / 10000;
    int minor = (LIBXML_VERSION / 100) % 100;
    INFO("libxml2 version: " << major << "." << minor);
    REQUIRE(LIBXML_VERSION >= 20900);
}

#include "../catch_amalgamated.hpp"
#include "../../src/util/LegacyNoteTextCodec.h"

TEST_CASE("LegacyNoteTextCodec preserves multiline text round-trip", "[legacy][text]") {
    const std::string original = "line1\nline2\\path\tTabbed|Pipe";
    const std::string encoded = LegacyNoteTextCodec::encode(original);

    REQUIRE(encoded.find('\n') == std::string::npos);
    REQUIRE(encoded.find("\\n") != std::string::npos);
    REQUIRE(LegacyNoteTextCodec::decode(encoded) == original);
}

TEST_CASE("LegacyNoteTextCodec keeps backward-compatible plain text unchanged", "[legacy][text]") {
    const std::string legacy = "single line plain text";
    REQUIRE(LegacyNoteTextCodec::decode(legacy) == legacy);
}

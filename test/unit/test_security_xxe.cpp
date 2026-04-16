// test/unit/test_security_xxe.cpp
#include "../catch_amalgamated.hpp"
#include "../../src/serialization/SecureXmlParser.h"
#include <filesystem>
#include <fstream>
#include <libxml/parser.h>
#include <libxml/xmlversion.h>
#include <cstring>

namespace fs = std::filesystem;

static_assert(XML_PARSE_NOENT == 2, "NOENT mismatch");
static_assert(XML_PARSE_DTDLOAD == 4, "DTDLOAD mismatch");
static_assert(XML_PARSE_NONET == 2048, "NONET mismatch");
static_assert(LIBXML_VERSION >= 20900, "libxml2 >= 2.9 required");

TEST_CASE("libxml2 version check", "[security][xxe]") {
    REQUIRE(LIBXML_VERSION >= 20900);
}

TEST_CASE("SecureXmlParser does not disclose local file content through XXE", "[security][xxe]") {
    const fs::path secretPath = fs::current_path() / "xxe_secret.txt";
    const std::string secretText = "TOP_SECRET_123";

    std::ofstream secretFile(secretPath, std::ios::binary);
    secretFile << secretText;
    secretFile.close();

    std::string fileUri = "file:///" + fs::absolute(secretPath).generic_string();
    const std::string xxePayload =
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE offlinenote ["
        "  <!ENTITY xxe SYSTEM \"" + fileUri + "\">"
        "]>"
        "<offlinenote title=\"test\">"
        "  <content>&xxe;</content>"
        "</offlinenote>";

    auto result = SecureXmlParser::parseFromBuffer(xxePayload.c_str(), xxePayload.size());
    if (!result.success()) {
        REQUIRE(result.errorMessage.find("XML parse failed") != std::string::npos ||
                result.errorMessage.find("failed") != std::string::npos);
        return;
    }

    xmlNodePtr root = xmlDocGetRootElement(result.doc.get());
    REQUIRE(root != nullptr);

    xmlNodePtr contentNode = nullptr;
    for (xmlNodePtr node = root->children; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE && xmlStrcmp(node->name, BAD_CAST "content") == 0) {
            contentNode = node;
            break;
        }
    }

    REQUIRE(contentNode != nullptr);
    xmlChar* content = xmlNodeGetContent(contentNode);
    std::string text = content ? reinterpret_cast<const char*>(content) : "";
    if (content) xmlFree(content);
    REQUIRE(text.find(secretText) == std::string::npos);
}

TEST_CASE("SecureXmlParser accepts valid XML", "[security][xxe]") {
    const char* validXml =
        "<?xml version=\"1.0\"?>"
        "<offlinenote title=\"Test Note\" author=\"tester\">"
        "  <page width=\"595.28\" height=\"841.89\">"
        "    <stroke width=\"2.0\" r=\"0\" g=\"0\" b=\"1\" a=\"1\">0,0 100,100 200,50</stroke>"
        "  </page>"
        "</offlinenote>";

    auto result = SecureXmlParser::parseFromBuffer(validXml, std::strlen(validXml));
    REQUIRE(result.success());
}

TEST_CASE("SecureXmlParser rejects oversized input", "[security][xxe]") {
    std::vector<char> bigBuf(70ULL * 1024 * 1024, 'A');
    const char* prefix = "<?xml version=\"1.0\"?><offlinenote>";
    std::memcpy(bigBuf.data(), prefix, std::strlen(prefix));

    auto result = SecureXmlParser::parseFromBuffer(bigBuf.data(), bigBuf.size());
    REQUIRE(!result.success());
    REQUIRE(result.errorMessage.find("large") != std::string::npos ||
            result.errorMessage.find("Too large") != std::string::npos);
}

TEST_CASE("SecureXmlParser rejects empty buffer", "[security][xxe]") {
    auto result = SecureXmlParser::parseFromBuffer(nullptr, 0);
    REQUIRE(!result.success());
}

TEST_CASE("SecureXmlParser rejects malformed XML", "[security][xxe]") {
    const char* malformed = "<offlinenote><page><unclosed>";
    auto result = SecureXmlParser::parseFromBuffer(malformed, std::strlen(malformed));
    REQUIRE(!result.success());
}

TEST_CASE("SecureXmlParser rejects unexpected root element", "[security][xxe]") {
    const char* wrongRoot = "<?xml version=\"1.0\"?><wrongroot></wrongroot>";
    auto result = SecureXmlParser::parseFromBuffer(wrongRoot, std::strlen(wrongRoot));
    REQUIRE(!result.success());
    REQUIRE(result.errorMessage.find("root") != std::string::npos ||
            result.errorMessage.find("Unexpected") != std::string::npos);
}

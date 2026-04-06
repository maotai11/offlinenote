// src/serialization/SecureXmlParser.cpp
#include "SecureXmlParser.h"
#include "../util/Logger.h"
#include <libxml/xmlerror.h>
#include <libxml/xmlversion.h>
#include <mutex>

static_assert(LIBXML_VERSION >= 20900, "libxml2 >= 2.9.0 required");

static constexpr int SAFE_PARSE_OPTIONS = XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_COMPACT;
static constexpr int DANGEROUS_OPTIONS = XML_PARSE_NOENT | XML_PARSE_DTDLOAD;

static_assert(XML_PARSE_NOENT == 2, "NOENT value mismatch");
static_assert(XML_PARSE_DTDLOAD == 4, "DTDLOAD value mismatch");
static_assert(XML_PARSE_NONET == 2048, "NONET value mismatch");
static_assert((SAFE_PARSE_OPTIONS & DANGEROUS_OPTIONS) == 0, "Safe/dangerous overlap");

xmlParserInputPtr SecureXmlParser::noOpEntityLoader(const char* url, const char*, xmlParserCtxtPtr) {
    Logger::warning("SecureXmlParser [global-guard]: Blocked external entity: {}", url ? url : "(null)");
    return nullptr;
}

void SecureXmlParser::installGlobalNoOpLoader() {
    static std::once_flag once;
    std::call_once(once, []() {
        Logger::warning("SecureXmlParser: Installing global no-op entity loader");
        xmlSetExternalEntityLoader(noOpEntityLoader);
    });
}

void SecureXmlParser::applyContextSecurityOptions(xmlParserCtxtPtr ctxt) {
#if LIBXML_VERSION >= 21300
    xmlCtxtSetOptions(ctxt, SAFE_PARSE_OPTIONS);
#else
    xmlCtxtUseOptions(ctxt, SAFE_PARSE_OPTIONS);
    ctxt->options &= ~DANGEROUS_OPTIONS;
#endif
}

void SecureXmlParser::ErrorAccumulator::handler(void* userData, const xmlError* err) {
    if (!userData || !err || !err->message) return;
    auto* acc = static_cast<ErrorAccumulator*>(userData);
    acc->text += std::string("[line ") + std::to_string(err->line) + "] " + err->message;
}

SecureXmlParser::ParseResult SecureXmlParser::parseFromBuffer(const char* buffer, size_t bufferSize, size_t maxSizeBytes) {
    installGlobalNoOpLoader();
    if (!buffer || bufferSize == 0) return { XmlDocHolder{}, "Empty buffer" };
    if (bufferSize > maxSizeBytes) return { XmlDocHolder{}, "Input too large" };

    XmlParserCtxtHolder ctxtHolder{ xmlNewParserCtxt() };
    if (!ctxtHolder.valid()) return { XmlDocHolder{}, "xmlNewParserCtxt failed" };

    applyContextSecurityOptions(ctxtHolder.get());

    ErrorAccumulator errorAcc;
    xmlSetStructuredErrorFunc(ctxtHolder.get(), ErrorAccumulator::handler);
    ctxtHolder.get()->userData = &errorAcc;

    xmlDocPtr rawDoc = xmlCtxtReadMemory(ctxtHolder.get(), buffer, static_cast<int>(bufferSize),
                                          "offlinenote-doc", "UTF-8", SAFE_PARSE_OPTIONS);

    // Post-parse check
    if ((ctxtHolder.get()->options & XML_PARSE_NOENT) != 0) {
        if (rawDoc) xmlFreeDoc(rawDoc);
        return { XmlDocHolder{}, "Security check: XML_PARSE_NOENT detected" };
    }
    if ((ctxtHolder.get()->options & XML_PARSE_DTDLOAD) != 0) {
        if (rawDoc) xmlFreeDoc(rawDoc);
        return { XmlDocHolder{}, "Security check: XML_PARSE_DTDLOAD detected" };
    }

    if (!rawDoc) return { XmlDocHolder{}, "XML parse failed: " + errorAcc.text };

    XmlDocHolder docHolder{ rawDoc };
    xmlNodePtr root = xmlDocGetRootElement(rawDoc);
    if (!root) return { XmlDocHolder{}, "No root element" };
    if (xmlStrcmp(root->name, BAD_CAST "offlinenote") != 0)
        return { XmlDocHolder{}, "Unexpected root: " + std::string(reinterpret_cast<const char*>(root->name)) };

    return { std::move(docHolder), "" };
}

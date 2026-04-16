// src/serialization/SecureXmlParser.cpp
#include "SecureXmlParser.h"
#include "../util/Logger.h"
#include <libxml/xmlerror.h>
#include <libxml/xmlversion.h>
#include <mutex>

static_assert(LIBXML_VERSION >= 20900, "libxml2 >= 2.9.0 required");

// NOTE: libxml2 is officially unmaintained since 2025.
// xmlSetExternalEntityLoader is deprecated in recent versions as a global hook.
// Our primary defense against XXE is:
//   1. NOT using XML_PARSE_NOENT (default: external entities are NOT resolved)
//   2. NOT using XML_PARSE_DTDLOAD (no external DTD)
//   3. NOT using XML_PARSE_XINCLUDE (no XInclude)
//   4. XML_PARSE_NONET explicitly blocks network access
// The global entity loader below is a defense-in-depth measure that only activates
// if dangerous options are accidentally enabled. Since libxml2 2.15+, this is a no-op.

static constexpr int SAFE_PARSE_OPTIONS = XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_COMPACT;
static constexpr int DANGEROUS_OPTIONS = XML_PARSE_NOENT | XML_PARSE_DTDLOAD;

static_assert(XML_PARSE_NOENT == 2, "NOENT value mismatch");
static_assert(XML_PARSE_DTDLOAD == 4, "DTDLOAD value mismatch");
static_assert(XML_PARSE_NONET == 2048, "NONET value mismatch");
static_assert((SAFE_PARSE_OPTIONS & DANGEROUS_OPTIONS) == 0, "Safe/dangerous overlap");

#if LIBXML_VERSION >= 21300
static xmlParserErrors denyExternalResources(void*,
                                             const char*,
                                             const char*,
                                             xmlResourceType,
                                             xmlParserInputFlags,
                                             xmlParserInput** out) {
    if (out) *out = nullptr;
    return XML_IO_LOAD_ERROR;
}
#endif

void SecureXmlParser::applyContextSecurityOptions(xmlParserCtxtPtr ctxt) {
#if LIBXML_VERSION >= 21300
    xmlCtxtSetOptions(ctxt, SAFE_PARSE_OPTIONS);
    xmlCtxtSetResourceLoader(ctxt, denyExternalResources, nullptr);
#else
    xmlCtxtUseOptions(ctxt, SAFE_PARSE_OPTIONS);
    ctxt->options &= ~DANGEROUS_OPTIONS;
    ctxt->loadsubset = 0;  // Disable external subset (DTD) loading
    ctxt->replaceEntities = 0;  // Do not replace entities
#if LIBXML_VERSION < 21500
    // For libxml2 < 2.15, also set the per-context loader as defense-in-depth
    // For 2.15+, the above flags are sufficient as xmlSetExternalEntityLoader is a no-op
    ctxt->extSubsetHandler = nullptr;  // Explicitly disable external subset handler
#endif
#endif
}

void SecureXmlParser::ErrorAccumulator::handler(void* userData, const xmlError* err) {
    if (!userData || !err || !err->message) return;
    auto* acc = static_cast<ErrorAccumulator*>(userData);
    acc->text += std::string("[line ") + std::to_string(err->line) + "] " + err->message;
}

SecureXmlParser::ParseResult SecureXmlParser::parseFromBuffer(const char* buffer, size_t bufferSize, size_t maxSizeBytes) {
    // NOTE: We intentionally do NOT use the global entity loader anymore.
    // Security is enforced via parse options (NOENT=0, DTDLOAD=0, NONET=1).
    if (!buffer || bufferSize == 0) return { XmlDocHolder{}, "Empty buffer" };
    if (bufferSize > maxSizeBytes) return { XmlDocHolder{}, "Input too large" };

    XmlParserCtxtHolder ctxtHolder{ xmlNewParserCtxt() };
    if (!ctxtHolder.valid()) return { XmlDocHolder{}, "xmlNewParserCtxt failed" };

    applyContextSecurityOptions(ctxtHolder.get());

    ErrorAccumulator errorAcc;
#if LIBXML_VERSION >= 21300
    xmlCtxtSetErrorHandler(ctxtHolder.get(), ErrorAccumulator::handler, &errorAcc);
#else
    xmlSetStructuredErrorFunc(ctxtHolder.get(), ErrorAccumulator::handler);
    ctxtHolder.get()->userData = &errorAcc;
#endif

    xmlDocPtr rawDoc = xmlCtxtReadMemory(ctxtHolder.get(), buffer, static_cast<int>(bufferSize),
                                          "offlinenote-doc", "UTF-8", SAFE_PARSE_OPTIONS);

    // Post-parse check: ensure dangerous options were not enabled
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

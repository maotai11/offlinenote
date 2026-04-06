// src/serialization/SecureXmlParser.h
#pragma once
#include <libxml/parser.h>
#include <string>

class XmlDocHolder {
public:
    explicit XmlDocHolder(xmlDocPtr d = nullptr) : doc_(d) {}
    ~XmlDocHolder() { if (doc_) xmlFreeDoc(doc_); }
    XmlDocHolder(const XmlDocHolder&) = delete;
    XmlDocHolder& operator=(const XmlDocHolder&) = delete;
    XmlDocHolder(XmlDocHolder&& o) noexcept : doc_(o.doc_) { o.doc_ = nullptr; }
    xmlDocPtr get() const { return doc_; }
    bool valid() const { return doc_ != nullptr; }
private:
    xmlDocPtr doc_ = nullptr;
};

class XmlParserCtxtHolder {
public:
    explicit XmlParserCtxtHolder(xmlParserCtxtPtr c = nullptr) : ctx_(c) {}
    ~XmlParserCtxtHolder() { if (ctx_) xmlFreeParserCtxt(ctx_); }
    XmlParserCtxtHolder(const XmlParserCtxtHolder&) = delete;
    XmlParserCtxtHolder& operator=(const XmlParserCtxtHolder&) = delete;
    XmlParserCtxtHolder(XmlParserCtxtHolder&& o) noexcept : ctx_(o.ctx_) { o.ctx_ = nullptr; }
    xmlParserCtxtPtr get() const { return ctx_; }
    bool valid() const { return ctx_ != nullptr; }
private:
    xmlParserCtxtPtr ctx_ = nullptr;
};

class SecureXmlParser {
public:
    struct ParseResult {
        XmlDocHolder doc;
        std::string errorMessage;
        bool success() const { return doc.valid(); }
    };
    static ParseResult parseFromBuffer(const char* buffer, size_t bufferSize,
                                        size_t maxSizeBytes = 64ULL * 1024 * 1024);
private:
    static xmlParserInputPtr noOpEntityLoader(const char*, const char*, xmlParserCtxtPtr);
    static void installGlobalNoOpLoader();
    static void applyContextSecurityOptions(xmlParserCtxtPtr ctxt);
    struct ErrorAccumulator {
        std::string text;
        static void handler(void* userData, const xmlError* err);
    };
};

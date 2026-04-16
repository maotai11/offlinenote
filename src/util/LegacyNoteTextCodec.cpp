// SPDX-License-Identifier: GPL-2.0-or-later

#include "LegacyNoteTextCodec.h"

namespace LegacyNoteTextCodec {

std::string encode(std::string_view text) {
    std::string encoded;
    encoded.reserve(text.size());

    for (const char ch : text) {
        switch (ch) {
            case '\\':
                encoded += "\\\\";
                break;
            case '\n':
                encoded += "\\n";
                break;
            case '\r':
                encoded += "\\r";
                break;
            case '\t':
                encoded += "\\t";
                break;
            default:
                encoded += ch;
                break;
        }
    }

    return encoded;
}

std::string decode(std::string_view text) {
    std::string decoded;
    decoded.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\') {
            decoded += text[i];
            continue;
        }

        if (i + 1 >= text.size()) {
            decoded += '\\';
            break;
        }

        const char escaped = text[++i];
        switch (escaped) {
            case '\\':
                decoded += '\\';
                break;
            case 'n':
                decoded += '\n';
                break;
            case 'r':
                decoded += '\r';
                break;
            case 't':
                decoded += '\t';
                break;
            default:
                decoded += escaped;
                break;
        }
    }

    return decoded;
}

} // namespace LegacyNoteTextCodec

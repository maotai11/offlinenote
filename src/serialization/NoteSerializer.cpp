// SPDX-License-Identifier: GPL-2.0-or-later

#include "NoteSerializer.h"
#include "../document/Document.h"
#include "../document/Layer.h"
#include "../document/Page.h"
#include "../document/Stroke.h"
#include "../platform/AtomicRename.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

std::string escapeXml(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char ch : value) {
        switch (ch) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&apos;"; break;
            default: escaped += ch; break;
        }
    }

    return escaped;
}

double colorToUnit(uint8_t component) {
    return static_cast<double>(component) / 255.0;
}

} // namespace

bool NoteSerializer::serialize(const Document& doc, const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
    }

    std::ostringstream xml;
    xml << std::fixed << std::setprecision(6);
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<offlinenote"
        << " title=\"" << escapeXml(doc.metadata().title) << "\""
        << " author=\"" << escapeXml(doc.metadata().author) << "\""
        << " formatVersion=\"" << escapeXml(doc.metadata().formatVersion) << "\""
        << " createdAt=\"" << doc.metadata().createdAt << "\""
        << " modifiedAt=\"" << doc.metadata().modifiedAt << "\">\n";

    for (int pageIndex = 0; pageIndex < doc.pageCount(); ++pageIndex) {
        const auto page = doc.getPage(pageIndex);
        if (!page) continue;

        xml << "  <page width=\"" << page->widthPt() << "\" height=\"" << page->heightPt() << "\">\n";
        for (const auto& layer : page->layers()) {
            if (!layer) continue;

            for (const auto& stroke : layer->strokes()) {
                if (!stroke) continue;

                const auto& props = stroke->properties();
                xml << "    <stroke"
                    << " width=\"" << props.width << "\""
                    << " opacity=\"" << props.opacity << "\""
                    << " tool=\"" << toolTypeToString(props.toolType) << "\""
                    << " r=\"" << colorToUnit(props.color.r) << "\""
                    << " g=\"" << colorToUnit(props.color.g) << "\""
                    << " b=\"" << colorToUnit(props.color.b) << "\""
                    << " a=\"" << colorToUnit(props.color.a) << "\">";

                bool firstPoint = true;
                for (const auto& point : stroke->points()) {
                    if (!firstPoint) {
                        xml << ' ';
                    }
                    firstPoint = false;
                    xml << point.x << ',' << point.y;
                }

                xml << "</stroke>\n";
            }
        }
        xml << "  </page>\n";
    }

    xml << "</offlinenote>\n";

    const std::filesystem::path tempPath = path.string() + ".tmp";
    {
        std::ofstream ofs(tempPath, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;

        ofs << xml.str();
        if (!ofs.good()) return false;
    }

    if (atomicRename(tempPath, path)) {
        return true;
    }

    std::filesystem::remove(tempPath, ec);
    return false;
}

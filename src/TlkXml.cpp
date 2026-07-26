// SPDX-License-Identifier: GPL-3.0-or-later

#include "neotlk/TlkXml.hpp"

#include "SimpleXml.hpp"
#include "neotlk/TlkTable.hpp"
#include "neotlk/TlkColumns.hpp"

#include <charconv>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace neotlk {
namespace {

bool isAsciiSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool isWhitespaceOnly(const std::string& text) noexcept {
    for (char c : text) {
        if (!isAsciiSpace(c)) return false;
    }
    return true;
}

std::string xmlEscapeTextPreserveControls(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char rawCh : text) {
        const unsigned char ch = static_cast<unsigned char>(rawCh);
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '\r': out += "&#13;"; break;
        case '\t': out += "&#9;"; break;
        default: out.push_back(static_cast<char>(ch)); break;
        }
    }
    return out;
}

std::string xmlEscapeAttributePreserveControls(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char rawCh : text) {
        const unsigned char ch = static_cast<unsigned char>(rawCh);
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        case '\r': out += "&#13;"; break;
        case '\n': out += "&#10;"; break;
        case '\t': out += "&#9;"; break;
        default: out.push_back(static_cast<char>(ch)); break;
        }
    }
    return out;
}

UInt32 parseXmlUInt32(const std::string& value, const std::string& fieldName) {
    UInt32 parsed = 0;
    const char* first = value.data();
    const char* last = value.data() + value.size();
    const auto result = std::from_chars(first, last, parsed);
    if (value.empty() || result.ec != std::errc{} || result.ptr != last) {
        throw NeoTLKError("Invalid TLK XML " + fieldName + ": " + value);
    }
    return parsed;
}

std::string floatXmlText(float value) {
    std::ostringstream out;
    out << std::setprecision(9) << value;
    return out.str();
}

void validateStringAttributes(const neoxml::Node& node) {
    for (const auto& [key, value] : node.attributes) {
        (void)value;
        if (key != "id" && key != "sound" && key != "soundlength" &&
            key != "volumevariance" && key != "pitchvariance" && key != "soundid" &&
            key != "flags" && key != "encoding") {
            throw NeoTLKError("Unsupported TLK XML <string> attribute: " + key);
        }
    }
}

neoxml::Node parseTlkXmlRoot(const std::string& xmlText) {
    neoxml::Node root;
    try {
        root = neoxml::parse(xmlText);
    } catch (const neoxml::XmlError& error) {
        throw NeoTLKError(std::string("Invalid TLK XML: ") + error.what());
    }
    if (root.name != "tlk") {
        throw NeoTLKError("Invalid TLK XML. Expected root element <tlk>.");
    }
    if (!root.text.empty() && !isWhitespaceOnly(root.text)) {
        throw NeoTLKError("Invalid TLK XML. The <tlk> root may only contain <string> children and whitespace.");
    }
    for (const auto& [key, value] : root.attributes) {
        (void)value;
        if (key != "language" && key != "storageFormat") {
            throw NeoTLKError("Unsupported TLK XML <tlk> attribute: " + key);
        }
    }
    return root;
}

neotabular::Table tableFromTlkXmlRoot(const neoxml::Node& root,
                                      bool inferClassicFamily) {
    neotabular::Table out;
    out.columns = tlkTableColumnNames();

    std::string storageFormat = root.attribute("storageFormat");
    if (storageFormat.empty() && inferClassicFamily) {
        for (const neoxml::Node& child : root.children) {
            if (child.attributes.find("soundid") != child.attributes.end()) {
                storageFormat = storageFormatToken(TlkStorageFormat::JadeV40);
                break;
            }
        }
    }
    const std::string language = root.attribute("language");

    for (const neoxml::Node& node : root.children) {
        if (node.name != "string") {
            throw NeoTLKError("Invalid TLK XML. The <tlk> root may only contain <string> children.");
        }
        if (!node.children.empty()) {
            throw NeoTLKError("Invalid TLK XML. <string> elements may not contain nested elements.");
        }
        validateStringAttributes(node);
        const std::string id = node.attribute("id");
        if (id.empty()) {
            throw NeoTLKError("Invalid TLK XML. <string> is missing required id attribute.");
        }
        (void)parseXmlUInt32(id, "string id");

        out.rows.push_back({
            id,
            node.text,
            node.attribute("sound"),
            node.attribute("flags"),
            node.attribute("soundid"),
            node.attribute("volumevariance"),
            node.attribute("pitchvariance"),
            node.attribute("soundlength"),
            storageFormat,
            language,
            node.attribute("encoding"),
        });
    }

    if (out.rows.empty()) {
        out.rows.push_back({
            std::string(), std::string(), std::string(), std::string(),
            std::string(), std::string(), std::string(), std::string(),
            storageFormat, language, std::string(),
        });
    }
    return out;
}
} // namespace

std::string talkTableToXml(const TalkTable& table) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    out << "<tlk storageFormat=\"" << storageFormatToken(table.storageFormat()) << "\"";
    if (table.supportsLanguageId()) out << " language=\"" << table.language() << "\"";
    out << ">\n";
    for (const TalkString& entry : table.entries()) {
        out << "  <string id=\"" << entry.strRef << "\""
            << " encoding=\"" << xmlEscapeAttributePreserveControls(textEncodingName(entry.textEncoding)) << "\"";
        if (table.storageFormat() == TlkStorageFormat::ClassicV30) {
            const std::string sound = entry.soundResref.soundString();
            out << " flags=\"" << entry.flags << "\"";
            if (!sound.empty()) out << " sound=\"" << xmlEscapeAttributePreserveControls(sound) << "\"";
            out << " volumevariance=\"" << entry.volumeVariance << "\""
                << " pitchvariance=\"" << entry.pitchVariance << "\""
                << " soundlength=\"" << floatXmlText(entry.soundLength) << "\"";
        } else if (table.storageFormat() == TlkStorageFormat::JadeV40) {
            out << " soundid=\"" << entry.soundId << "\"";
        }
        out << ">" << xmlEscapeTextPreserveControls(entry.text) << "</string>\n";
    }
    out << "</tlk>\n";
    return out.str();
}

void applyXmlToTalkTable(TalkTable& table,
                         const std::string& xmlText,
                         bool allowClassicFormatChange) {
    const neoxml::Node root = parseTlkXmlRoot(xmlText);
    neotabular::Table imported = tableFromTlkXmlRoot(root, allowClassicFormatChange);
    applyTabularToTalkTable(table,
                            imported,
                            allowClassicFormatChange,
                            TlkTabularApplyMode::Replace);
}

UInt32 languageFromTlkXml(const std::string& xmlText, UInt32 fallback) {
    const neoxml::Node root = parseTlkXmlRoot(xmlText);
    const std::string language = root.attribute("language");
    return language.empty() ? fallback : parseXmlUInt32(language, "language");
}

std::optional<TlkStorageFormat> storageFormatFromTlkXml(const std::string& xmlText) {
    const neoxml::Node root = parseTlkXmlRoot(xmlText);
    const std::string format = root.attribute("storageFormat");
    if (format.empty()) return std::nullopt;
    return parseStorageFormat(format);
}


} // namespace neotlk

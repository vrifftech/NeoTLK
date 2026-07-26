#include "neotlk/TlkJson.hpp"

#include "neotlk/TlkColumns.hpp"

#include "SimpleJson.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace neotlk {
namespace {

std::string trim(std::string value) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::uint32_t parseUInt32Text(const std::string& text, const char* what) {
    const std::string trimmed = trim(text);
    std::uint32_t value = 0;
    const char* first = trimmed.data();
    const char* last = trimmed.data() + trimmed.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (trimmed.empty() || result.ec != std::errc{} || result.ptr != last) {
        throw std::invalid_argument(std::string("Invalid ") + what + ": " + text);
    }
    return value;
}

std::uint32_t valueToUInt32(const neojson::Value& value, const char* what) {
    return parseUInt32Text(value.asText(what), what);
}

std::string optionalStringOrNumber(const neojson::Value& object, const std::string& key, const std::string& fallback = {}) {
    const neojson::Value* value = object.find(key);
    if (!value || value->isNull()) return fallback;
    return value->asText(("JSON member '" + key + "'").c_str());
}

std::string optionalString(const neojson::Value& object, const std::string& key, const std::string& fallback = {}) {
    const neojson::Value* value = object.find(key);
    if (!value || value->isNull()) return fallback;
    return value->asString(("JSON member '" + key + "'").c_str());
}

std::string requireStringOrNumber(const neojson::Value& object, const std::string& key) {
    return object.at(key).asText(("JSON member '" + key + "'").c_str());
}

} // namespace

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Unable to open text file: " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Unable to open text file for writing: " + path.string());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) throw std::runtime_error("Unable to write text file: " + path.string());
}

std::string talkTableToJson(const TalkTable& table) {
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\n"
        << "  \"format\": \"TLK\",\n"
        << "  \"storageFormat\": " << neojson::quote(storageFormatToken(table.storageFormat())) << ",\n";
    if (table.supportsLanguageId()) {
        out << "  \"languageId\": " << table.language() << ",\n";
    }
    out << "  \"entries\": [\n";
    for (std::size_t index = 0; index < table.entries().size(); ++index) {
        const TalkString& entry = table.entries()[index];
        out << "    {\n"
            << "      \"id\": " << entry.strRef << ",\n"
            << "      \"text\": " << neojson::quote(entry.text) << ",\n"
            << "      \"textEncoding\": " << neojson::quote(textEncodingName(entry.textEncoding));
        if (table.storageFormat() == TlkStorageFormat::ClassicV30) {
            out << ",\n"
                << "      \"soundResRef\": " << neojson::quote(entry.soundResref.soundString()) << ",\n"
                << "      \"soundLength\": " << entry.soundLength << ",\n"
                << "      \"flags\": " << entry.flags << ",\n"
                << "      \"volumeVariance\": " << entry.volumeVariance << ",\n"
                << "      \"pitchVariance\": " << entry.pitchVariance;
        } else if (table.storageFormat() == TlkStorageFormat::JadeV40) {
            out << ",\n"
                << "      \"soundId\": " << entry.soundId;
        }
        out << "\n    }" << (index + 1u < table.entries().size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

neotabular::Table tlkTableFromJson(const std::string& jsonText) {
    const neojson::Value root = neojson::parse(jsonText);
    if (!root.isObject()) throw std::invalid_argument("TLK JSON must be an object.");
    neotabular::Table out;
    out.columns = neotlk::tlkTableColumnNames();
    const std::string storageFormat = optionalString(root, "storageFormat");
    const std::string language = optionalStringOrNumber(root, "languageId");
    const auto& entries = root.at("entries").asArray("TLK JSON entries");
    for (const auto& entryValue : entries) {
        entryValue.asObject("TLK JSON entry");
        const std::string id = requireStringOrNumber(entryValue, "id");
        const std::string text = optionalString(entryValue, "text");
        const std::string flags = optionalStringOrNumber(entryValue, "flags", text.empty() ? "0" : "1");
        out.rows.push_back({
            id,
            text,
            optionalString(entryValue, "soundResRef", optionalString(entryValue, "sound")),
            flags,
            optionalStringOrNumber(entryValue, "soundId", "4294967295"),
            optionalStringOrNumber(entryValue, "volumeVariance", "0"),
            optionalStringOrNumber(entryValue, "pitchVariance", "0"),
            optionalStringOrNumber(entryValue, "soundLength", "0"),
            storageFormat,
            language,
            optionalString(entryValue, "textEncoding"),
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

std::uint32_t tlkLanguageFromJson(const std::string& jsonText, std::uint32_t fallback) {
    const neojson::Value root = neojson::parse(jsonText);
    if (!root.isObject()) return fallback;
    const neojson::Value* language = root.find("languageId");
    return language ? valueToUInt32(*language, "languageId") : fallback;
}

std::optional<TlkStorageFormat> tlkStorageFormatFromJson(const std::string& jsonText) {
    const neojson::Value root = neojson::parse(jsonText);
    if (!root.isObject()) return std::nullopt;
    const neojson::Value* format = root.find("storageFormat");
    if (!format || format->isNull()) return std::nullopt;
    return parseStorageFormat(format->asString("storageFormat"));
}

} // namespace neotlk

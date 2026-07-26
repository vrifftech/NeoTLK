#pragma once

#include "TabularData.hpp"
#include "neotlk/TlkFile.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace neotlk {

std::string readTextFile(const std::filesystem::path& path);
void writeTextFile(const std::filesystem::path& path, const std::string& text);

// Structured JSON is a complete semantic TLK document, not a filtered table.
std::string talkTableToJson(const TalkTable& table);
neotabular::Table tlkTableFromJson(const std::string& jsonText);
std::uint32_t tlkLanguageFromJson(const std::string& jsonText, std::uint32_t fallback);
std::optional<TlkStorageFormat> tlkStorageFormatFromJson(const std::string& jsonText);

} // namespace neotlk

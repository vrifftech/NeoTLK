#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace neotlk {

inline constexpr std::string_view kTlkColumnStrRef = "StrRef";
inline constexpr std::string_view kTlkColumnText = "Text";
inline constexpr std::string_view kTlkColumnSound = "Sound";
inline constexpr std::string_view kTlkColumnFlags = "Flags";
inline constexpr std::string_view kTlkColumnSoundId = "SoundId";
inline constexpr std::string_view kTlkColumnVolume = "Volume";
inline constexpr std::string_view kTlkColumnPitch = "Pitch";
inline constexpr std::string_view kTlkColumnSoundLength = "SoundLength";
inline constexpr std::string_view kTlkColumnStorageFormat = "StorageFormat";
inline constexpr std::string_view kTlkColumnLanguageId = "LanguageId";
inline constexpr std::string_view kTlkColumnTextEncoding = "TextEncoding";

inline constexpr std::array<std::string_view, 11> kTlkTableColumns = {
    kTlkColumnStrRef,
    kTlkColumnText,
    kTlkColumnSound,
    kTlkColumnFlags,
    kTlkColumnSoundId,
    kTlkColumnVolume,
    kTlkColumnPitch,
    kTlkColumnSoundLength,
    kTlkColumnStorageFormat,
    kTlkColumnLanguageId,
    kTlkColumnTextEncoding,
};

inline std::vector<std::string> tlkTableColumnNames() {
    std::vector<std::string> out;
    out.reserve(kTlkTableColumns.size());
    for (std::string_view column : kTlkTableColumns) {
        out.emplace_back(column);
    }
    return out;
}

} // namespace neotlk

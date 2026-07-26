// SPDX-License-Identifier: GPL-3.0-or-later

#include "neotlk/TlkTable.hpp"

#include "neotlk/TlkColumns.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>

namespace neotlk {
namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::size_t columnIndex(const neotabular::Table& table,
                        std::string_view name,
                        bool required) {
    const std::string wanted = lowerAscii(std::string(name));
    for (std::size_t index = 0; index < table.columns.size(); ++index) {
        if (lowerAscii(table.columns[index]) == wanted) return index;
    }
    if (required) {
        throw NeoTLKError("Imported TLK table is missing required column: " + std::string(name));
    }
    return table.columns.size();
}

std::string cell(const std::vector<std::string>& row, std::size_t column) {
    return column < row.size() ? row[column] : std::string();
}

UInt32 parseUInt32(const std::string& text, std::string_view fieldName) {
    UInt32 value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (text.empty() || result.ec != std::errc{} || result.ptr != last) {
        throw NeoTLKError("Invalid " + std::string(fieldName) + ": " + text);
    }
    return value;
}

float parseFloat(const std::string& text, std::string_view fieldName) {
    std::size_t consumed = 0;
    float value = 0.0f;
    try {
        value = std::stof(text, &consumed);
    } catch (const std::exception&) {
        throw NeoTLKError("Invalid " + std::string(fieldName) + ": " + text);
    }
    if (text.empty() || consumed != text.size() || !std::isfinite(value)) {
        throw NeoTLKError("Invalid " + std::string(fieldName) + ": " + text);
    }
    return value;
}

std::string floatText(float value) {
    std::ostringstream out;
    out.precision(9);
    out << value;
    return out.str();
}



} // namespace

neotabular::Table talkTableToTabular(const TalkTable& table) {
    neotabular::Table out;
    out.columns = tlkTableColumnNames();
    const std::string format = storageFormatToken(table.storageFormat());
    const std::string language = table.supportsLanguageId() ? std::to_string(table.language()) : std::string();
    for (const TalkString& entry : table.entries()) {
        std::string sound;
        std::string flags;
        std::string soundId;
        std::string volume;
        std::string pitch;
        std::string soundLength;
        if (table.storageFormat() == TlkStorageFormat::ClassicV30) {
            sound = entry.soundResref.soundString();
            flags = std::to_string(entry.flags);
            volume = std::to_string(entry.volumeVariance);
            pitch = std::to_string(entry.pitchVariance);
            soundLength = floatText(entry.soundLength);
        } else if (table.storageFormat() == TlkStorageFormat::JadeV40) {
            if (entry.soundId != 0xffffffffu) soundId = std::to_string(entry.soundId);
        }
        out.rows.push_back({
            std::to_string(entry.strRef),
            entry.text,
            std::move(sound),
            std::move(flags),
            std::move(soundId),
            std::move(volume),
            std::move(pitch),
            std::move(soundLength),
            format,
            language,
            textEncodingName(entry.textEncoding),
        });
    }
    ensureTlkTabularMetadataRow(table, out);
    return out;
}

void ensureTlkTabularMetadataRow(const TalkTable& table, neotabular::Table& exported) {
    if (!exported.rows.empty()) return;
    if (exported.columns.empty()) exported.columns = tlkTableColumnNames();

    std::vector<std::string> row(exported.columns.size());
    const std::size_t formatCol = columnIndex(exported, kTlkColumnStorageFormat, false);
    const std::size_t languageCol = columnIndex(exported, kTlkColumnLanguageId, false);
    const std::size_t encodingCol = columnIndex(exported, kTlkColumnTextEncoding, false);
    if (formatCol < row.size()) row[formatCol] = storageFormatToken(table.storageFormat());
    if (languageCol < row.size() && table.supportsLanguageId()) row[languageCol] = std::to_string(table.language());
    if (encodingCol < row.size()) row[encodingCol] = textEncodingName(table.preferredTextEncoding());
    exported.rows.push_back(std::move(row));
}

TlkTabularMetadata inspectTlkTabularMetadata(const neotabular::Table& table) {
    TlkTabularMetadata metadata;
    const std::size_t formatCol = columnIndex(table, kTlkColumnStorageFormat, false);
    const std::size_t languageCol = columnIndex(table, kTlkColumnLanguageId, false);
    for (const auto& row : table.rows) {
        const std::string formatText = cell(row, formatCol);
        if (!formatText.empty()) {
            const TlkStorageFormat parsed = parseStorageFormat(formatText);
            if (metadata.storageFormat && *metadata.storageFormat != parsed) {
                throw NeoTLKError("Imported TLK table contains conflicting StorageFormat values.");
            }
            metadata.storageFormat = parsed;
        }
        const std::string languageText = cell(row, languageCol);
        if (!languageText.empty()) {
            const UInt32 parsed = parseUInt32(languageText, kTlkColumnLanguageId);
            if (metadata.languageId && *metadata.languageId != parsed) {
                throw NeoTLKError("Imported TLK table contains conflicting LanguageId values.");
            }
            metadata.languageId = parsed;
        }
    }
    return metadata;
}

void applyDeclaredStorageFormat(TalkTable& table,
                                TlkStorageFormat declaredFormat,
                                bool allowClassicFormatChange) {
    const TlkStorageFormat current = table.storageFormat();
    if (current == declaredFormat) return;
    if (current == TlkStorageFormat::DragonAgeV02 || declaredFormat == TlkStorageFormat::DragonAgeV02) {
        throw NeoTLKError(
            "Dragon Age GFF TLK V0.2 interchange must be imported into an already-open Dragon Age TLK so its native GFF structure is preserved.");
    }
    if (!allowClassicFormatChange) {
        throw NeoTLKError("Imported TLK data declares " + storageFormatName(declaredFormat) +
                          ", but the active document is " + storageFormatName(current) + ".");
    }
    if (declaredFormat == TlkStorageFormat::JadeV40) table.setVersion40();
    else table.setVersion30();
}

void applyTabularToTalkTable(TalkTable& table,
                             const neotabular::Table& imported,
                             bool allowClassicFormatChange,
                             TlkTabularApplyMode mode) {
    const TlkTabularMetadata metadata = inspectTlkTabularMetadata(imported);
    const TlkStorageFormat currentFormat = table.storageFormat();
    const TlkStorageFormat targetFormat = metadata.storageFormat.value_or(currentFormat);
    if (targetFormat != currentFormat) {
        if (currentFormat == TlkStorageFormat::DragonAgeV02 || targetFormat == TlkStorageFormat::DragonAgeV02) {
            throw NeoTLKError(
                "Dragon Age GFF TLK V0.2 interchange must be imported into an already-open Dragon Age TLK so its native GFF structure is preserved.");
        }
        if (!allowClassicFormatChange) {
            throw NeoTLKError("Imported TLK data declares " + storageFormatName(targetFormat) +
                              ", but the active document is " + storageFormatName(currentFormat) + ".");
        }
        if (mode != TlkTabularApplyMode::Replace && table.count() != 0u) {
            throw NeoTLKError("Changing TLK storage families requires a complete XML/JSON replacement import, not a partial CSV/TSV merge.");
        }
    }
    if (metadata.languageId && targetFormat == TlkStorageFormat::DragonAgeV02) {
        throw NeoTLKError("Dragon Age GFF TLK V0.2 does not store a classic LanguageId.");
    }

    const std::size_t strRefCol = columnIndex(imported, kTlkColumnStrRef, true);
    const std::size_t textCol = columnIndex(imported, kTlkColumnText, false);
    const std::size_t soundCol = columnIndex(imported, kTlkColumnSound, false);
    const std::size_t flagsCol = columnIndex(imported, kTlkColumnFlags, false);
    const std::size_t soundIdCol = columnIndex(imported, kTlkColumnSoundId, false);
    const std::size_t volumeCol = columnIndex(imported, kTlkColumnVolume, false);
    const std::size_t pitchCol = columnIndex(imported, kTlkColumnPitch, false);
    const std::size_t lengthCol = columnIndex(imported, kTlkColumnSoundLength, false);
    const std::size_t encodingCol = columnIndex(imported, kTlkColumnTextEncoding, false);

    const auto hasColumn = [&](std::size_t column) {
        return column < imported.columns.size();
    };
    const bool sparse = targetFormat == TlkStorageFormat::DragonAgeV02;
    const TextEncoding newEntryEncoding = targetFormat == TlkStorageFormat::ClassicV30
        ? table.preferredTextEncoding()
        : TextEncoding::Utf8;

    std::vector<TalkString> nextEntries = mode == TlkTabularApplyMode::Merge
        ? table.entries()
        : std::vector<TalkString>{};
    std::unordered_set<UInt32> importedStrRefs;

    auto findEntry = [&](UInt32 strRef) -> TalkString* {
        if (!sparse && static_cast<std::size_t>(strRef) < nextEntries.size() &&
            nextEntries[static_cast<std::size_t>(strRef)].strRef == strRef) {
            return &nextEntries[static_cast<std::size_t>(strRef)];
        }
        const auto found = std::find_if(nextEntries.begin(), nextEntries.end(), [strRef](const TalkString& entry) {
            return entry.strRef == strRef;
        });
        return found == nextEntries.end() ? nullptr : &*found;
    };

    auto makeEntry = [&](UInt32 strRef) -> TalkString& {
        if (sparse) {
            TalkString entry;
            entry.strRef = strRef;
            entry.textEncoding = newEntryEncoding;
            entry.custom = true;
            nextEntries.push_back(std::move(entry));
            return nextEntries.back();
        }
        try {
            while (nextEntries.size() <= static_cast<std::size_t>(strRef)) {
                TalkString entry;
                entry.strRef = static_cast<UInt32>(nextEntries.size());
                entry.textEncoding = newEntryEncoding;
                entry.custom = true;
                nextEntries.push_back(std::move(entry));
            }
        } catch (const std::bad_alloc&) {
            throw NeoTLKError("Imported StrRef requires a TLK table too large to allocate safely.");
        }
        return nextEntries[static_cast<std::size_t>(strRef)];
    };

    for (const auto& row : imported.rows) {
        const std::string refText = cell(row, strRefCol);
        if (refText.empty()) continue; // Metadata-only flat-export row.
        const UInt32 strRef = parseUInt32(refText, kTlkColumnStrRef);
        if (!importedStrRefs.insert(strRef).second) {
            throw NeoTLKError("Imported TLK data contains duplicate StrRef " + std::to_string(strRef) + ".");
        }

        TalkString* entry = findEntry(strRef);
        const bool entryExisted = entry != nullptr;
        if (entry == nullptr) entry = &makeEntry(strRef);

        if (hasColumn(textCol) && textCol < row.size()) entry->text = cell(row, textCol);
        if (hasColumn(encodingCol)) {
            const std::string encodingText = cell(row, encodingCol);
            if (!encodingText.empty()) {
                const TextEncoding encoding = parseTextEncoding(encodingText);
                if (targetFormat != TlkStorageFormat::ClassicV30 && encoding != TextEncoding::Utf8) {
                    throw NeoTLKError(storageFormatName(targetFormat) + " stores Unicode text; TextEncoding must be UTF-8.");
                }
                entry->textEncoding = encoding;
            } else if (!entryExisted) {
                entry->textEncoding = newEntryEncoding;
            }
        } else if (!entryExisted) {
            entry->textEncoding = newEntryEncoding;
        }

        const std::string sound = hasColumn(soundCol) ? cell(row, soundCol) : std::string();
        const std::string flags = hasColumn(flagsCol) ? cell(row, flagsCol) : std::string();
        const std::string soundId = hasColumn(soundIdCol) ? cell(row, soundIdCol) : std::string();
        const std::string volume = hasColumn(volumeCol) ? cell(row, volumeCol) : std::string();
        const std::string pitch = hasColumn(pitchCol) ? cell(row, pitchCol) : std::string();
        const std::string length = hasColumn(lengthCol) ? cell(row, lengthCol) : std::string();
        const UInt32 expectedTextFlags = entry->text.empty() ? 0u : TEXT_PRESENT;

        if (targetFormat == TlkStorageFormat::DragonAgeV02) {
            if (!sound.empty()) throw NeoTLKError("Dragon Age GFF TLK V0.2 does not store a sound ResRef.");
            if (!soundId.empty() && parseUInt32(soundId, kTlkColumnSoundId) != 0xffffffffu) {
                throw NeoTLKError("Dragon Age GFF TLK V0.2 does not store a numeric SoundId.");
            }
            if (!flags.empty() && parseUInt32(flags, kTlkColumnFlags) != expectedTextFlags) {
                throw NeoTLKError("Dragon Age GFF TLK V0.2 does not store explicit TLK flags.");
            }
            if (!volume.empty() && parseUInt32(volume, kTlkColumnVolume) != 0u) {
                throw NeoTLKError("Dragon Age GFF TLK V0.2 does not store volume variance.");
            }
            if (!pitch.empty() && parseUInt32(pitch, kTlkColumnPitch) != 0u) {
                throw NeoTLKError("Dragon Age GFF TLK V0.2 does not store pitch variance.");
            }
            if (!length.empty() && parseFloat(length, kTlkColumnSoundLength) != 0.0f) {
                throw NeoTLKError("Dragon Age GFF TLK V0.2 does not store sound length.");
            }
            entry->flags = expectedTextFlags;
            entry->soundResref = {};
            entry->soundId = 0xffffffffu;
            entry->volumeVariance = 0u;
            entry->pitchVariance = 0u;
            entry->soundLength = 0.0f;
            entry->textEncoding = TextEncoding::Utf8;
            entry->custom = true;
            continue;
        }

        if (targetFormat == TlkStorageFormat::JadeV40) {
            std::optional<UInt32> parsedSoundId;
            if (!soundId.empty()) parsedSoundId = parseUInt32(soundId, kTlkColumnSoundId);
            if (!sound.empty()) {
                const UInt32 legacySoundId = parseUInt32(sound, kTlkColumnSound);
                if (parsedSoundId && *parsedSoundId != legacySoundId) {
                    throw NeoTLKError("Jade Empire TLK row contains conflicting Sound and SoundId values.");
                }
                parsedSoundId = legacySoundId;
            }
            if (!flags.empty() && parseUInt32(flags, kTlkColumnFlags) != expectedTextFlags) {
                throw NeoTLKError("Jade Empire TLK V4.0 does not store explicit classic TLK flags.");
            }
            if (!volume.empty() && parseUInt32(volume, kTlkColumnVolume) != 0u) {
                throw NeoTLKError("Jade Empire TLK V4.0 does not store volume variance.");
            }
            if (!pitch.empty() && parseUInt32(pitch, kTlkColumnPitch) != 0u) {
                throw NeoTLKError("Jade Empire TLK V4.0 does not store pitch variance.");
            }
            if (!length.empty() && parseFloat(length, kTlkColumnSoundLength) != 0.0f) {
                throw NeoTLKError("Jade Empire TLK V4.0 does not store sound length.");
            }
            entry->flags = expectedTextFlags;
            entry->soundResref = {};
            if (parsedSoundId) entry->soundId = *parsedSoundId;
            else if (!entryExisted || mode == TlkTabularApplyMode::Replace) entry->soundId = 0xffffffffu;
            entry->volumeVariance = 0u;
            entry->pitchVariance = 0u;
            entry->soundLength = 0.0f;
            entry->textEncoding = TextEncoding::Utf8;
            entry->custom = true;
            continue;
        }

        if (!soundId.empty() && parseUInt32(soundId, kTlkColumnSoundId) != 0xffffffffu) {
            throw NeoTLKError("Classic TLK V3.0 stores a sound ResRef, not a numeric SoundId.");
        }
        if (hasColumn(soundCol)) entry->soundResref = ResRef::fromString(sound);
        entry->soundId = 0xffffffffu;
        if (hasColumn(volumeCol)) entry->volumeVariance = volume.empty() ? 0u : parseUInt32(volume, kTlkColumnVolume);
        if (hasColumn(pitchCol)) entry->pitchVariance = pitch.empty() ? 0u : parseUInt32(pitch, kTlkColumnPitch);
        if (hasColumn(lengthCol)) entry->soundLength = length.empty() ? 0.0f : parseFloat(length, kTlkColumnSoundLength);

        if (!flags.empty()) {
            entry->flags = parseUInt32(flags, kTlkColumnFlags);
        } else if (hasColumn(flagsCol) || !entryExisted || mode == TlkTabularApplyMode::Replace) {
            entry->flags = expectedTextFlags;
            if (!entry->soundResref.soundString().empty()) entry->flags |= SND_PRESENT;
            if (entry->soundLength != 0.0f) entry->flags |= SNDLENGTH_PRESENT;
        } else {
            if (hasColumn(textCol)) {
                entry->flags &= ~TEXT_PRESENT;
                entry->flags |= expectedTextFlags;
            }
            if (hasColumn(soundCol)) {
                entry->flags &= ~SND_PRESENT;
                if (!entry->soundResref.soundString().empty()) entry->flags |= SND_PRESENT;
            }
            if (hasColumn(lengthCol)) {
                entry->flags &= ~SNDLENGTH_PRESENT;
                if (entry->soundLength != 0.0f) entry->flags |= SNDLENGTH_PRESENT;
            }
        }
        entry->custom = true;
    }

    if (targetFormat != currentFormat) {
        applyDeclaredStorageFormat(table, targetFormat, allowClassicFormatChange);
    }
    if (metadata.languageId) table.setLanguage(*metadata.languageId);
    table.replaceAllEntries(std::move(nextEntries));
}

} // namespace neotlk

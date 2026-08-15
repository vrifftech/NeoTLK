// SPDX-License-Identifier: GPL-3.0-or-later

#include "neotlk/Search.hpp"
#include "neotlk/StringUtil.hpp"
#include "neotlk/TlkFile.hpp"
#include "neotlk/TlkXml.hpp"
#include "TabularData.hpp"
#include "neotlk/TlkJson.hpp"
#include "neotlk/TlkPatcher.hpp"
#include "neotlk/TlkColumns.hpp"
#include "neotlk/TlkTable.hpp"

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <utility>


using neotlk::UInt32;

namespace {


void printUsage(std::ostream& out) {
    out << "NeoTLK C++ command line utility\n"
        << "\n"
        << "Usage:\n"
        << "  neotlk-cli info <file.tlk>\n"
        << "  neotlk-cli list <file.tlk> [start] [stop]\n"
        << "  neotlk-cli export <file.tlk> <csv|tsv|xml|json> <output> [filter-term-for-csv-tsv]\n"
        << "  neotlk-cli import <input-table> <csv|tsv|xml|json> <output.tlk> [language-id]\n"
        << "  neotlk-cli diff-tslpatcher <original.tlk> <modified-input> <output-dir> [--modified-format csv|tsv|xml|json|tlk|kotor|native|auto] [--tslpatcher|--holopatcher] [--ini installer.ini] [--allow-unsupported]\n"
        << "  neotlk-cli diff-tslpatcher-import <original.tlk> <modified-input> <csv|tsv|xml|json|tlk|kotor|native|auto> <output-dir> [--tslpatcher|--holopatcher] [--ini installer.ini] [--allow-unsupported]\n"
        << "  neotlk-cli search <file.tlk> [options]\n"
        << "  neotlk-cli add <input.tlk> <output.tlk> [entry-options]\n"
        << "  neotlk-cli edit <input.tlk> <output.tlk> <strref> [entry-options]\n"
        << "  neotlk-cli delete <input.tlk> <output.tlk> <strref>\n"
        << "  neotlk-cli append <input.tlk> <append.tlk> <output.tlk>\n"
        << "  neotlk-cli pad <input.tlk> <output.tlk> <target-strref>\n"
        << "  neotlk-cli set-language <input.tlk> <output.tlk> <0..5>\n"
        << "\n"
        << "Search options:\n"
        << "  --text TEXT           Text search criterion\n"
        << "  --resref RESREF       Sound ResRef substring criterion\n"
        << "  --start N             First StrRef to search\n"
        << "  --stop N              Last StrRef to search\n"
        << "  --case                Case-sensitive text search\n"
        << "  --negate              Negate text match\n"
        << "  --exact               Exact text match\n"
        << "  --words               Match individual words\n"
        << "  --new-only            Only entries marked custom/new in this session\n"
        << "  --no-blank            Exclude blank text entries\n"
        << "  --any-sound           Match entries with any sound value\n"
        << "\n"
        << "Entry options for add/edit:\n"
        << "  --text TEXT           Entry text\n"
        << "  --resref RESREF       TLK V3.0 sound ResRef, truncated to 16 bytes\n"
        << "  --sound-id N          TLK V4.0 numeric sound id\n"
        << "  --flags N             Raw flags value\n"
        << "  --text-present        Set TEXT_PRESENT\n"
        << "  --sound-present       Set SND_PRESENT\n"
        << "  --length-present      Set SNDLENGTH_PRESENT\n"
        << "  --volume N            Volume variance\n"
        << "  --pitch N             Pitch variance\n"
        << "  --sound-length F      Sound length float\n"
        << "\n"
        << "TLK patcher options:\n"
        << "  --tslpatcher          Stock-compatible append.tlk output (default)\n"
        << "  --holopatcher         Also allow replace.tlk edits to existing StrRefs\n"
        << "  Output is always a complete package containing the selected installer INI and required TLK payloads.\n"
        << "  --allow-unsupported   Emit a partial result despite unsupported changes\n";
}

bool isAsciiSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool isAsciiDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

std::string trimAscii(std::string value) {
    std::size_t begin = 0;
    while (begin < value.size() && isAsciiSpace(value[begin])) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && isAsciiSpace(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

UInt32 parseUInt32(const std::string& value, const std::string& label) {
    const std::string trimmed = trimAscii(value);
    if (trimmed.empty()) {
        throw neotlk::NeoTLKError("Invalid numeric value for " + label + ": " + value);
    }

    unsigned long long parsed = 0;
    for (char c : trimmed) {
        if (!isAsciiDigit(c)) {
            throw neotlk::NeoTLKError("Invalid numeric value for " + label + ": " + value);
        }
        parsed = parsed * 10ull + static_cast<unsigned long long>(c - '0');
        if (parsed > 0xffffffffull) {
            throw neotlk::NeoTLKError("Invalid numeric value for " + label + ": " + value);
        }
    }
    return static_cast<UInt32>(parsed);
}


std::filesystem::path cliPathFromString(const std::string& filename) {
    return std::filesystem::path(filename);
}

bool pathTextContainsEmbeddedNul(const std::string& value) {
    return value.find('\0') != std::string::npos;
}

bool existingPathsEquivalent(const std::string& left, const std::string& right) {
    if (pathTextContainsEmbeddedNul(left) || pathTextContainsEmbeddedNul(right)) {
        return false;
    }
    std::error_code ec;
    const std::filesystem::path leftPath = cliPathFromString(left);
    const std::filesystem::path rightPath = cliPathFromString(right);
    if (!std::filesystem::exists(leftPath, ec)) {
        return false;
    }
    ec.clear();
    if (!std::filesystem::exists(rightPath, ec)) {
        return false;
    }
    ec.clear();
    return std::filesystem::equivalent(leftPath, rightPath, ec) && !ec;
}

void rejectAppendOutputOverwritingAppendSource(const std::string& input,
                                               const std::string& append,
                                               const std::string& output) {
    // In-place append to the primary input file is intentional and safe because
    // the TLK is already fully loaded before atomic save. Overwriting the append
    // source is much easier to do by argument-order mistake and destroys the
    // secondary source file, so reject that ambiguous case unless input and
    // append are the same file.
    if (existingPathsEquivalent(append, output) && !existingPathsEquivalent(input, append)) {
        throw neotlk::NeoTLKError("Refusing to overwrite the append source file. Use the primary input file or a separate output path.");
    }
}

float parseFloat(const std::string& value, const std::string& label) {
    std::size_t used = 0;
    float parsed = 0.0f;
    try {
        parsed = std::stof(value, &used);
    } catch (const std::exception&) {
        throw neotlk::NeoTLKError("Invalid decimal value for " + label + ": " + value);
    }
    if (used != value.size() || !std::isfinite(parsed)) {
        throw neotlk::NeoTLKError("Invalid decimal value for " + label + ": " + value);
    }
    return parsed;
}

std::string requireValue(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw neotlk::NeoTLKError("Missing value after " + option);
    }
    ++index;
    return args[index];
}


struct PatchOutputOptions {
    bool allowUnsupported = false;
    std::string modifiedFormat = "auto";
    std::filesystem::path iniFilename = "changes.ini";
    neotlk::TlkPatcherCompatibility compatibility = neotlk::TlkPatcherCompatibility::TslPatcher;
};

PatchOutputOptions parsePatchOutputOptions(const std::vector<std::string>& args, std::size_t begin) {
    PatchOutputOptions options;
    for (std::size_t i = begin; i < args.size(); ++i) {
        const std::string& arg = args[i];
        // Retain --package as a backwards-compatible no-op. Complete package
        // output is the only supported NeoTLK patcher workflow.
        if (arg == "--package") continue;
        else if (arg == "--fragment") {
            throw neotlk::NeoTLKError(
                "NeoTLK does not expose fragment mode because usable TLK patches require "
                "append.tlk and/or replace.tlk payload files. Provide an output directory "
                "for a complete package instead.");
        }
        else if (arg == "--ini") {
            if (i + 1 >= args.size()) throw neotlk::NeoTLKError("--ini requires a filename.");
            options.iniFilename = args[++i];
        }
        else if (arg == "--allow-unsupported") options.allowUnsupported = true;
        else if (arg == "--tslpatcher" || arg == "--append-only") options.compatibility = neotlk::TlkPatcherCompatibility::TslPatcher;
        else if (arg == "--holopatcher" || arg == "--replace-existing") options.compatibility = neotlk::TlkPatcherCompatibility::HoloPatcher;
        else if (arg == "--modified-format" || arg == "--input-format") {
            if (i + 1 >= args.size()) throw neotlk::NeoTLKError(arg + " requires a value.");
            options.modifiedFormat = args[++i];
        }
        else throw neotlk::NeoTLKError("Unknown diff-tslpatcher option: " + arg);
    }
    return options;
}

void writeTlkPatchOutput(neotlk::TlkPatcherResult& result,
                         const std::filesystem::path& output,
                         const PatchOutputOptions& options) {
    const std::filesystem::path iniPath = options.iniFilename.is_absolute()
        ? options.iniFilename
        : output / options.iniFilename;
    neotlk::writeTlkPatcherPackageToIni(result, iniPath, options.allowUnsupported);
}

void printEntryTsv(const neotlk::TalkString& entry) {
    std::cout << entry.strRef << '\t'
              << neotlk::eraseCharacter(entry.text, '\r') << '\t'
              << entry.soundString() << '\n';
}

struct EntryOptionState {
    bool textSet = false;
    bool resrefSet = false;
    bool flagsSet = false;
    bool soundIdSet = false;
    bool volumeSet = false;
    bool pitchSet = false;
    bool soundLengthSet = false;
    bool rawFlagsSet = false;
    bool soundPresentOption = false;
    bool lengthPresentOption = false;
    std::string text;
    std::string resref;
    UInt32 flags = neotlk::TEXT_PRESENT | neotlk::SND_PRESENT | neotlk::SNDLENGTH_PRESENT;
    UInt32 soundId = 0xffffffffu;
    UInt32 volume = 0;
    UInt32 pitch = 0;
    float soundLength = 0.0f;
};

EntryOptionState parseEntryOptions(const std::vector<std::string>& args, std::size_t startIndex) {
    EntryOptionState state;
    for (std::size_t i = startIndex; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--text") {
            state.text = requireValue(args, i, arg);
            state.textSet = true;
        } else if (arg == "--resref") {
            state.resref = requireValue(args, i, arg);
            state.resrefSet = true;
        } else if (arg == "--flags") {
            state.flags = parseUInt32(requireValue(args, i, arg), arg);
            state.flagsSet = true;
            state.rawFlagsSet = true;
        } else if (arg == "--sound-id") {
            state.soundId = parseUInt32(requireValue(args, i, arg), arg);
            state.soundIdSet = true;
        } else if (arg == "--text-present") {
            state.flags |= neotlk::TEXT_PRESENT;
            state.flagsSet = true;
        } else if (arg == "--sound-present") {
            state.flags |= neotlk::SND_PRESENT;
            state.flagsSet = true;
            state.soundPresentOption = true;
        } else if (arg == "--length-present") {
            state.flags |= neotlk::SNDLENGTH_PRESENT;
            state.flagsSet = true;
            state.lengthPresentOption = true;
        } else if (arg == "--volume") {
            state.volume = parseUInt32(requireValue(args, i, arg), arg);
            state.volumeSet = true;
        } else if (arg == "--pitch") {
            state.pitch = parseUInt32(requireValue(args, i, arg), arg);
            state.pitchSet = true;
        } else if (arg == "--sound-length") {
            state.soundLength = parseFloat(requireValue(args, i, arg), arg);
            state.soundLengthSet = true;
        } else {
            throw neotlk::NeoTLKError("Unknown entry option: " + arg);
        }
    }
    return state;
}

void applyEntryOptions(neotlk::TalkString& entry, const EntryOptionState& options, bool forceDefaults) {
    if (forceDefaults || options.flagsSet) {
        entry.flags = options.flags;
    }
    if (forceDefaults || options.soundIdSet) {
        entry.soundId = options.soundId;
    }
    if (forceDefaults || options.volumeSet) {
        entry.volumeVariance = options.volume;
    }
    if (forceDefaults || options.pitchSet) {
        entry.pitchVariance = options.pitch;
    }
    if (forceDefaults || options.soundLengthSet) {
        entry.soundLength = options.soundLength;
    }
    if (forceDefaults || options.resrefSet) {
        entry.soundResref = neotlk::ResRef::fromString(options.resref);
    }
    if (forceDefaults || options.textSet) {
        entry.text = neotlk::eraseCharacter(options.text, '\r');
    }
}

void validateEntryOptionsForFormat(const neotlk::TalkTable& table,
                                   const EntryOptionState& options) {
    if (table.supportsSoundMetadata()) return;
    const bool rawSoundFlags = options.rawFlagsSet &&
        (options.flags & (neotlk::SND_PRESENT | neotlk::SNDLENGTH_PRESENT)) != 0u;
    if (options.resrefSet || options.soundIdSet || options.volumeSet || options.pitchSet ||
        options.soundLengthSet || options.soundPresentOption || options.lengthPresentOption || rawSoundFlags) {
        throw neotlk::NeoTLKError("Dragon Age TLK V0.2 stores string IDs and text only; sound metadata options are not applicable.");
    }
}

void normalizeEntryForFormat(const neotlk::TalkTable& table, neotlk::TalkString& entry) {
    if (!table.supportsSoundMetadata()) {
        entry.flags = entry.text.empty() ? 0u : neotlk::TEXT_PRESENT;
        entry.soundResref = {};
        entry.soundId = 0xffffffffu;
        entry.volumeVariance = 0u;
        entry.pitchVariance = 0u;
        entry.soundLength = 0.0f;
    }
}


std::string extensionImportFormat(const std::filesystem::path& path) {
    std::string ext = neotlk::asciiLower(path.extension().string());
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    if (ext == "csv" || ext == "tsv" || ext == "xml" || ext == "json") return ext;
    return "native";
}

bool isNativeTlkImportFormat(std::string formatName) {
    formatName = neotlk::asciiLower(std::move(formatName));
    return formatName == "native" || formatName == "kotor" || formatName == "tlk";
}

neotlk::TalkTable loadTlkFromImport(const std::filesystem::path& originalPath,
                                    const std::filesystem::path& inputPath,
                                    std::string formatName) {
    formatName = neotlk::asciiLower(std::move(formatName));
    if (formatName.empty() || formatName == "auto") formatName = extensionImportFormat(inputPath);
    if (isNativeTlkImportFormat(formatName)) {
        return neotlk::TalkTable(inputPath.string());
    }

    neotlk::TalkTable table(originalPath.string());
    const auto format = neotabular::parseFormat(formatName);
    if (format == neotabular::Format::Xml) {
        neotlk::applyXmlToTalkTable(table, neotlk::readTextFile(inputPath), false);
        return table;
    }
    const std::string jsonText = format == neotabular::Format::Json
        ? neotlk::readTextFile(inputPath)
        : std::string();
    const auto imported = format == neotabular::Format::Json
        ? neotlk::tlkTableFromJson(jsonText)
        : neotabular::readTable(inputPath, format);
    neotlk::applyTabularToTalkTable(
        table,
        imported,
        false,
        format == neotabular::Format::Json
            ? neotlk::TlkTabularApplyMode::Replace
            : neotlk::TlkTabularApplyMode::Merge);
    return table;
}

int runCommand(const std::vector<std::string>& args) {
    if (args.size() < 2 || args[1] == "--help" || args[1] == "-h" || args[1] == "help") {
        printUsage(std::cout);
        return 0;
    }

    const std::string command = args[1];

    if (command == "info") {
        if (args.size() != 3) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli info <file.tlk>");
        }
        neotlk::TalkTable table(args[2]);
        std::cout << "File: " << table.filename() << '\n'
                  << "Type: " << neotlk::fourCharToString(table.fileId()) << '\n'
                  << "Version: " << neotlk::fourCharToString(table.version()) << '\n'
                  << "Format: " << neotlk::storageFormatName(table.storageFormat()) << '\n'
                  << "Encoding: " << table.textEncodingSummary() << '\n';
        if (table.supportsLanguageId()) std::cout << "Language: " << table.language() << '\n';
        else std::cout << "Language: not stored by this format\n";
        std::cout << "Entries: " << table.count() << '\n';
        if (table.count() != 0u) {
            std::cout << "StrRefRange: " << table.minStrRef() << ".." << table.maxStrRef() << '\n';
        }
        std::cout << "StringDataOffset: " << table.stringEntriesOffset() << '\n';
        return 0;
    }

    if (command == "list") {
        if (args.size() < 3 || args.size() > 5) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli list <file.tlk> [start] [stop]");
        }
        neotlk::TalkTable table(args[2]);
        UInt32 start = table.count() > 0 ? table.minStrRef() : 0;
        UInt32 stop = table.count() > 0 ? table.maxStrRef() : 0;
        if (args.size() >= 4) {
            start = parseUInt32(args[3], "start");
        }
        if (args.size() >= 5) {
            stop = parseUInt32(args[4], "stop");
        }
        if (start > stop || (table.count() > 0 && stop > table.maxStrRef())) {
            throw neotlk::NeoTLKError("Invalid StrRef interval.");
        }
        std::cout << "StrRef\tEntry text\tSound Resref\n";
        for (const neotlk::TalkString& entry : table.entries()) {
            if (entry.strRef >= start && entry.strRef <= stop) {
                printEntryTsv(entry);
            }
        }
        return 0;
    }


    if (command == "export") {
        if (args.size() < 5 || args.size() > 6) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli export <file.tlk> <csv|tsv|xml|json> <output> [filter-term-for-csv-tsv]");
        }
        neotlk::TalkTable table(args[2]);
        const auto format = neotabular::parseFormat(args[3]);
        if ((format == neotabular::Format::Xml || format == neotabular::Format::Json) && args.size() == 6) {
            throw neotlk::NeoTLKError(
                "NeoTLK XML/JSON exports are complete semantic documents and cannot be filtered. Use CSV or TSV for filtered row exports.");
        }
        if (format == neotabular::Format::Xml) {
            neotlk::writeTextFile(args[4], neotlk::talkTableToXml(table));
        } else if (format == neotabular::Format::Json) {
            neotlk::writeTextFile(args[4], neotlk::talkTableToJson(table));
        } else {
            auto exported = neotlk::talkTableToTabular(table);
            if (args.size() == 6) {
                exported = neotabular::filterRows(exported, args[5]);
                neotlk::ensureTlkTabularMetadataRow(table, exported);
            }
            neotabular::writeTable(exported, args[4], format);
        }
        return 0;
    }

    if (command == "import") {
        if (args.size() < 5 || args.size() > 6) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli import <input-table> <csv|tsv|xml|json> <output.tlk> [language-id]");
        }
        neotlk::TalkTable table;
        table.newFile();
        const auto format = neotabular::parseFormat(args[3]);
        if (format == neotabular::Format::Xml) {
            const std::string xmlText = neotlk::readTextFile(args[2]);
            const auto declared = neotlk::storageFormatFromTlkXml(xmlText);
            if (declared && *declared == neotlk::TlkStorageFormat::DragonAgeV02) {
                throw neotlk::NeoTLKError(
                    "Standalone Dragon Age TLK import requires an existing native Dragon Age TLK as a backing document. Open the native TLK in NeoTLK and import into that document.");
            }
            neotlk::applyXmlToTalkTable(table, xmlText, true);
        } else {
            const std::string jsonText = format == neotabular::Format::Json
                ? neotlk::readTextFile(args[2])
                : std::string();
            const auto imported = format == neotabular::Format::Json
                ? neotlk::tlkTableFromJson(jsonText)
                : neotabular::readTable(args[2], format);
            const auto metadata = neotlk::inspectTlkTabularMetadata(imported);
            if (metadata.storageFormat && *metadata.storageFormat == neotlk::TlkStorageFormat::DragonAgeV02) {
                throw neotlk::NeoTLKError(
                    "Standalone Dragon Age TLK import requires an existing native Dragon Age TLK as a backing document. Open the native TLK in NeoTLK and import into that document.");
            }
            if (format == neotabular::Format::Json) {
                const auto declared = neotlk::tlkStorageFormatFromJson(jsonText);
                if (declared && *declared == neotlk::TlkStorageFormat::DragonAgeV02) {
                    throw neotlk::NeoTLKError(
                        "Standalone Dragon Age TLK import requires an existing native Dragon Age TLK as a backing document. Open the native TLK in NeoTLK and import into that document.");
                }
                if (declared) neotlk::applyDeclaredStorageFormat(table, *declared, true);
                if (table.supportsLanguageId()) {
                    table.setLanguage(neotlk::tlkLanguageFromJson(jsonText, table.language()));
                }
            }
            neotlk::applyTabularToTalkTable(
                table,
                imported,
                true,
                format == neotabular::Format::Json
                    ? neotlk::TlkTabularApplyMode::Replace
                    : neotlk::TlkTabularApplyMode::Merge);
        }
        if (args.size() == 6) {
            if (!table.supportsLanguageId()) {
                throw neotlk::NeoTLKError("Dragon Age GFF TLK V0.2 does not store a classic language ID.");
            }
            table.setLanguage(parseUInt32(args[5], "language"));
        }
        table.save(args[4]);
        return 0;
    }


    if (command == "diff-tslpatcher" || command == "diff-tslpatcher-import") {
        const bool explicitImport = command == "diff-tslpatcher-import";
        if ((!explicitImport && args.size() < 5) || (explicitImport && args.size() < 6)) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli diff-tslpatcher <original.tlk> <modified-input> <output-dir> [--modified-format csv|tsv|xml|json|tlk|kotor|native|auto] [--tslpatcher|--holopatcher] [--allow-unsupported]");
        }
        if (explicitImport) {
            auto options = parsePatchOutputOptions(args, 6);
            options.modifiedFormat = args[4];
            neotlk::TalkTable original(args[2]);
            neotlk::TalkTable modified = loadTlkFromImport(args[2], args[3], options.modifiedFormat);
            neotlk::TlkPatcherOptions patchOptions;
            patchOptions.compatibility = options.compatibility;
            auto result = neotlk::diffTlkForPatcher(original, modified, patchOptions);
            writeTlkPatchOutput(result, args[5], options);
            return 0;
        }
        neotlk::TalkTable original(args[2]);
        const auto options = parsePatchOutputOptions(args, 5);
        neotlk::TalkTable modified = loadTlkFromImport(args[2], args[3], options.modifiedFormat);
        neotlk::TlkPatcherOptions patchOptions;
        patchOptions.compatibility = options.compatibility;
        auto result = neotlk::diffTlkForPatcher(original, modified, patchOptions);
        writeTlkPatchOutput(result, args[4], options);
        return 0;
    }

    if (command == "search") {
        if (args.size() < 3) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli search <file.tlk> [options]");
        }
        neotlk::TalkTable table(args[2]);
        neotlk::SearchOptions options;
        options.start = table.count() > 0 ? table.minStrRef() : 0;
        options.stop = table.count() > 0 ? table.maxStrRef() : 0;

        for (std::size_t i = 3; i < args.size(); ++i) {
            const std::string& arg = args[i];
            if (arg == "--text") {
                options.text = requireValue(args, i, arg);
            } else if (arg == "--resref") {
                options.soundResRef = requireValue(args, i, arg);
            } else if (arg == "--start") {
                options.start = parseUInt32(requireValue(args, i, arg), arg);
            } else if (arg == "--stop") {
                options.stop = parseUInt32(requireValue(args, i, arg), arg);
            } else if (arg == "--case") {
                options.matchCase = true;
            } else if (arg == "--negate") {
                options.negateTextMatch = true;
            } else if (arg == "--exact") {
                options.exactTextMatch = true;
            } else if (arg == "--words") {
                options.wordSearch = true;
            } else if (arg == "--new-only") {
                options.newEntriesOnly = true;
            } else if (arg == "--no-blank") {
                options.filterBlankStrings = true;
            } else if (arg == "--any-sound") {
                options.matchEntriesWithAnySound = true;
            } else {
                throw neotlk::NeoTLKError("Unknown search option: " + arg);
            }
        }

        std::cout << "StrRef\tEntry text\tSound Resref\n";
        for (const neotlk::TalkString* entry : neotlk::searchEntries(table, options)) {
            printEntryTsv(*entry);
        }
        return 0;
    }

    if (command == "add") {
        if (args.size() < 4) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli add <input.tlk> <output.tlk> [entry-options]");
        }
        neotlk::TalkTable table(args[2]);
        neotlk::TalkString entry;
        const EntryOptionState options = parseEntryOptions(args, 4);
        validateEntryOptionsForFormat(table, options);
        applyEntryOptions(entry, options, true);
        normalizeEntryForFormat(table, entry);
        table.addEntry(std::move(entry));
        table.save(args[3]);
        return 0;
    }

    if (command == "edit") {
        if (args.size() < 5) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli edit <input.tlk> <output.tlk> <strref> [entry-options]");
        }
        neotlk::TalkTable table(args[2]);
        const UInt32 strRef = parseUInt32(args[4], "strref");
        neotlk::TalkString& entry = table.entryAtStrRef(strRef);
        const EntryOptionState options = parseEntryOptions(args, 5);
        validateEntryOptionsForFormat(table, options);
        applyEntryOptions(entry, options, false);
        normalizeEntryForFormat(table, entry);
        table.replaceEntry(entry);
        table.save(args[3]);
        return 0;
    }

    if (command == "delete") {
        if (args.size() != 5) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli delete <input.tlk> <output.tlk> <strref>");
        }
        neotlk::TalkTable table(args[2]);
        table.deleteEntry(parseUInt32(args[4], "strref"));
        table.save(args[3]);
        return 0;
    }

    if (command == "append") {
        if (args.size() != 5) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli append <input.tlk> <append.tlk> <output.tlk>");
        }
        rejectAppendOutputOverwritingAppendSource(args[2], args[3], args[4]);
        neotlk::TalkTable table(args[2]);
        neotlk::TalkTable append(args[3]);
        table.appendFrom(append);
        table.save(args[4]);
        return 0;
    }

    if (command == "pad") {
        if (args.size() != 5) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli pad <input.tlk> <output.tlk> <target-strref>");
        }
        neotlk::TalkTable table(args[2]);
        table.padToStrRef(parseUInt32(args[4], "target-strref"));
        table.save(args[3]);
        return 0;
    }

    if (command == "set-language") {
        if (args.size() != 5) {
            throw neotlk::NeoTLKError("Usage: neotlk-cli set-language <input.tlk> <output.tlk> <0..5>");
        }
        neotlk::TalkTable table(args[2]);
        const UInt32 language = parseUInt32(args[4], "language");
        if (language > 5) {
            throw neotlk::NeoTLKError("Unsupported language id " + std::to_string(language) + " encountered. Unable to save.");
        }
        table.setLanguage(language);
        table.save(args[3]);
        return 0;
    }

    throw neotlk::NeoTLKError("Unknown command: " + command);
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> args;
        args.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
        return runCommand(args);
    } catch (const neotlk::NeoTLKError& error) {
        std::cerr << "ERROR! " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "ERROR! " << error.what() << '\n';
        return 1;
    }
}

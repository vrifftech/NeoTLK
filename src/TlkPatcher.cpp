// SPDX-License-Identifier: GPL-3.0-or-later

#include "neotlk/TlkPatcher.hpp"

#include "neotlk/StringUtil.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace neotlk {
namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool iequals(const std::string& left, const std::string& right) {
    return lowerAscii(left) == lowerAscii(right);
}

void validatePackageFilename(const std::string& filename, const char* label) {
    const std::filesystem::path path(filename);
    const bool hasPortableInvalidCharacter = std::any_of(filename.begin(), filename.end(), [](unsigned char ch) {
        return ch < 0x20u || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
               ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*' ||
               ch == '[' || ch == ']' || ch == '=' || ch == ';';
    });
    if (filename.empty() || path.filename() != path || path == "." || path == ".." ||
        hasPortableInvalidCharacter || filename.back() == '.' || filename.back() == ' ') {
        throw NeoTLKError(std::string(label) +
                          " must be a portable plain filename without directory or INI syntax characters.");
    }
    if (lowerAscii(path.extension().string()) != ".tlk") {
        throw NeoTLKError(std::string(label) + " must use the .tlk extension.");
    }
}

void validatePackageOptions(const TlkPatcherOptions& options) {
    validatePackageFilename(options.appendFilename, "Append TLK filename");
    validatePackageFilename(options.replacementFilename, "Replacement TLK filename");
    if (options.appendFilename != "append.tlk") {
        throw NeoTLKError("Stock TSLPatcher requires the append table to be named exactly append.tlk.");
    }
    if (iequals(options.appendFilename, options.replacementFilename)) {
        throw NeoTLKError("Append and replacement TLK filenames must be different.");
    }
}

std::filesystem::path knownTablePath(const TalkTable& table) {
    if (table.hasSaveTarget() && !table.saveTargetFilename().empty()) {
        return std::filesystem::path(table.saveTargetFilename());
    }
    if (!table.filename().empty()) {
        return std::filesystem::path(table.filename());
    }
    return {};
}

void rememberInputPath(TlkPatcherResult& result, const TalkTable& table) {
    const std::filesystem::path path = knownTablePath(table);
    if (path.empty()) return;
    const auto normalized = std::filesystem::absolute(path).lexically_normal();
    const auto duplicate = std::find(result.protectedInputFiles.begin(), result.protectedInputFiles.end(), normalized);
    if (duplicate == result.protectedInputFiles.end()) {
        result.protectedInputFiles.push_back(normalized);
    }
}

bool pathsReferToSameFile(const std::filesystem::path& left, const std::filesystem::path& right) {
    if (left.empty() || right.empty()) return false;
    std::error_code ec;
    const bool equivalent = std::filesystem::equivalent(left, right, ec);
    if (!ec && equivalent) return true;
    ec.clear();
    const auto absoluteLeft = std::filesystem::absolute(left, ec).lexically_normal();
    if (ec) return false;
    ec.clear();
    const auto absoluteRight = std::filesystem::absolute(right, ec).lexically_normal();
    return !ec && absoluteLeft == absoluteRight;
}

void rejectInputOverwrite(const TlkPatcherResult& result,
                          const std::vector<std::filesystem::path>& outputFiles) {
    for (const auto& outputFile : outputFiles) {
        for (const auto& inputFile : result.protectedInputFiles) {
            if (pathsReferToSameFile(outputFile, inputFile)) {
                throw NeoTLKError("Refusing to overwrite a TLK comparison input while generating the package: " +
                                  outputFile.string());
            }
        }
    }
}

void initializePatchTable(TalkTable& output, const TalkTable& source) {
    output.newFile();
    output.setVersion30();
    output.setLanguage(source.language());
}

void appendClone(TalkTable& output, const TalkString& source) {
    const UInt32 outputStrRef = output.count();
    TalkString clone;
    clone.cloneFrom(source);
    output.addEntry(std::move(clone));
    // TalkTable::addEntry normally adopts the destination table's preferred
    // encoding for UTF-8/empty entries. A patch table must instead preserve
    // the modified entry's selected on-disk encoding exactly.
    output.entryAtStrRef(outputStrRef).textEncoding = source.textEncoding;
}


void remapNumericSectionValues(neotsl::PatchProject& project,
                               const std::string& sectionName,
                               const std::vector<std::size_t>& indexMap,
                               bool strRefKeysOnly) {
    auto* section = const_cast<neotsl::IniSection*>(project.findSection(sectionName));
    if (!section) return;
    for (auto& entry : section->entries) {
        if (strRefKeysOnly) {
            if (entry.key.size() <= 6u || lowerAscii(entry.key.substr(0u, 6u)) != "strref") continue;
            const std::string suffix = entry.key.substr(6u);
            if (!std::all_of(suffix.begin(), suffix.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) continue;
        }
        try {
            const std::size_t sourceIndex = static_cast<std::size_t>(std::stoull(entry.value));
            if (sourceIndex >= indexMap.size()) {
                throw NeoTLKError("Generated TLK patch section refers to a payload index outside the generated table: " + entry.value);
            }
            entry.value = std::to_string(indexMap[sourceIndex]);
        } catch (const NeoTLKError&) {
            throw;
        } catch (...) {
            throw NeoTLKError("Generated TLK patch section contains a nonnumeric payload index: " + entry.value);
        }
    }
}

std::size_t matchingSuffixPrefix(const TalkTable& existing, const TalkTable& incoming) {
    const std::size_t limit = std::min<std::size_t>(existing.count(), incoming.count());
    for (std::size_t length = limit; length > 0u; --length) {
        const std::size_t existingStart = existing.count() - length;
        bool matches = true;
        for (std::size_t index = 0u; index < length; ++index) {
            if (!talkStringsEquivalentForPatcher(
                    existing.entryAtStrRef(static_cast<UInt32>(existingStart + index)),
                    incoming.entryAtStrRef(static_cast<UInt32>(index)))) {
                matches = false;
                break;
            }
        }
        if (matches) return length;
    }
    return 0u;
}

std::vector<std::size_t> mergePatchTable(TalkTable& destination, const TalkTable& source) {
    const std::size_t overlap = matchingSuffixPrefix(destination, source);
    const std::size_t existingStart = destination.count() - overlap;
    std::vector<std::size_t> indexMap(source.count());
    for (std::size_t index = 0u; index < overlap; ++index) {
        indexMap[index] = existingStart + index;
    }
    for (std::size_t index = overlap; index < source.count(); ++index) {
        indexMap[index] = destination.count();
        appendClone(destination, source.entryAtStrRef(static_cast<UInt32>(index)));
    }
    return indexMap;
}

void savePatchTableAtomically(TalkTable& table, const std::filesystem::path& target) {
    const std::filesystem::path temporary = target.string() + ".neo-tmp";
    const std::filesystem::path backup = target.string() + ".neo-bak";
    table.save(temporary.string());
    std::error_code ec;
    if (std::filesystem::exists(target, ec) && !ec) {
        std::filesystem::remove(backup, ec);
        ec.clear();
        std::filesystem::rename(target, backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            throw NeoTLKError("Unable to prepare existing TLK payload for replacement: " + ec.message());
        }
        std::filesystem::rename(temporary, target, ec);
        if (ec) {
            std::error_code ignored;
            std::filesystem::rename(backup, target, ignored);
            std::filesystem::remove(temporary, ignored);
            throw NeoTLKError("Unable to replace TLK package payload: " + ec.message());
        }
        std::filesystem::remove(backup, ec);
    } else {
        std::filesystem::rename(temporary, target, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            throw NeoTLKError("Unable to install TLK package payload: " + ec.message());
        }
    }
}

TalkTable loadOrCreatePatchTable(const std::filesystem::path& path,
                                 UInt32 language) {
    TalkTable table;
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        table.load(path.string());
        if (table.isVersion40() || table.isDragonAgeV02() || table.language() != language) {
            throw NeoTLKError(
                "The existing " + path.filename().string() +
                " is not a compatible KotOR TLK V3.0 table with the same language ID.");
        }
    } else {
        table.newFile();
        table.setVersion30();
        table.setLanguage(language);
    }
    return table;
}

} // namespace

const char* tlkPatcherCompatibilityName(TlkPatcherCompatibility compatibility) noexcept {
    switch (compatibility) {
    case TlkPatcherCompatibility::TslPatcher: return "TSLPatcher";
    case TlkPatcherCompatibility::HoloPatcher: return "HoloPatcher";
    }
    return "TLK patcher";
}

bool talkStringsEquivalentForPatcher(const TalkString& left, const TalkString& right) {
    return left.flags == right.flags
        && left.soundResref == right.soundResref
        && left.volumeVariance == right.volumeVariance
        && left.pitchVariance == right.pitchVariance
        && left.soundLength == right.soundLength
        && left.soundId == right.soundId
        && eraseCharacter(left.text, '\r') == eraseCharacter(right.text, '\r');
}

TlkPatcherResult diffTlkForPatcher(const TalkTable& original,
                                    const TalkTable& modified,
                                    const TlkPatcherOptions& options) {
    validatePackageOptions(options);

    TlkPatcherResult result;
    result.options = options;
    rememberInputPath(result, original);
    rememberInputPath(result, modified);
    initializePatchTable(result.appendTable, modified);
    initializePatchTable(result.replacementTable, modified);

    if (original.isDragonAgeV02() || modified.isDragonAgeV02()) {
        result.project.unsupported.push_back(
            "Dragon Age GFF TLK V0.2 uses sparse string IDs and is not compatible with the KotOR append.tlk patching model.");
        return result;
    }
    if (original.isVersion40() || modified.isVersion40()) {
        result.project.unsupported.push_back(
            "Jade Empire TLK V4.0 is not supported by TSLPatcher/HoloPatcher's KotOR dialog.tlk workflow.");
        return result;
    }
    if (original.storageFormat() != modified.storageFormat()) {
        result.project.unsupported.push_back("The original and modified TLK files use different native TLK formats.");
        return result;
    }
    if (original.language() != modified.language()) {
        result.project.unsupported.push_back(
            "Changing the TLK language ID is not representable by append.tlk or replace.tlk patch instructions.");
    }

    const UInt32 commonCount = std::min(original.count(), modified.count());
    bool replacementFileRegistered = false;
    for (UInt32 strRef = 0; strRef < commonCount; ++strRef) {
        const TalkString& before = original.entryAtStrRef(strRef);
        const TalkString& after = modified.entryAtStrRef(strRef);
        if (talkStringsEquivalentForPatcher(before, after)) continue;

        if (options.compatibility == TlkPatcherCompatibility::TslPatcher) {
            result.project.unsupported.push_back(
                "Modified existing TLK entry is not stock TSLPatcher-compatible: StrRef " + std::to_string(strRef));
            continue;
        }

        if (!replacementFileRegistered) {
            result.project.add("TLKList", "ReplaceFile0", options.replacementFilename);
            replacementFileRegistered = true;
        }
        const std::size_t replacementIndex = result.replacedEntries;
        appendClone(result.replacementTable, after);
        result.project.add(options.replacementFilename,
                           std::to_string(strRef),
                           std::to_string(replacementIndex));
        ++result.replacedEntries;
    }

    if (modified.count() < original.count()) {
        result.project.unsupported.push_back(
            "TLK entry deletion or truncation is not supported by TSLPatcher or HoloPatcher TLK patch instructions.");
    }

    for (UInt32 strRef = original.count(); strRef < modified.count(); ++strRef) {
        const std::size_t appendIndex = result.appendedEntries;
        appendClone(result.appendTable, modified.entryAtStrRef(strRef));
        result.project.add("TLKList",
                           "StrRef" + std::to_string(appendIndex),
                           std::to_string(appendIndex));
        ++result.appendedEntries;
    }

    if (options.compatibility == TlkPatcherCompatibility::HoloPatcher && result.hasReplacementTable()) {
        result.project.warnings.push_back(
            "Existing-entry replacement uses HoloPatcher's replace.tlk extension and is not compatible with the original TSLPatcher executable.");
    }
    if (!result.hasPatchableChanges() && result.project.unsupported.empty()) {
        result.project.warnings.push_back("No appended or replaced TLK entries were detected.");
    }

    return result;
}

void writeTlkPatcherPackageToIni(TlkPatcherResult& result,
                                 const std::filesystem::path& outputIni,
                                 bool allowUnsupported) {
    const TlkPatcherOptions& options = result.options;
    validatePackageOptions(options);
    if (!allowUnsupported) neotsl::throwIfUnsupported(result.project);
    else neotsl::printReport(result.project);

    const std::filesystem::path iniPath = outputIni.extension().empty()
        ? std::filesystem::path(outputIni.string() + ".ini")
        : outputIni;
    const std::filesystem::path outputDirectory = iniPath.parent_path().empty()
        ? std::filesystem::current_path()
        : iniPath.parent_path();
    (void)neotsl::preflightIniMerge(result.project, iniPath, true);

    std::error_code ec;
    std::filesystem::create_directories(outputDirectory, ec);
    if (ec) {
        throw NeoTLKError("Unable to create TLK patcher package folder: " + outputDirectory.string() + ": " + ec.message());
    }

    std::vector<std::filesystem::path> generatedFiles{iniPath, outputDirectory / "info.rtf"};
    if (result.hasAppendTable()) generatedFiles.push_back(outputDirectory / options.appendFilename);
    if (result.hasReplacementTable()) generatedFiles.push_back(outputDirectory / options.replacementFilename);
    rejectInputOverwrite(result, generatedFiles);

    if (result.hasAppendTable()) {
        const std::filesystem::path appendPath = outputDirectory / options.appendFilename;
        TalkTable merged = loadOrCreatePatchTable(appendPath, result.appendTable.language());
        const UInt32 beforeCount = merged.count();
        const auto indexMap = mergePatchTable(merged, result.appendTable);
        remapNumericSectionValues(result.project, "TLKList", indexMap, true);
        if (merged.count() != beforeCount) savePatchTableAtomically(merged, appendPath);
    }
    if (result.hasReplacementTable()) {
        const std::filesystem::path replacementPath = outputDirectory / options.replacementFilename;
        TalkTable merged = loadOrCreatePatchTable(replacementPath, result.replacementTable.language());
        const UInt32 beforeCount = merged.count();
        const auto indexMap = mergePatchTable(merged, result.replacementTable);
        remapNumericSectionValues(result.project, options.replacementFilename, indexMap, false);
        if (merged.count() != beforeCount) savePatchTableAtomically(merged, replacementPath);
    }
    neotsl::writePackageToIni(result.project, iniPath, true);
}

void writeTlkPatcherPackage(TlkPatcherResult& result,
                            const std::filesystem::path& outputDirectory,
                            bool allowUnsupported) {
    writeTlkPatcherPackageToIni(result, outputDirectory / "changes.ini", allowUnsupported);
}

} // namespace neotlk

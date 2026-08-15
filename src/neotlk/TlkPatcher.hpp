// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "neotlk/TlkFile.hpp"
#include "TslPatcher.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace neotlk {

enum class TlkPatcherCompatibility {
    TslPatcher,
    HoloPatcher,
};

struct TlkPatcherOptions {
    TlkPatcherCompatibility compatibility = TlkPatcherCompatibility::TslPatcher;
    std::string appendFilename = "append.tlk";
    std::string replacementFilename = "replace.tlk";
};

struct TlkPatcherResult {
    TlkPatcherOptions options;
    neotsl::PatchProject project;
    TalkTable appendTable;
    TalkTable replacementTable;
    std::size_t appendedEntries = 0;
    std::size_t replacedEntries = 0;
    std::vector<std::filesystem::path> protectedInputFiles;

    bool hasAppendTable() const noexcept { return appendedEntries != 0u; }
    bool hasReplacementTable() const noexcept { return replacedEntries != 0u; }
    bool hasPatchableChanges() const noexcept { return hasAppendTable() || hasReplacementTable(); }
};

const char* tlkPatcherCompatibilityName(TlkPatcherCompatibility compatibility) noexcept;
bool talkStringsEquivalentForPatcher(const TalkString& left, const TalkString& right);

TlkPatcherResult diffTlkForPatcher(const TalkTable& original,
                                    const TalkTable& modified,
                                    const TlkPatcherOptions& options = {});

[[deprecated("Use writeTlkPatcherPackageToIni() with the exact selected installer INI path")]]
void writeTlkPatcherPackage(TlkPatcherResult& result,
                            const std::filesystem::path& outputDirectory,
                            bool allowUnsupported = false);

void writeTlkPatcherPackageToIni(TlkPatcherResult& result,
                                 const std::filesystem::path& outputIni,
                                 bool allowUnsupported = false);

} // namespace neotlk

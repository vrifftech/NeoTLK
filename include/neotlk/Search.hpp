// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "neotlk/TlkFile.hpp"

namespace neotlk {

struct SearchOptions {
    std::string text;
    std::string soundResRef;
    UInt32 start = 0;
    UInt32 stop = 0;
    bool matchCase = false;
    bool negateTextMatch = false;
    bool exactTextMatch = false;
    bool wordSearch = false;
    bool newEntriesOnly = false;
    bool filterBlankStrings = false;
    bool matchEntriesWithAnySound = false;
};

bool checkWordMatch(const std::string& criteria, const std::string& data);
std::vector<UInt32> searchStrRefs(const TalkTable& table, const SearchOptions& options);
std::vector<const TalkString*> searchEntries(const TalkTable& table, const SearchOptions& options);

} // namespace neotlk

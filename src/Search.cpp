// SPDX-License-Identifier: GPL-3.0-or-later

#include "neotlk/Search.hpp"

#include "neotlk/StringUtil.hpp"

#include <algorithm>
#include <vector>

namespace neotlk {

bool checkWordMatch(const std::string& criteria, const std::string& data) {
    if (criteria.size() < 3 || criteria.find(' ') == std::string::npos ||
        data.size() < 3 || data.find(' ') == std::string::npos) {
        return criteria == data;
    }

    const std::vector<std::string> criteriaTokens = splitNonEmpty(criteria, ' ');
    const std::vector<std::string> dataTokens = splitNonEmpty(data, ' ');

    if (dataTokens.empty()) {
        return false;
    }
    if (criteriaTokens.empty()) {
        return true;
    }

    for (const std::string& token : criteriaTokens) {
        int matches = 0;
        const std::string criteriaWord = stripOneTrailingSearchPunctuation(token);

        for (const std::string& dataToken : dataTokens) {
            const std::string dataWord = stripOneTrailingSearchPunctuation(dataToken);
            if (criteriaWord == dataWord) {
                ++matches;
            }
        }

        if (matches == 0) {
            return false;
        }
    }

    return true;
}

std::vector<UInt32> searchStrRefs(const TalkTable& table, const SearchOptions& options) {
    if (options.start > options.stop) {
        throw NeoTLKError("Invalid start value of interval to search!");
    }

    if (table.count() > 0 && options.stop > table.maxStrRef()) {
        throw NeoTLKError("Invalid end value of interval to search!");
    }

    std::vector<UInt32> result;
    for (const TalkString& entry : table.entries()) {
        if (entry.strRef > options.stop) {
            continue;
        }
        if (entry.strRef < options.start) {
            continue;
        }

        const std::string searchedText = options.matchCase ? entry.text : asciiLower(entry.text);
        const std::string searchText = options.matchCase ? options.text : asciiLower(options.text);
        const bool containsText = searchedText.find(searchText) != std::string::npos;
        const bool exactText = (searchText == searchedText);
        const bool wordMatch = checkWordMatch(searchText, searchedText);

        const bool passText = options.text.empty() ||
            (!options.negateTextMatch &&
                ((options.wordSearch && wordMatch) ||
                 ((!options.exactTextMatch && containsText) || (options.exactTextMatch && exactText)))) ||
            (options.negateTextMatch &&
                ((options.wordSearch && !wordMatch) ||
                 ((!options.exactTextMatch && !containsText) || (options.exactTextMatch && !exactText))));

        const std::string sound = entry.soundString();
        const std::string directSound = entry.soundString();
        const bool passBlank = !options.filterBlankStrings || !entry.text.empty();
        const bool passNew = !options.newEntriesOnly || entry.custom;
        const bool passAnySound = !options.matchEntriesWithAnySound || !sound.empty();
        const bool passSoundResRef = options.soundResRef.empty() ||
            (asciiLower(directSound).find(asciiLower(options.soundResRef)) != std::string::npos);

        if (passBlank && passNew && passAnySound && passText && passSoundResRef) {
            result.push_back(entry.strRef);
        }
    }

    return result;
}

std::vector<const TalkString*> searchEntries(const TalkTable& table, const SearchOptions& options) {
    const std::vector<UInt32> refs = searchStrRefs(table, options);
    std::vector<const TalkString*> result;
    result.reserve(refs.size());
    for (UInt32 ref : refs) {
        result.push_back(&table.entryAtStrRef(ref));
    }
    return result;
}

} // namespace neotlk

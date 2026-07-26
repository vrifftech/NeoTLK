// SPDX-License-Identifier: GPL-3.0-or-later

#include "neotlk/StringUtil.hpp"

#include <algorithm>

namespace neotlk {

std::string eraseCharacter(std::string text, char value) {
    text.erase(std::remove(text.begin(), text.end(), value), text.end());
    return text;
}

std::vector<std::string> splitNonEmpty(std::string_view text, char delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(delimiter, start);
        const std::size_t stop = (end == std::string_view::npos) ? text.size() : end;
        if (stop > start) {
            result.emplace_back(text.substr(start, stop - start));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

std::string asciiLower(std::string value) {
    // Keep search folding byte-oriented for the ASCII A..Z range. Avoid
    // locale-sensitive std::tolower behavior here.
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

std::string stripOneTrailingSearchPunctuation(std::string value) {
    if (!value.empty()) {
        const char c = value.back();
        if (c == '.' || c == '!' || c == '?' || c == ',') {
            value.pop_back();
        }
    }
    return value;
}

} // namespace neotlk

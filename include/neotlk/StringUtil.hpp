// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace neotlk {

std::string eraseCharacter(std::string text, char value);
std::vector<std::string> splitNonEmpty(std::string_view text, char delimiter);
std::string asciiLower(std::string value);
std::string stripOneTrailingSearchPunctuation(std::string value);


} // namespace neotlk

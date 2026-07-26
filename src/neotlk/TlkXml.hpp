// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "neotlk/TlkFile.hpp"

#include <optional>
#include <string>

namespace neotlk {

// Structured XML is a complete semantic TLK document, not a filtered table.
std::string talkTableToXml(const TalkTable& table);
void applyXmlToTalkTable(TalkTable& table,
                         const std::string& xmlText,
                         bool allowClassicFormatChange = false);
UInt32 languageFromTlkXml(const std::string& xmlText, UInt32 fallback);
std::optional<TlkStorageFormat> storageFormatFromTlkXml(const std::string& xmlText);

} // namespace neotlk

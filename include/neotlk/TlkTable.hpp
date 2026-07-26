// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "TabularData.hpp"
#include "neotlk/TlkFile.hpp"

#include <optional>

namespace neotlk {

struct TlkTabularMetadata {
    std::optional<TlkStorageFormat> storageFormat;
    std::optional<UInt32> languageId;
};

enum class TlkTabularApplyMode {
    Merge,
    Replace,
};

neotabular::Table talkTableToTabular(const TalkTable& table);
void ensureTlkTabularMetadataRow(const TalkTable& table, neotabular::Table& exported);
TlkTabularMetadata inspectTlkTabularMetadata(const neotabular::Table& table);

// Imported metadata may switch between the two classic TLK families only when
// allowClassicFormatChange is true. Dragon Age TLKs always require an already
// loaded native Dragon Age table so its GFF backing structure can be preserved.
void applyDeclaredStorageFormat(TalkTable& table,
                                TlkStorageFormat declaredFormat,
                                bool allowClassicFormatChange);
void applyTabularToTalkTable(TalkTable& table,
                             const neotabular::Table& imported,
                             bool allowClassicFormatChange = false,
                             TlkTabularApplyMode mode = TlkTabularApplyMode::Merge);

} // namespace neotlk

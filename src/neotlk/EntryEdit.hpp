// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "neotlk/TlkFile.hpp"
#include <optional>

namespace neotlk {
// Changes are explicit; absent values preserve the original metadata/bytes.
struct EntryChanges {
    std::optional<std::string> text;
    std::optional<UInt32> visibleFlags;
    std::optional<std::string> sound;
    std::optional<UInt32> volume, pitch;
    std::optional<float> length;
};
TalkString applyEntryChanges(const TalkString& original, const EntryChanges& changes,
                            bool numericSoundId, bool soundMetadata);
} // namespace neotlk

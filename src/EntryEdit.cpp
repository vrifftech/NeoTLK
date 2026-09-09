// SPDX-License-Identifier: GPL-3.0-or-later
#include "neotlk/EntryEdit.hpp"
#include <charconv>
#include <cmath>

namespace neotlk {
TalkString applyEntryChanges(const TalkString& original, const EntryChanges& changes,
                            bool numericSoundId, bool soundMetadata) {
    TalkString result = original;
    if (changes.text) {
        if (soundMetadata && !numericSoundId && changes.text->find('\0') != std::string::npos)
            throw NeoTLKError("Native TLK text cannot contain an embedded NUL: the game hides everything after it. Remove it deliberately or cancel the edit.");
        if (!isValidUtf8(*changes.text)) throw NeoTLKError("Edited text must be valid Unicode.");
        result.text = *changes.text;
    }
    if (soundMetadata) {
        if (changes.visibleFlags) {
            constexpr UInt32 mask = TEXT_PRESENT | SND_PRESENT | SNDLENGTH_PRESENT;
            result.flags = (original.flags & ~mask) | (*changes.visibleFlags & mask);
        }
        if (changes.sound) {
            if (numericSoundId) {
                UInt32 id = 0xffffffffu;
                if (!changes.sound->empty()) {
                    const auto& s = *changes.sound;
                    const auto parsed = std::from_chars(s.data(), s.data() + s.size(), id);
                    if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size())
                        throw NeoTLKError("Invalid numeric sound ID.");
                }
                result.soundId = id;
            } else result.soundResref = ResRef::fromString(*changes.sound);
        }
        if (changes.volume) result.volumeVariance = *changes.volume;
        if (changes.pitch) result.pitchVariance = *changes.pitch;
        if (changes.length) {
            if (!std::isfinite(*changes.length)) throw NeoTLKError("Edited sound length must be finite.");
            result.soundLength = *changes.length;
        }
    }
    return result;
}
} // namespace neotlk

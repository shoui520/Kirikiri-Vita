#pragma once

#include <cstddef>
#include <cstdint>

namespace krkrvita {

// Yuri already keeps exactly one measured string on each layer bitmap.  KAG's
// HistoryLayer grows currentLine one UTF-16 code unit at a time, so that one
// entry is also the only prefix worth retaining.  A prefix may be reused only
// while the rasterizer reports the identical, fully initialized metric state.
// Exact code-unit comparison makes hash collisions impossible.
struct YuriTextWidthPrefixReuse {
    std::size_t code_units = 0;
    std::uint32_t width = 0;
    bool reused = false;
};

template <typename Character>
constexpr YuriTextWidthPrefixReuse yuri_find_text_width_prefix(
    const Character* cached_text, std::size_t cached_length,
    std::uint32_t cached_width, bool cached_state_valid,
    std::uint64_t cached_state, const Character* text,
    std::size_t text_length, bool state_valid,
    std::uint64_t state) noexcept {
    if (!cached_state_valid || !state_valid || cached_state != state ||
        cached_length == 0 || cached_length > text_length) {
        return {};
    }

    for (std::size_t i = 0; i < cached_length; ++i) {
        // Yuri measures with while(*buf), not the logical ttstr length.  A
        // cached string containing an embedded NUL therefore has no reusable
        // prefix beyond its first terminator.
        if (cached_text[i] == Character{}) return {};
        if (cached_text[i] != text[i]) return {};
    }

    return {cached_length, cached_width, true};
}

}  // namespace krkrvita

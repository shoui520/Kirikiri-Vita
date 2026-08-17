#pragma once

#include <cstddef>
#include <cstdint>

namespace krkrvita {

struct RgbaFrameProbe {
    std::uint64_t color_pixels = 0;
    std::uint64_t opaque_pixels = 0;
    std::uint32_t first_pixel = 0;
    std::uint32_t center_pixel = 0;

    [[nodiscard]] bool has_visible_color() const { return color_pixels != 0; }
};

// Yuri's mobile compositor applies TVP_REVRGB and emits little-endian
// AABBGGRR words, i.e. RGBA bytes in memory. Scan the real pitched surface,
// not an assumed packed
// copy. This is deliberately a complete scan: it runs only until the first
// visibly coloured frame and is the hardware acceptance boundary which keeps
// an opaque-black bootstrap buffer from masquerading as working video.
inline RgbaFrameProbe probe_rgba_frame(const void* pixels, int pitch,
                                       int width, int height) {
    RgbaFrameProbe result;
    if (!pixels || width <= 0 || height <= 0 || pitch < width * 4 ||
        (pitch & 3) != 0)
        return result;

    const auto* source = static_cast<const std::uint8_t*>(pixels);
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            source + static_cast<std::size_t>(y) * pitch);
        for (int x = 0; x < width; ++x) {
            const std::uint32_t pixel = row[x];
            result.color_pixels += (pixel & 0x00ffffffu) != 0;
            result.opaque_pixels += (pixel & 0xff000000u) == 0xff000000u;
        }
    }
    result.first_pixel = *static_cast<const std::uint32_t*>(pixels);
    const auto* center_row = reinterpret_cast<const std::uint32_t*>(
        source + static_cast<std::size_t>(height / 2) * pitch);
    result.center_pixel = center_row[width / 2];
    return result;
}

} // namespace krkrvita

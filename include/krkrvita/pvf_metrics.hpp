#pragma once

namespace krkrvita {

constexpr int pvf_kirikiri_raster_growth = 2;
constexpr int pvf_kirikiri_baseline_correction = 10;

constexpr int pvf_kirikiri_raster_height(int font_height) {
    if (font_height <= 0) return 0;
    const int enlarged = font_height + pvf_kirikiri_raster_growth;
    return enlarged > 256 ? 256 : enlarged;
}

// Kirikiri positions a glyph relative to the logical font-cell ascent used by
// Windows text metrics. Vita's PVF reports the tight vector-face ascender and
// therefore supplies no equivalent internal leading. Reserve one sixth of the
// requested cell height above the PVF glyph, matching the space CJK GDI fonts
// ordinarily contribute instead of pinning glyph pixels to the line top.
constexpr int pvf_kirikiri_internal_leading(int font_height) {
    return font_height > 0 ? (font_height + 5) / 6 : 0;
}

constexpr int pvf_kirikiri_baseline(int font_height, int pvf_ascent) {
    return pvf_ascent + pvf_kirikiri_internal_leading(font_height) +
           (font_height > 0 ? pvf_kirikiri_baseline_correction : 0);
}

} // namespace krkrvita

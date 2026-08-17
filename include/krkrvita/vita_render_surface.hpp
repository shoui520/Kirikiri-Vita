#pragma once

#include <algorithm>

namespace krkrvita {

inline constexpr int kVitaDisplayWidth = 960;
inline constexpr int kVitaDisplayHeight = 544;

struct VitaRenderSurface {
    int width = 1;
    int height = 1;
    float scale_width = 1.0f;
    float scale_height = 1.0f;

    constexpr bool is_scaled() const {
        return scale_width < 1.0f || scale_height < 1.0f;
    }
};

// Yuri's mobile renderer represents a logical Kirikiri layer separately from
// its mutable GPU backing surface. Fit only that backing surface to the Vita's
// physical display while preserving aspect ratio and logical coordinates.
// Source textures remain at their decoded resolution, so crop/zoom operations
// still sample the original artwork.
constexpr VitaRenderSurface fit_vita_render_surface(int logical_width,
                                                     int logical_height) {
    if (logical_width <= 0 || logical_height <= 0) return {};
    if (logical_width <= kVitaDisplayWidth &&
        logical_height <= kVitaDisplayHeight) {
        return {logical_width, logical_height, 1.0f, 1.0f};
    }

    const double width_scale =
        static_cast<double>(kVitaDisplayWidth) / logical_width;
    const double height_scale =
        static_cast<double>(kVitaDisplayHeight) / logical_height;
    const double uniform_scale =
        width_scale < height_scale ? width_scale : height_scale;
    const int width = std::max(
        1, static_cast<int>(logical_width * uniform_scale));
    const int height = std::max(
        1, static_cast<int>(logical_height * uniform_scale));
    return {width, height,
            static_cast<float>(width) / logical_width,
            static_cast<float>(height) / logical_height};
}

} // namespace krkrvita

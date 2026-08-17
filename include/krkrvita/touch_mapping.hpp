#pragma once

#include <algorithm>
#include <cmath>

namespace krkrvita {

struct TouchPanelBounds {
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
};

struct TouchPoint {
    int x = 0;
    int y = 0;
};

// This is the exact inverse of the aspect-fit transform used by the VitaGL
// presenter. Raw Vita coordinates describe the 960x544 display, while the
// result is in Kirikiri's primary paint-box coordinate space.
inline TouchPoint map_vita_touch_to_layer(
    int raw_x, int raw_y, const TouchPanelBounds& panel,
    int layer_width, int layer_height,
    int display_width = 960, int display_height = 544) {
    if (layer_width <= 0 || layer_height <= 0 ||
        display_width <= 0 || display_height <= 0)
        return {};

    const int panel_width = std::max(1, panel.max_x - panel.min_x);
    const int panel_height = std::max(1, panel.max_y - panel.min_y);
    const float screen_x =
        (raw_x - panel.min_x) * static_cast<float>(display_width) /
        panel_width;
    const float screen_y =
        (raw_y - panel.min_y) * static_cast<float>(display_height) /
        panel_height;
    const float scale =
        std::min(static_cast<float>(display_width) / layer_width,
                 static_cast<float>(display_height) / layer_height);
    const float left = (display_width - layer_width * scale) * 0.5f;
    const float top = (display_height - layer_height * scale) * 0.5f;

    return {
        std::clamp(static_cast<int>(std::floor((screen_x - left) / scale)),
                   0, layer_width - 1),
        std::clamp(static_cast<int>(std::floor((screen_y - top) / scale)),
                   0, layer_height - 1),
    };
}

} // namespace krkrvita

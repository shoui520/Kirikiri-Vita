#pragma once

namespace krkrvita {

// A layer-manager completion can reuse its persistent draw buffer as the
// child's draw target.  Skipping the final Blt is valid only for an exact
// opaque copy back onto the identical rectangle; shifted self-copies and
// alpha/opacity operations still have observable pixel semantics.
template <typename Rect, typename LayerType>
constexpr bool is_noop_drawbuffer_completion(
    const void* draw_buffer, const void* completed_bitmap,
    const Rect& destination, const Rect& source, LayerType layer_type,
    LayerType opaque_layer_type, int opacity) noexcept {
    return draw_buffer && draw_buffer == completed_bitmap &&
           destination.left == source.left &&
           destination.top == source.top &&
           destination.right == source.right &&
           destination.bottom == source.bottom &&
           layer_type == opaque_layer_type && opacity == 255;
}

}  // namespace krkrvita

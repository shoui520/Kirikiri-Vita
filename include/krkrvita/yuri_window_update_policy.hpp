#pragma once

namespace krkrvita {

// Window.update(utNormal) means "flush the regions the layer manager already
// marked dirty".  It must not manufacture a full-window exposure: doing that
// turns every character reveal and animation tick into a complete composition.
// Before the first completed draw buffer exists, a full exposure is still
// required to bootstrap the surface.
constexpr bool yuri_needs_full_window_exposure(bool entire_update,
                                                bool has_draw_buffer) {
    return entire_update || !has_draw_buffer;
}

} // namespace krkrvita

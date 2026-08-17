#pragma once

#include <cstdint>

#include "krkrvita/frame_damage.hpp"

// Presentation boundary used by the wholesale Kirikiri engine. The stable
// default path uploads Yuri's completed software framebuffer to VitaGL. The
// direct texture entry point is reserved for the disabled experimental OpenGL
// compositor. SDL is not involved.
bool krkrvita_vitagl_initialize();
bool krkrvita_vitagl_resize(int width, int height);
bool krkrvita_vitagl_present(const void* pixels, int pitch, int width, int height);
bool krkrvita_vitagl_present_damage(
    const void* pixels, int pitch, int width, int height,
    const krkrvita::FrameDamageRegion& damage);
// Redraws the last successfully uploaded software surface, for example after
// cursor movement, without copying the 1280x960 Kirikiri framebuffer again.
bool krkrvita_vitagl_redraw();
#ifdef KRKRVITA_YURI_OPENGL_COMPOSITOR
bool krkrvita_vitagl_present_texture(unsigned int texture, int width, int height,
                                     int internal_width, int internal_height,
                                     float scale_width, float scale_height);
#endif
void krkrvita_vitagl_set_cursor(int x, int y, bool visible);
std::uint32_t krkrvita_vitagl_presented_frames();
std::uint32_t krkrvita_vitagl_contentful_frames();
std::uint32_t krkrvita_vitagl_uploaded_frames();
std::uint32_t krkrvita_vitagl_full_uploads();
std::uint32_t krkrvita_vitagl_partial_uploads();
std::uint64_t krkrvita_vitagl_uploaded_pixels();

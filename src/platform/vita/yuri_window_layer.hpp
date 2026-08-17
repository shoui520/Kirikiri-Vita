#pragma once

#include <cstdint>

class tTJSNI_Window;

// Entry points consumed by Yuri's WindowImpl.cpp.  The implementation is a
// Vita platform adapter, not a Cocos window or an alternate Kirikiri frontend.
class iWindowLayer;
iWindowLayer* TVPCreateAndAddWindow(tTJSNI_Window* window);
void TVPRemoveWindowLayer(iWindowLayer* layer);
tTJSNI_Window* TVPGetActiveWindow();

// Thin input seam used by the Vita event pump. Coordinates are expressed in
// Yuri's logical primary-layer space, not 960x544 display pixels.
void krkrvita_yuri_pointer_move(std::int32_t x, std::int32_t y);
void krkrvita_yuri_pointer_button(bool down, std::int32_t button,
                                  std::int32_t x, std::int32_t y);
void krkrvita_yuri_pointer_wheel(std::int32_t delta, std::int32_t x,
                                 std::int32_t y);
void krkrvita_yuri_key(bool down, std::uint16_t key, std::uint32_t shift);
bool krkrvita_yuri_active_pointer(std::int32_t& width, std::int32_t& height,
                                  std::int32_t& x, std::int32_t& y);

// Presents only a draw-buffer or cursor generation that has actually changed.
// Yuri's BasicDrawDevice::Show() is the authority for software-surface dirtiness.
bool krkrvita_yuri_present_frame();

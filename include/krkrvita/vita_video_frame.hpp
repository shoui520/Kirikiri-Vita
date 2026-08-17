#pragma once

#include <cstdint>

// Thread-safe handoff from Yuri's movie decoder to the main-thread VitaGL
// presenter. Decoding never calls GL, and the presenter never owns or seeks a
// KiriKiri storage stream.
bool krkrvita_vitagl_submit_video_frame(const void* rgba, int pitch,
                                        int width, int height,
                                        std::uint64_t serial);
void krkrvita_vitagl_set_video_rect(int left, int top, int right, int bottom);
void krkrvita_vitagl_set_video_visible(bool visible);
void krkrvita_vitagl_set_video_alpha(float alpha);
void krkrvita_vitagl_clear_video();
bool krkrvita_vitagl_video_active();

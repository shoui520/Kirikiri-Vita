#pragma once

// The software compositor reports its finalized dirty rectangles to the Vita
// presenter so unchanged pixels do not have to be uploaded again.
extern "C" void krkrvita_yuri_begin_frame_damage(int width, int height);
extern "C" void krkrvita_yuri_add_frame_damage(
    int left, int top, int right, int bottom);

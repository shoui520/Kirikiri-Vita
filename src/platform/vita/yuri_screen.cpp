#include "TVPScreen.h"

namespace {

// Kirikiroid/Yuri deliberately exposes a 2048-wide logical desktop and derives
// its height from the physical aspect ratio.  Preserve that backend behavior
// instead of reporting Vita pixels and changing game window/layout decisions.
constexpr int logical_width = 2048;
constexpr int vita_width = 960;
constexpr int vita_height = 544;
constexpr int logical_height = logical_width * vita_height / vita_width;

} // namespace

int tTVPScreen::GetWidth() { return logical_width; }
int tTVPScreen::GetHeight() { return logical_height; }
int tTVPScreen::GetDesktopLeft() { return 0; }
int tTVPScreen::GetDesktopTop() { return 0; }
int tTVPScreen::GetDesktopWidth() { return logical_width; }
int tTVPScreen::GetDesktopHeight() { return logical_height; }

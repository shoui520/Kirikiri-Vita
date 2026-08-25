#include "yuri_tvpgl_c_exports.h"

// Keep Yuri's generated scalar core and its ARM backend from the same source
// generation.  The NEON routines use these functions for short and unaligned
// fragments.
#include "tvpgl.cpp"

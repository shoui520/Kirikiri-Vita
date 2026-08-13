#pragma once

// VitaSDK ships Onigmo under onigmo.h, while the pinned host dependency uses
// the historical oniguruma.h name. Yuri's TJS2 sources include this shim.
#ifdef KRKRVITA_USE_ONIGURUMA
#include_next <oniguruma.h>
#else
#include <onigmo.h>
#endif

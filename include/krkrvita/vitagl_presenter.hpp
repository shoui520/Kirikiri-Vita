#pragma once

#include <cstdint>

// Presentation boundary used by the wholesale Kirikiri engine. Kirikiri
// composites Layers in software; this uploads the completed 32-bit bitmap and
// presents it with VitaGL. SDL must not create a renderer on Vita.
bool krkrvita_vitagl_initialize();
bool krkrvita_vitagl_resize(int width, int height);
bool krkrvita_vitagl_present(const void* pixels, int pitch, int width, int height);

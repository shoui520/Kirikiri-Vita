#pragma once

namespace krkrvita {

// Yuri's ARM backend is allowed to replace TVPGL function pointers at run
// time.  The destination-additive-alpha routines are not byte-compatible
// with the generated scalar core on ARMv7, so Vita keeps the scalar contract
// for this comparatively uncommon layer mode.
template <typename Function>
inline void select_exact_additive_alpha(Function& selected,
                                        Function scalar) noexcept {
    selected = scalar;
}

} // namespace krkrvita

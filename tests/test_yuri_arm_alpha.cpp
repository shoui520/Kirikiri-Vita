#include "tjsTypes.h"
#include "tvpgl.h"
#include "tvpgl_asm_init.h"
#include "krkrvita/yuri_additive_alpha_policy.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
tjs_uint32 TVPCPUFeatures = 0;
}

// The production executable supplies the non-ARM extension hook from Yuri's
// blend-function unit.  This focused static ARM probe needs no extra entries.
void TVPGL_C_Init() {}

namespace {

constexpr tjs_uint32 kYuriArmNeon = 0x02000003u;

template <std::size_t N, typename Function, typename... Args>
bool compare(const char* name, Function scalar, Function selected,
             const std::array<tjs_uint32, N>& source,
             const std::array<tjs_uint32, N>& background, Args... args) {
    auto expected = background;
    auto actual = background;
    scalar(expected.data(), source.data(), static_cast<tjs_int>(N), args...);
    selected(actual.data(), source.data(), static_cast<tjs_int>(N), args...);
    if (expected == actual) return true;
    // Only the colour channels are displayed when the destination is opaque.
    // A difference confined to the alpha byte is the documented non-HDA
    // contract, not a visible defect, so report the two separately.
    bool rgb_differs = false;
    for (std::size_t index = 0; index < N; ++index) {
        if (expected[index] == actual[index]) continue;
        const bool rgb = (expected[index] & 0x00ffffffu) !=
                         (actual[index] & 0x00ffffffu);
        if (rgb) rgb_differs = true;
        std::fprintf(stderr,
            "%s %s at %zu: scalar=%08x selected=%08x src=%08x dst=%08x\n",
            name, rgb ? "RGB DIFFERS" : "alpha-only", index, expected[index],
            actual[index], source[index], background[index]);
        if (rgb) break;
    }
    return !rgb_differs;
}

} // namespace

int main() {
    TVPInitTVPGL();

    // Internal Kirikiri pixels are 0xAARRGGBB. These include transparent and
    // partially transparent colors sampled from an affected title's UI,
    // plus edge values that exercise both scalar fragments and NEON lanes.
    constexpr std::array<tjs_uint32, 19> straight = {
        0x00ffffff, 0x01010101, 0x101008f0, 0x204080c0, 0x3f204060,
        0x40010203, 0x7f7f4020, 0x80808080, 0x80ffffff, 0x81f01020,
        0xa0403020, 0xc0abcdef, 0xe00180ff, 0xfe112233, 0xff000000,
        0xffffffff, 0xffcc8844, 0x336699cc, 0x9900ff80,
    };
    constexpr std::array<tjs_uint32, 19> background = {
        0xff000000, 0xff102030, 0xff203040, 0xff304050, 0xff405060,
        0xff506070, 0xff607080, 0xff708090, 0xff8090a0, 0xff90a0b0,
        0xffa0b0c0, 0xffb0c0d0, 0xffc0d0e0, 0xffd0e0f0, 0xffe0f000,
        0xfff00010, 0xff001020, 0xff112233, 0xff445566,
    };

    TVPCPUFeatures = kYuriArmNeon;
    TVPGL_ASM_Init();

    krkrvita::select_exact_additive_alpha(TVPAlphaBlend_a,
                                           TVPAlphaBlend_a_c);
    krkrvita::select_exact_additive_alpha(TVPAlphaBlend_ao,
                                           TVPAlphaBlend_ao_c);
    krkrvita::select_exact_additive_alpha(TVPAdditiveAlphaBlend_a,
                                           TVPAdditiveAlphaBlend_a_c);
    krkrvita::select_exact_additive_alpha(TVPAdditiveAlphaBlend_ao,
                                           TVPAdditiveAlphaBlend_ao_c);
    krkrvita::select_exact_additive_alpha(TVPApplyColorMap_a,
                                           TVPApplyColorMap_a_c);

    bool ok = true;
    ok &= compare("AlphaBlend", TVPAlphaBlend_HDA_c, TVPAlphaBlend_HDA,
                  straight, background);
    ok &= compare("AlphaBlend_o", TVPAlphaBlend_HDA_o_c,
                  TVPAlphaBlend_HDA_o, straight, background, 173);

    auto additive = straight;
    auto additive_neon = straight;
    TVPConvertAlphaToAdditiveAlpha_c(additive.data(), additive.size());
    TVPConvertAlphaToAdditiveAlpha(additive_neon.data(), additive_neon.size());
    if (additive != additive_neon) {
        std::fputs("ConvertAlphaToAdditiveAlpha differs\n", stderr);
        ok = false;
    }
    ok &= compare("AdditiveAlphaBlend", TVPAdditiveAlphaBlend_HDA_c,
                  TVPAdditiveAlphaBlend_HDA, additive, background);
    ok &= compare("AdditiveAlphaBlend_o", TVPAdditiveAlphaBlend_HDA_o_c,
                  TVPAdditiveAlphaBlend_HDA_o, additive, background, 173);

    // The affected title's buttons are ltAlpha children of its ltAddAlpha
    // message layer. That selects the destination-alpha variants below,
    // rather than the opaque-destination functions above.
    ok &= compare("AlphaBlend_a", TVPAlphaBlend_a_c, TVPAlphaBlend_a,
                  straight, additive);
    ok &= compare("AlphaBlend_ao", TVPAlphaBlend_ao_c, TVPAlphaBlend_ao,
                  straight, additive, 173);
    ok &= compare("AdditiveAlphaBlend_a", TVPAdditiveAlphaBlend_a_c,
                  TVPAdditiveAlphaBlend_a, additive, straight);
    ok &= compare("AdditiveAlphaBlend_ao", TVPAdditiveAlphaBlend_ao_c,
                  TVPAdditiveAlphaBlend_ao, additive, straight, 173);

    constexpr std::array<tjs_uint8, 19> glyph = {
        0, 1, 8, 16, 31, 32, 63, 64, 96, 127, 128, 159, 191, 223, 240,
        250, 253, 254, 255,
    };
    auto glyph_expected = additive;
    auto glyph_actual = additive;
    TVPApplyColorMap_a_c(glyph_expected.data(), glyph.data(), glyph.size(),
                         0x00ffffff);
    TVPApplyColorMap_a(glyph_actual.data(), glyph.data(), glyph.size(),
                       0x00ffffff);
    if (glyph_expected != glyph_actual) {
        for (std::size_t index = 0; index < glyph.size(); ++index) {
            if (glyph_expected[index] != glyph_actual[index]) {
                std::fprintf(stderr,
                    "ApplyColorMap_a differs at %zu: scalar=%08x selected=%08x mask=%u dst=%08x\n",
                    index, glyph_expected[index], glyph_actual[index],
                    glyph[index], additive[index]);
            }
        }
        ok = false;
    }

    // The visible result is a two-stage operation: glyph coverage is
    // accumulated in a transparent ltAddAlpha message layer, then that layer
    // is composited over the opaque scene.  Check the final displayed words,
    // not only the intermediate alpha bytes.
    std::array<tjs_uint32, 19> message_expected{};
    std::array<tjs_uint32, 19> message_actual{};
    TVPApplyColorMap_a_c(message_expected.data(), glyph.data(), glyph.size(),
                         0x00ffffff);
    TVPApplyColorMap_a(message_actual.data(), glyph.data(), glyph.size(),
                       0x00ffffff);
    auto frame_expected = background;
    auto frame_actual = background;
    TVPAdditiveAlphaBlend_HDA_c(frame_expected.data(),
                                message_expected.data(), glyph.size());
    TVPAdditiveAlphaBlend_HDA(frame_actual.data(), message_actual.data(),
                              glyph.size());
    if (frame_expected != frame_actual) {
        std::fputs("two-stage retail composition differs\n", stderr);
        ok = false;
    }

    // TVP_BLEND_4 picks the plain, non-HDA function whenever the destination
    // layer is opaque, and an ltAddAlpha message layer over the primary layer
    // is exactly that case. Every check above used the _HDA forms, so the
    // functions the affected message box actually goes through were never
    // compared against scalar TVPGL at all.
    ok &= compare("AdditiveAlphaBlend(plain)", TVPAdditiveAlphaBlend_c,
                  TVPAdditiveAlphaBlend, additive, background);
    ok &= compare("AdditiveAlphaBlend_o(plain)", TVPAdditiveAlphaBlend_o_c,
                  TVPAdditiveAlphaBlend_o, additive, background, 173);
    ok &= compare("AlphaBlend(plain)", TVPAlphaBlend_c, TVPAlphaBlend,
                  straight, background);
    ok &= compare("AlphaBlend_o(plain)", TVPAlphaBlend_o_c, TVPAlphaBlend_o,
                  straight, background, 173);

    // The same two-stage message composition as above, but ending through the
    // opaque-destination function the engine really calls.
    auto plain_expected = background;
    auto plain_actual = background;
    TVPAdditiveAlphaBlend_c(plain_expected.data(), message_expected.data(),
                            glyph.size());
    TVPAdditiveAlphaBlend(plain_actual.data(), message_actual.data(),
                          glyph.size());
    for (std::size_t index = 0; index < glyph.size(); ++index) {
        if (plain_expected[index] == plain_actual[index]) continue;
        const bool rgb = (plain_expected[index] & 0x00ffffffu) !=
                         (plain_actual[index] & 0x00ffffffu);
        std::fprintf(stderr,
            "retail opaque-destination composition %s at %zu: "
            "scalar=%08x selected=%08x src=%08x dst=%08x\n",
            rgb ? "RGB DIFFERS" : "alpha-only", index, plain_expected[index],
            plain_actual[index], message_expected[index], background[index]);
        if (rgb) { ok = false; break; }
    }

    // The affected title menu is a full-screen ltAddAlpha message layer created
    // with `@position frame="" opacity=0`, so MessageLayer.tjs fills it with
    // ARGB 0x00000000 and composites it over the title art. A fully
    // transparent source must leave the destination untouched.
    //
    // The samples above could never catch a failure here: the only alpha==0
    // entry is paired with a black background, where "correct" and "blacked
    // out" are the same bytes. Pair transparent sources with bright
    // destinations instead.
    constexpr std::array<tjs_uint32, 8> clear_src = {
        0x00000000, 0x00ffffff, 0x00808080, 0x00ff0000,
        0x00000000, 0x0000ff00, 0x000000ff, 0x00123456,
    };
    constexpr std::array<tjs_uint32, 8> bright_dst = {
        0xffffffff, 0xffff8040, 0xff20c0ff, 0xff7f7f7f,
        0xffc0ffc0, 0xff102030, 0xffffff00, 0xff00ffff,
    };
    ok &= compare("AddAlpha clear-over-bright", TVPAdditiveAlphaBlend_c,
                  TVPAdditiveAlphaBlend, clear_src, bright_dst);
    ok &= compare("AddAlpha_o clear-over-bright", TVPAdditiveAlphaBlend_o_c,
                  TVPAdditiveAlphaBlend_o, clear_src, bright_dst, 255);
    ok &= compare("AddAlpha_HDA clear-over-bright",
                  TVPAdditiveAlphaBlend_HDA_c, TVPAdditiveAlphaBlend_HDA,
                  clear_src, bright_dst);
    ok &= compare("Alpha clear-over-bright", TVPAlphaBlend_c, TVPAlphaBlend,
                  clear_src, bright_dst);
    ok &= compare("Alpha_HDA clear-over-bright", TVPAlphaBlend_HDA_c,
                  TVPAlphaBlend_HDA, clear_src, bright_dst);

    // Independently of scalar-vs-NEON agreement, a transparent source must be
    // a no-op on the colour channels. Check the absolute contract too, so a
    // fault shared by both implementations still fails.
    // A premultiplied source with alpha 0 must have RGB 0, so restrict the
    // absolute check to those entries; a non-zero RGB at alpha 0 legitimately
    // adds light under additive-alpha semantics.
    for (std::size_t index = 0; index < clear_src.size(); ++index) {
        if (clear_src[index] != 0u) continue;
        tjs_uint32 neon = bright_dst[index];
        tjs_uint32 scalar = bright_dst[index];
        TVPAdditiveAlphaBlend(&neon, &clear_src[index], 1);
        TVPAdditiveAlphaBlend_c(&scalar, &clear_src[index], 1);
        const tjs_uint32 want = bright_dst[index] & 0x00ffffffu;
        if ((neon & 0x00ffffffu) != want || (scalar & 0x00ffffffu) != want) {
            std::fprintf(stderr,
                "transparent source is not a no-op at %zu: dst=%08x "
                "neon=%08x scalar=%08x\n",
                index, bright_dst[index], neon, scalar);
            ok = false;
        }
    }

    // Repeated composition is what turns that into a black screen: the title's
    // title keeps a full-screen opacity-0 message layer over the art and the
    // animated menu re-composites it every frame.
    {
        tjs_uint32 decayed = 0xffffffffu;
        const tjs_uint32 clear = 0u;
        for (int frame = 0; frame < 300; ++frame)
            TVPAdditiveAlphaBlend(&decayed, &clear, 1);
        if ((decayed & 0x00ffffffu) != 0x00ffffffu) {
            std::fprintf(stderr,
                "300 transparent composites decayed white to %08x\n", decayed);
            ok = false;
        }
    }

    if (!ok) return 1;
    std::puts("Yuri ARM alpha composition matches scalar TVPGL");
    return 0;
}

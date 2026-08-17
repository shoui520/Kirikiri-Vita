#include "yuri_pvf_font_rasterizer.hpp"

#include "FontImpl.h"
#include "FontSystem.h"
#include "FreeTypeFontRasterizer.h"
#include "LayerBitmapIntf.h"
#include "MsgIntf.h"
#include "krkrvita/pvf_metrics.hpp"
#include "krkrvita/retail_bootstrap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <malloc.h>
#include <memory>

extern FontSystem* TVPFontSystem;

namespace {

void* pvf_allocate(void*, ScePvfU32 size) {
    return memalign(4, (static_cast<std::size_t>(size) + 3u) &
                           ~std::size_t(3));
}

void* pvf_reallocate(void*, void* pointer, ScePvfU32 size) {
    return std::realloc(pointer, (static_cast<std::size_t>(size) + 3u) &
                                     ~std::size_t(3));
}

void pvf_free(void*, void* pointer) { std::free(pointer); }

tjs_int from_fixed_64(ScePvfS32 value, tjs_int raster_scale = 1) {
    return static_cast<tjs_int>(
        std::lround(value / (64.0 * std::max<tjs_int>(1, raster_scale))));
}

using AlignedPixels = std::unique_ptr<tjs_uint8, decltype(&std::free)>;

AlignedPixels allocate_pixels(std::size_t count) {
    return AlignedPixels(
        static_cast<tjs_uint8*>(memalign(16, std::max<std::size_t>(1, count))),
        &std::free);
}

} // namespace

YuriVitaPvfFontRasterizer::YuriVitaPvfFontRasterizer() {
    ScePvfInitRec init{};
    init.maxNumFonts = 2;
    init.allocFunc = pvf_allocate;
    init.reallocFunc = pvf_reallocate;
    init.freeFunc = pvf_free;

    ScePvfError error = 0;
    library_ = scePvfNewLib(&init, &error);
    if (!library_ || error < 0)
        TVPThrowExceptionMessage(TVPFontRasterizeError);

    // At 72 dpi, one PVF point maps to one Kirikiri layout pixel.
    scePvfSetResolution(library_, 72.0f, 72.0f);
    scePvfSetEM(library_, 1.0f);
    scePvfSetAltCharacterCode(library_, 0x25a1);

    // Restore the Vita's default thin Japanese system face. Its native PVF
    // metrics are the baseline that previously fitted this game's text boxes;
    // enumerating a heavier face and supersampling it changed advance rounding
    // enough to clip retail dialogue horizontally.
    japanese_ =
        scePvfOpenDefaultJapaneseFontOnSharedMemory(library_, &error);
    if (japanese_ && error >= 0)
        krkrvita_boot_trace("yuri-pvf-japanese-default-selected");
    if (!japanese_ || error < 0)
        TVPThrowExceptionMessage(TVPFontRasterizeError);

    error = 0;
    latin_ = scePvfOpenDefaultLatinFontOnSharedMemory(library_, &error);
    if (!latin_ || error < 0) latin_ = nullptr;

    extra_fonts_ = new FreeTypeFontRasterizer();
    ConfigureSystemFonts(current_font_);

    // Opening a shared PVF handle does not prove that our buffer origin agrees
    // with libpvf's baseline-coordinate contract. Render one Japanese glyph
    // into an aligned buffer now so missing dialogue cannot silently pass boot.
    constexpr ScePvfU32 test_character = 0x3042; // HIRAGANA LETTER A
    ScePvfCharInfo test_info{};
    ScePvfIrect test_rect{};
    if (!scePvfIsElement(japanese_, test_character) ||
        scePvfGetCharInfo(japanese_, test_character, &test_info) < 0 ||
        scePvfGetCharImageRect(japanese_, test_character, &test_rect) < 0 ||
        test_rect.width <= 0 || test_rect.height <= 0)
        TVPThrowExceptionMessage(TVPFontRasterizeError);
    const std::size_t test_size =
        static_cast<std::size_t>(test_rect.width) * test_rect.height;
    AlignedPixels test_pixels = allocate_pixels(test_size);
    if (!test_pixels) TVPThrowExceptionMessage(TVPFontRasterizeError);
    std::fill_n(test_pixels.get(), test_size, 0);
    ScePvfUserImageBufferRec test_image{};
    test_image.pixelFormat = SCE_PVF_USERIMAGE_DIRECT8;
    test_image.xPos64 = -test_info.glyphMetrics.horizontalBearingX64;
    test_image.yPos64 = test_info.glyphMetrics.horizontalBearingY64;
    test_image.rect.width = test_rect.width;
    test_image.rect.height = test_rect.height;
    test_image.bytesPerLine = test_rect.width;
    test_image.buffer = test_pixels.get();
    if (scePvfGetCharGlyphImage(japanese_, test_character, &test_image) < 0 ||
        std::none_of(test_pixels.get(), test_pixels.get() + test_size,
                     [](tjs_uint8 value) { return value != 0; }))
        TVPThrowExceptionMessage(TVPFontRasterizeError);
    krkrvita_boot_trace("yuri-pvf-japanese-glyph-self-test-passed");
}

YuriVitaPvfFontRasterizer::~YuriVitaPvfFontRasterizer() {
    if (extra_fonts_) extra_fonts_->Release();
    if (latin_) scePvfClose(latin_);
    if (japanese_) scePvfClose(japanese_);
    if (library_) scePvfDoneLib(library_);
}

void YuriVitaPvfFontRasterizer::AddRef() { ++ref_count_; }

void YuriVitaPvfFontRasterizer::Release() {
    last_bitmap_ = nullptr;
    if (--ref_count_ == 0) delete this;
}

void YuriVitaPvfFontRasterizer::ApplyFont(tTVPNativeBaseBitmap* bitmap,
                                           bool force) {
    if (bitmap != last_bitmap_ || force) {
        ApplyFont(bitmap->GetFont());
        last_bitmap_ = bitmap;
    }
}

void YuriVitaPvfFontRasterizer::ConfigureSystemFonts(const tTVPFont& font) {
    height_ =
        std::max<tjs_int>(1, std::min<tjs_int>(256, std::abs(font.Height)));
    const tjs_int raster_height =
        krkrvita::pvf_kirikiri_raster_height(height_);
    raster_scale_ = 1;
    const ScePvfFontId fonts[] = {japanese_, latin_};
    for (ScePvfFontId handle : fonts) {
        if (!handle) continue;
        scePvfSetCharSize(handle,
                          static_cast<float>(raster_height * raster_scale_),
                          static_cast<float>(raster_height * raster_scale_));
        // Preserve the firmware face's native weight for normal dialogue.
        // Explicit Kirikiri bold text still receives a deliberate embolden.
        scePvfSetEmboldenRate(handle,
                             (font.Flags & TVP_TF_BOLD) ? 6.0f : 0.0f);
        scePvfSetSkewValue(handle,
                          (font.Flags & TVP_TF_ITALIC) ? 12.0f : 0.0f,
                          0.0f);
    }
    ScePvfFontInfo info{};
    const tjs_int pvf_ascent = scePvfGetFontInfo(japanese_, &info) >= 0
                                   ? std::max<tjs_int>(
                                         1, from_fixed_64(
                                                info.maxIGlyphMetrics.ascender64,
                                                raster_scale_))
                                   : height_;
    ascent_ = krkrvita::pvf_kirikiri_baseline(height_, pvf_ascent);
}

void YuriVitaPvfFontRasterizer::ApplyFont(const tTVPFont& font) {
    current_font_ = font;
    // Resolve Kirikiri's comma-separated candidate list before deciding which
    // backend owns it. Checking the raw face string would miss a valid private
    // font, while handing a PVF alias to FreeType would make it open an empty
    // storage name. Strip the Windows vertical-font marker for registry lookup;
    // the font angle still carries the requested layout direction.
    ttstr resolved_face =
        TVPFontSystem ? TVPFontSystem->GetBeingFont(font.Face) : font.Face;
    if (resolved_face.GetLen() > 0 && resolved_face.c_str()[0] == TJS_W('@'))
        resolved_face = resolved_face.c_str() + 1;
    const TVPFontNamePathInfo* registered = TVPFindFont(resolved_face);
    use_extra_font_ = registered &&
                      (registered->Getter || !registered->Path.IsEmpty());
    if (use_extra_font_) {
        static bool game_font_selection_reported = false;
        if (!game_font_selection_reported) {
            krkrvita_boot_trace("yuri-freetype-game-font-selected");
            game_font_selection_reported = true;
        }
        tTVPFont resolved_font = font;
        resolved_font.Face = resolved_face;
        extra_fonts_->ApplyFont(resolved_font);
    }
    else {
        ConfigureSystemFonts(font);
        static bool system_font_selection_reported = false;
        if (!system_font_selection_reported) {
            krkrvita_boot_trace("yuri-pvf-system-font-selected");
            system_font_selection_reported = true;
        }
    }
    last_bitmap_ = nullptr;
}

ScePvfFontId YuriVitaPvfFontRasterizer::FontFor(tjs_char character) const {
    if (japanese_ && scePvfIsElement(japanese_, character)) return japanese_;
    if (latin_ && scePvfIsElement(latin_, character)) return latin_;
    return japanese_ ? japanese_ : latin_;
}

void YuriVitaPvfFontRasterizer::GetTextExtent(tjs_char character,
                                               tjs_int& width,
                                               tjs_int& height) {
    if (use_extra_font_) {
        static bool extra_extent_entered = false;
        static bool extra_extent_completed = false;
        if (!extra_extent_entered) {
            krkrvita_boot_trace("yuri-freetype-text-extent-entered");
            extra_extent_entered = true;
        }
        extra_fonts_->GetTextExtent(character, width, height);
        if (!extra_extent_completed) {
            krkrvita_boot_trace("yuri-freetype-text-extent-complete");
            extra_extent_completed = true;
        }
        return;
    }
    static bool pvf_extent_entered = false;
    static bool pvf_extent_completed = false;
    if (!pvf_extent_entered) {
        krkrvita_boot_trace("yuri-pvf-text-extent-entered");
        pvf_extent_entered = true;
    }
    ScePvfCharInfo info{};
    const ScePvfFontId font = FontFor(character);
    if (font && scePvfGetCharInfo(font, character, &info) >= 0)
        width = std::max<tjs_int>(
            1, from_fixed_64(info.glyphMetrics.horizontalAdvance64,
                             raster_scale_));
    else
        width = height_;
    height = height_;
    if (!pvf_extent_completed) {
        krkrvita_boot_trace("yuri-pvf-text-extent-complete");
        pvf_extent_completed = true;
    }
}

tjs_int YuriVitaPvfFontRasterizer::GetAscentHeight() {
    return use_extra_font_ ? extra_fonts_->GetAscentHeight() : ascent_;
}

tTVPCharacterData* YuriVitaPvfFontRasterizer::GetBitmap(
    const tTVPFontAndCharacterData& font_data, tjs_int offset_x,
    tjs_int offset_y) {
    if (use_extra_font_) {
        tTVPCharacterData* data =
            extra_fonts_->GetBitmap(font_data, offset_x, offset_y);
        static bool freetype_glyph_reported = false;
        if (data && !freetype_glyph_reported) {
            krkrvita_boot_trace("yuri-freetype-first-glyph-rendered");
            freetype_glyph_reported = true;
        }
        return data;
    }

    tjs_char character = font_data.Character;
    ScePvfFontId font = FontFor(character);
    ScePvfCharInfo info{};
    ScePvfIrect rect{};
    if (!font || scePvfGetCharInfo(font, character, &info) < 0 ||
        scePvfGetCharImageRect(font, character, &rect) < 0) {
        character = 0x25a1;
        font = FontFor(character);
        if (!font || scePvfGetCharInfo(font, character, &info) < 0 ||
            scePvfGetCharImageRect(font, character, &rect) < 0)
            TVPThrowExceptionMessage(TVPFontRasterizeError);
    }

    const tjs_int source_width = rect.width;
    const tjs_int source_height = rect.height;
    const tjs_int width =
        (source_width + raster_scale_ - 1) / raster_scale_;
    const tjs_int height =
        (source_height + raster_scale_ - 1) / raster_scale_;
    const std::size_t source_pixel_count =
        static_cast<std::size_t>(std::max(1, source_width)) *
        static_cast<std::size_t>(std::max(1, source_height));
    AlignedPixels source_pixels = allocate_pixels(source_pixel_count);
    if (!source_pixels) TVPThrowExceptionMessage(TVPFontRasterizeError);
    std::fill_n(source_pixels.get(), source_pixel_count, 0);
    if (source_width > 0 && source_height > 0) {
        ScePvfUserImageBufferRec image{};
        image.pixelFormat = SCE_PVF_USERIMAGE_DIRECT8;
        image.xPos64 = -info.glyphMetrics.horizontalBearingX64;
        // libpvf takes the baseline position in a downwards-growing image.
        // Placing it at the positive top bearing makes the tight glyph image
        // start at row zero; libpvf applies bitmapTop internally.
        image.yPos64 = info.glyphMetrics.horizontalBearingY64;
        image.rect.width = source_width;
        image.rect.height = source_height;
        image.bytesPerLine = source_width;
        image.buffer = source_pixels.get();
        if (scePvfGetCharGlyphImage(font, character, &image) < 0)
            TVPThrowExceptionMessage(TVPFontRasterizeError);
        if (std::none_of(source_pixels.get(),
                         source_pixels.get() + source_pixel_count,
                         [](tjs_uint8 value) { return value != 0; }))
            TVPThrowExceptionMessage(TVPFontRasterizeError);
        static bool system_glyph_reported = false;
        if (!system_glyph_reported) {
            krkrvita_boot_trace("yuri-pvf-first-system-glyph-rendered");
            system_glyph_reported = true;
        }
    }
    const std::size_t pixel_count =
        static_cast<std::size_t>(std::max(1, width)) *
        static_cast<std::size_t>(std::max(1, height));
    AlignedPixels pixels = allocate_pixels(pixel_count);
    if (!pixels) TVPThrowExceptionMessage(TVPFontRasterizeError);
    std::fill_n(pixels.get(), pixel_count, 0);
    std::copy_n(source_pixels.get(), pixel_count, pixels.get());
    if (!font_data.Antialiased)
        for (std::size_t index = 0; index < pixel_count; ++index)
            pixels.get()[index] = pixels.get()[index] >= 128 ? 255 : 0;

    const tjs_int advance = std::max<tjs_int>(
        1, from_fixed_64(info.glyphMetrics.horizontalAdvance64,
                         raster_scale_));
    tGlyphMetrics metrics{};
    if (font_data.Font.Angle == 0) {
        metrics.CellIncX = advance;
        metrics.CellIncY = 0;
    } else if (font_data.Font.Angle == 2700) {
        metrics.CellIncX = 0;
        metrics.CellIncY = advance;
    } else {
        const double angle =
            font_data.Font.Angle * (3.14159265358979323846 / 1800.0);
        metrics.CellIncX = static_cast<tjs_int>(std::cos(angle) * advance);
        metrics.CellIncY = static_cast<tjs_int>(-std::sin(angle) * advance);
    }

    auto* data = new tTVPCharacterData(
        pixels.get(), std::max(1, width),
        from_fixed_64(info.glyphMetrics.horizontalBearingX64, raster_scale_) +
            offset_x,
        ascent_ - from_fixed_64(info.glyphMetrics.horizontalBearingY64,
                                raster_scale_) +
            offset_y,
        width, height, metrics);
    data->Gray = 256;
    data->Antialiased = font_data.Antialiased;
    data->FullColored = false;
    data->Blured = font_data.Blured;
    data->BlurWidth = font_data.BlurWidth;
    data->BlurLevel = font_data.BlurLevel;
    if (height > 0 && (current_font_.Flags & TVP_TF_UNDERLINE))
        data->AddHorizontalLine(
            std::min<tjs_int>(height - 1, ascent_ + 1), 1, 255);
    if (height > 0 && (current_font_.Flags & TVP_TF_STRIKEOUT))
        data->AddHorizontalLine(
            std::min<tjs_int>(height - 1, ascent_ / 2), 1, 255);
    if (font_data.Blured) data->Blur();
    return data;
}

void YuriVitaPvfFontRasterizer::GetGlyphDrawRect(const ttstr& text,
                                                  tTVPRect& area) {
    if (use_extra_font_) {
        extra_fonts_->GetGlyphDrawRect(text, area);
        return;
    }
    area.clear();
    tjs_int pen = 0;
    bool has_glyph = false;
    for (tjs_uint index = 0; index < text.length(); ++index) {
        ScePvfCharInfo info{};
        ScePvfIrect image{};
        const ScePvfFontId font = FontFor(text[index]);
        if (!font || scePvfGetCharInfo(font, text[index], &info) < 0 ||
            scePvfGetCharImageRect(font, text[index], &image) < 0)
            continue;
        const tjs_int left =
            pen + from_fixed_64(info.glyphMetrics.horizontalBearingX64,
                                raster_scale_);
        const tjs_int top =
            ascent_ - from_fixed_64(info.glyphMetrics.horizontalBearingY64,
                                    raster_scale_);
        const tjs_int image_width =
            (image.width + raster_scale_ - 1) / raster_scale_;
        const tjs_int image_height =
            (image.height + raster_scale_ - 1) / raster_scale_;
        const tTVPRect glyph(left, top, left + image_width,
                             top + image_height);
        if (has_glyph)
            area.do_union(glyph);
        else {
            area = glyph;
            has_glyph = true;
        }
        pen += std::max<tjs_int>(
            1, from_fixed_64(info.glyphMetrics.horizontalAdvance64,
                             raster_scale_));
    }
}

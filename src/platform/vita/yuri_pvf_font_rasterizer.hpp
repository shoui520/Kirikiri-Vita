#pragma once

#include "CharacterData.h"
#include "FontRasterizer.h"

#include <psp2/pvf.h>

class FreeTypeFontRasterizer;

// Yuri FontRasterizer implementation backed by the Vita's shared Japanese and
// Latin system fonts.  Fonts explicitly registered by a game remain handled
// by Yuri's FreeType rasterizer.
class YuriVitaPvfFontRasterizer final : public FontRasterizer {
public:
    YuriVitaPvfFontRasterizer();
    ~YuriVitaPvfFontRasterizer() override;

    void AddRef() override;
    void Release() override;
    void ApplyFont(class tTVPNativeBaseBitmap* bitmap, bool force) override;
    void ApplyFont(const tTVPFont& font) override;
    void GetTextExtent(tjs_char character, tjs_int& width,
                       tjs_int& height) override;
    tjs_int GetAscentHeight() override;
    tTVPCharacterData* GetBitmap(const tTVPFontAndCharacterData& font,
                                 tjs_int offset_x,
                                 tjs_int offset_y) override;
    void GetGlyphDrawRect(const ttstr& text, tTVPRect& area) override;

private:
    ScePvfFontId FontFor(tjs_char character) const;
    void ConfigureSystemFonts(const tTVPFont& font);

    tjs_int ref_count_ = 1;
    ScePvfLibId library_ = nullptr;
    ScePvfFontId japanese_ = nullptr;
    ScePvfFontId latin_ = nullptr;
    tTVPFont current_font_{};
    tjs_int height_ = 12;
    tjs_int ascent_ = 10;
    tjs_int raster_scale_ = 1;
    class tTVPNativeBaseBitmap* last_bitmap_ = nullptr;
    FreeTypeFontRasterizer* extra_fonts_ = nullptr;
    bool use_extra_font_ = false;
};

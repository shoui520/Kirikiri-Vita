#pragma once

#include "CharacterData.h"
#include "FontRasterizer.h"

#include <psp2/pvf.h>

#include <set>

class FreeTypeFontRasterizer;

// Kirikiri-compatible rasterizer backed by the Vita's shared Japanese system
// font. Explicitly added game fonts continue through FreeType.
class VitaPvfFontRasterizer final : public FontRasterizer
{
public:
	VitaPvfFontRasterizer();
	~VitaPvfFontRasterizer() override;

	void AddRef() override;
	void Release() override;
	void ApplyFont(class tTVPNativeBaseBitmap *bitmap, bool force) override;
	void ApplyFont(const tTVPFont &font) override;
	void GetTextExtent(tjs_char character, tjs_int &width, tjs_int &height) override;
	tjs_int GetAscentHeight() override;
	tTVPCharacterData *GetBitmap(const tTVPFontAndCharacterData &font,
		tjs_int offset_x, tjs_int offset_y) override;
	void GetGlyphDrawRect(const ttstr &text, tTVPRect &area) override;
	bool AddFont(const ttstr &storage, std::vector<tjs_string> *faces) override;
	void GetFontList(std::vector<ttstr> &list, tjs_uint32 flags,
		const tTVPFont &font) override;

private:
	ScePvfFontId FontFor(tjs_char character) const;
	void ConfigureSystemFonts(const tTVPFont &font);

	tjs_int RefCount = 1;
	ScePvfLibId Library = nullptr;
	ScePvfFontId Japanese = nullptr;
	ScePvfFontId Latin = nullptr;
	tTVPFont CurrentFont{};
	tjs_int Height = 12;
	tjs_int Ascent = 10;
	class tTVPNativeBaseBitmap *LastBitmap = nullptr;
	FreeTypeFontRasterizer *ExtraFonts = nullptr;
	std::set<tjs_string> ExtraFaceNames;
	bool UseExtraFont = false;
};


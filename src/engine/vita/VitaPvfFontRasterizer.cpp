#include "VitaPvfFontRasterizer.h"

#include "FreeTypeFontRasterizer.h"
#include "LayerBitmapIntf.h"
#include "MsgIntf.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <vector>

namespace {

void *PvfAllocate(void *, ScePvfU32 size)
{
	return memalign(4, (static_cast<std::size_t>(size) + 3u) & ~std::size_t(3));
}

void *PvfReallocate(void *, void *pointer, ScePvfU32 size)
{
	return std::realloc(pointer,
		(static_cast<std::size_t>(size) + 3u) & ~std::size_t(3));
}

void PvfFree(void *, void *pointer)
{
	std::free(pointer);
}

tjs_int FromFixed64(ScePvfS32 value)
{
	return static_cast<tjs_int>(std::lround(value / 64.0));
}

const tjs_char *const SystemFaceNames[] = {
	TJS_W("ScePvf Japanese"), TJS_W("Noto Sans CJK JP"),
	TJS_W("MS Gothic"), TJS_W("MS PGothic"),
	TJS_W("ＭＳ ゴシック"), TJS_W("ＭＳ Ｐゴシック"),
	TJS_W("MS Mincho"), TJS_W("MS PMincho"),
	TJS_W("ＭＳ 明朝"), TJS_W("ＭＳ Ｐ明朝"),
	TJS_W("Meiryo"), TJS_W("メイリオ"),
	TJS_W("Yu Gothic"), TJS_W("游ゴシック")
};

} // namespace

VitaPvfFontRasterizer::VitaPvfFontRasterizer()
{
	ScePvfInitRec init{};
	init.maxNumFonts = 2;
	init.allocFunc = PvfAllocate;
	init.reallocFunc = PvfReallocate;
	init.freeFunc = PvfFree;
	ScePvfError error = 0;
	Library = scePvfNewLib(&init, &error);
	if(!Library || error < 0) TVPThrowExceptionMessage(TVPFontRasterizeError);

	// At 72 dpi a PVF point maps to one Kirikiri layout pixel.
	scePvfSetResolution(Library, 72.0f, 72.0f);
	scePvfSetEM(Library, 1.0f);
	scePvfSetAltCharacterCode(Library, 0x25a1);
	Japanese = scePvfOpenDefaultJapaneseFontOnSharedMemory(Library, &error);
	if(!Japanese || error < 0) TVPThrowExceptionMessage(TVPFontRasterizeError);
	error = 0;
	Latin = scePvfOpenDefaultLatinFontOnSharedMemory(Library, &error);
	if(!Latin || error < 0) Latin = nullptr;

	ExtraFonts = new FreeTypeFontRasterizer();
	ConfigureSystemFonts(CurrentFont);
}

VitaPvfFontRasterizer::~VitaPvfFontRasterizer()
{
	if(ExtraFonts) ExtraFonts->Release();
	if(Latin) scePvfClose(Latin);
	if(Japanese) scePvfClose(Japanese);
	if(Library) scePvfDoneLib(Library);
}

void VitaPvfFontRasterizer::AddRef() { ++RefCount; }

void VitaPvfFontRasterizer::Release()
{
	LastBitmap = nullptr;
	if(--RefCount == 0) delete this;
}

void VitaPvfFontRasterizer::ApplyFont(tTVPNativeBaseBitmap *bitmap, bool force)
{
	if(bitmap != LastBitmap || force)
	{
		ApplyFont(bitmap->GetFont());
		LastBitmap = bitmap;
	}
}

void VitaPvfFontRasterizer::ConfigureSystemFonts(const tTVPFont &font)
{
	Height = std::max<tjs_int>(1, std::min<tjs_int>(256, std::abs(font.Height)));
	for(ScePvfFontId handle : {Japanese, Latin})
	{
		if(!handle) continue;
		scePvfSetCharSize(handle, static_cast<float>(Height), static_cast<float>(Height));
		scePvfSetEmboldenRate(handle, (font.Flags & TVP_TF_BOLD) ? 1.0f : 0.0f);
		scePvfSetSkewValue(handle, (font.Flags & TVP_TF_ITALIC) ? 12.0f : 0.0f, 0.0f);
	}
	ScePvfFontInfo info{};
	if(scePvfGetFontInfo(Japanese, &info) >= 0)
		Ascent = std::max<tjs_int>(1, FromFixed64(info.maxIGlyphMetrics.ascender64));
	else
		Ascent = Height;
}

void VitaPvfFontRasterizer::ApplyFont(const tTVPFont &font)
{
	CurrentFont = font;
	UseExtraFont = false;
	for(const auto &name : ExtraFaceNames)
	{
		if(font.Face == name)
		{
			UseExtraFont = true;
			break;
		}
	}
	if(UseExtraFont)
		ExtraFonts->ApplyFont(font);
	else
		ConfigureSystemFonts(font);
	LastBitmap = nullptr;
}

ScePvfFontId VitaPvfFontRasterizer::FontFor(tjs_char character) const
{
	if(Japanese && scePvfIsElement(Japanese, character)) return Japanese;
	if(Latin && scePvfIsElement(Latin, character)) return Latin;
	return Japanese ? Japanese : Latin;
}

void VitaPvfFontRasterizer::GetTextExtent(tjs_char character,
	tjs_int &width, tjs_int &height)
{
	if(UseExtraFont)
	{
		ExtraFonts->GetTextExtent(character, width, height);
		return;
	}
	ScePvfCharInfo info{};
	if(auto font = FontFor(character); font && scePvfGetCharInfo(font, character, &info) >= 0)
		width = std::max<tjs_int>(1, FromFixed64(info.glyphMetrics.horizontalAdvance64));
	else
		width = Height;
	height = Height;
}

tjs_int VitaPvfFontRasterizer::GetAscentHeight()
{
	return UseExtraFont ? ExtraFonts->GetAscentHeight() : Ascent;
}

tTVPCharacterData *VitaPvfFontRasterizer::GetBitmap(
	const tTVPFontAndCharacterData &font_data, tjs_int offset_x, tjs_int offset_y)
{
	if(UseExtraFont) return ExtraFonts->GetBitmap(font_data, offset_x, offset_y);

	tjs_char character = font_data.Character;
	ScePvfFontId font = FontFor(character);
	ScePvfCharInfo info{};
	ScePvfIrect rect{};
	if(!font || scePvfGetCharInfo(font, character, &info) < 0 ||
		scePvfGetCharImageRect(font, character, &rect) < 0)
	{
		character = 0x25a1;
		font = FontFor(character);
		if(!font || scePvfGetCharInfo(font, character, &info) < 0 ||
			scePvfGetCharImageRect(font, character, &rect) < 0)
			TVPThrowExceptionMessage(TVPFontRasterizeError);
	}

	const tjs_int width = rect.width;
	const tjs_int height = rect.height;
	std::vector<tjs_uint8> pixels(static_cast<std::size_t>(std::max(1, width)) *
		static_cast<std::size_t>(std::max(1, height)), 0);
	if(width > 0 && height > 0)
	{
		ScePvfUserImageBufferRec image{};
		image.pixelFormat = SCE_PVF_USERIMAGE_DIRECT8;
		image.xPos64 = -info.glyphMetrics.horizontalBearingX64;
		image.yPos64 = info.glyphMetrics.horizontalBearingY64;
		image.rect.width = rect.width;
		image.rect.height = rect.height;
		image.bytesPerLine = rect.width;
		image.buffer = pixels.data();
		if(scePvfGetCharGlyphImage(font, character, &image) < 0)
			TVPThrowExceptionMessage(TVPFontRasterizeError);
	}
	if(!font_data.Antialiased)
		for(auto &pixel : pixels) pixel = pixel >= 128 ? 255 : 0;

	tGlyphMetrics metrics{};
	const tjs_int advance = std::max<tjs_int>(1,
		FromFixed64(info.glyphMetrics.horizontalAdvance64));
	if(font_data.Font.Angle == 0)
	{
		metrics.CellIncX = advance;
		metrics.CellIncY = 0;
	}
	else if(font_data.Font.Angle == 2700)
	{
		metrics.CellIncX = 0;
		metrics.CellIncY = advance;
	}
	else
	{
		const double angle = font_data.Font.Angle * (3.14159265358979323846 / 1800.0);
		metrics.CellIncX = static_cast<tjs_int>(std::cos(angle) * advance);
		metrics.CellIncY = static_cast<tjs_int>(-std::sin(angle) * advance);
	}

	auto *data = new tTVPCharacterData(pixels.data(), std::max(1, width),
		FromFixed64(info.glyphMetrics.horizontalBearingX64) + offset_x,
		Ascent - FromFixed64(info.glyphMetrics.horizontalBearingY64) + offset_y,
		width, height, metrics);
	data->Gray = 256;
	data->Antialiased = font_data.Antialiased;
	data->FullColored = false;
	data->Blured = font_data.Blured;
	data->BlurWidth = font_data.BlurWidth;
	data->BlurLevel = font_data.BlurLevel;
	if(height > 0 && (CurrentFont.Flags & TVP_TF_UNDERLINE))
		data->AddHorizontalLine(std::min<tjs_int>(height - 1, Ascent + 1), 1, 255);
	if(height > 0 && (CurrentFont.Flags & TVP_TF_STRIKEOUT))
		data->AddHorizontalLine(std::min<tjs_int>(height - 1, Ascent / 2), 1, 255);
	if(font_data.Blured) data->Blur();
	return data;
}

void VitaPvfFontRasterizer::GetGlyphDrawRect(const ttstr &text, tTVPRect &area)
{
	if(UseExtraFont)
	{
		ExtraFonts->GetGlyphDrawRect(text, area);
		return;
	}
	area.clear();
	tjs_int pen = 0;
	for(tjs_uint i = 0; i < text.length(); ++i)
	{
		ScePvfCharInfo info{};
		ScePvfIrect image{};
		auto font = FontFor(text[i]);
		if(!font || scePvfGetCharInfo(font, text[i], &info) < 0 ||
			scePvfGetCharImageRect(font, text[i], &image) < 0) continue;
		tTVPRect rect(pen + FromFixed64(info.glyphMetrics.horizontalBearingX64),
			Ascent - FromFixed64(info.glyphMetrics.horizontalBearingY64),
			pen + FromFixed64(info.glyphMetrics.horizontalBearingX64) + image.width,
			Ascent - FromFixed64(info.glyphMetrics.horizontalBearingY64) + image.height);
		if(i == 0) area = rect; else area.do_union(rect);
		pen += std::max<tjs_int>(1, FromFixed64(info.glyphMetrics.horizontalAdvance64));
	}
}

bool VitaPvfFontRasterizer::AddFont(const ttstr &storage,
	std::vector<tjs_string> *faces)
{
	std::vector<tjs_string> loaded;
	if(!ExtraFonts->AddFont(storage, &loaded)) return false;
	ExtraFaceNames.insert(loaded.begin(), loaded.end());
	if(faces) faces->insert(faces->end(), loaded.begin(), loaded.end());
	return true;
}

void VitaPvfFontRasterizer::GetFontList(std::vector<ttstr> &list,
	tjs_uint32 flags, const tTVPFont &font)
{
	for(const auto *name : SystemFaceNames) list.emplace_back(name);
	ExtraFonts->GetFontList(list, flags, font);
}

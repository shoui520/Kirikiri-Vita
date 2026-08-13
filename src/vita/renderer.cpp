#include "krkrvita/runtime.hpp"

#include <psp2/pvf.h>
#include <vitaGL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace krkrvita {
namespace {

constexpr int kAtlasWidth = 1024;
constexpr int kAtlasHeight = 1024;
constexpr int kGlyphMargin = 2;
constexpr float kPvfBaseSize = 10.125f;

using BitmapGlyph = std::array<unsigned char, 5>;

BitmapGlyph fallback_glyph(char character) {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    switch (c) {
    case 'A': return {0x7e,0x11,0x11,0x11,0x7e};
    case 'B': return {0x7f,0x49,0x49,0x49,0x36};
    case 'C': return {0x3e,0x41,0x41,0x41,0x22};
    case 'D': return {0x7f,0x41,0x41,0x22,0x1c};
    case 'E': return {0x7f,0x49,0x49,0x49,0x41};
    case 'F': return {0x7f,0x09,0x09,0x09,0x01};
    case 'G': return {0x3e,0x41,0x49,0x49,0x7a};
    case 'H': return {0x7f,0x08,0x08,0x08,0x7f};
    case 'I': return {0x00,0x41,0x7f,0x41,0x00};
    case 'J': return {0x20,0x40,0x41,0x3f,0x01};
    case 'K': return {0x7f,0x08,0x14,0x22,0x41};
    case 'L': return {0x7f,0x40,0x40,0x40,0x40};
    case 'M': return {0x7f,0x02,0x0c,0x02,0x7f};
    case 'N': return {0x7f,0x04,0x08,0x10,0x7f};
    case 'O': return {0x3e,0x41,0x41,0x41,0x3e};
    case 'P': return {0x7f,0x09,0x09,0x09,0x06};
    case 'Q': return {0x3e,0x41,0x51,0x21,0x5e};
    case 'R': return {0x7f,0x09,0x19,0x29,0x46};
    case 'S': return {0x46,0x49,0x49,0x49,0x31};
    case 'T': return {0x01,0x01,0x7f,0x01,0x01};
    case 'U': return {0x3f,0x40,0x40,0x40,0x3f};
    case 'V': return {0x1f,0x20,0x40,0x20,0x1f};
    case 'W': return {0x3f,0x40,0x38,0x40,0x3f};
    case 'X': return {0x63,0x14,0x08,0x14,0x63};
    case 'Y': return {0x07,0x08,0x70,0x08,0x07};
    case 'Z': return {0x61,0x51,0x49,0x45,0x43};
    case '0': return {0x3e,0x51,0x49,0x45,0x3e};
    case '1': return {0x00,0x42,0x7f,0x40,0x00};
    case '2': return {0x42,0x61,0x51,0x49,0x46};
    case '3': return {0x21,0x41,0x45,0x4b,0x31};
    case '4': return {0x18,0x14,0x12,0x7f,0x10};
    case '5': return {0x27,0x45,0x45,0x45,0x39};
    case '6': return {0x3c,0x4a,0x49,0x49,0x30};
    case '7': return {0x01,0x71,0x09,0x05,0x03};
    case '8': return {0x36,0x49,0x49,0x49,0x36};
    case '9': return {0x06,0x49,0x49,0x29,0x1e};
    case '-': return {0x08,0x08,0x08,0x08,0x08};
    case '_': return {0x40,0x40,0x40,0x40,0x40};
    case '.': return {0x00,0x60,0x60,0x00,0x00};
    case ':': return {0x00,0x36,0x36,0x00,0x00};
    case '/': return {0x20,0x10,0x08,0x04,0x02};
    case '=': return {0x14,0x14,0x14,0x14,0x14};
    case '?': return {0x02,0x01,0x51,0x09,0x06};
    case '!': return {0x00,0x00,0x5f,0x00,0x00};
    default: return {0,0,0,0,0};
    }
}

void* pvf_allocate(void*, ScePvfU32 size) {
    const auto aligned = (static_cast<std::size_t>(size) + 3u) & ~std::size_t{3};
    return memalign(4, aligned);
}

void* pvf_reallocate(void*, void* pointer, ScePvfU32 size) {
    const auto aligned = (static_cast<std::size_t>(size) + 3u) & ~std::size_t{3};
    return std::realloc(pointer, aligned);
}

void pvf_free(void*, void* pointer) {
    std::free(pointer);
}

std::pair<char32_t, std::size_t> decode_utf8(std::string_view text, std::size_t offset) {
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80) return {first, 1};

    int count = 0;
    char32_t value = 0;
    if ((first & 0xe0) == 0xc0) {
        count = 2;
        value = first & 0x1f;
    } else if ((first & 0xf0) == 0xe0) {
        count = 3;
        value = first & 0x0f;
    } else if ((first & 0xf8) == 0xf0) {
        count = 4;
        value = first & 0x07;
    } else {
        return {0xfffd, 1};
    }
    if (offset + static_cast<std::size_t>(count) > text.size()) return {0xfffd, 1};
    for (int i = 1; i < count; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset + i]);
        if ((byte & 0xc0) != 0x80) return {0xfffd, 1};
        value = (value << 6) | (byte & 0x3f);
    }
    const bool overlong = (count == 2 && value < 0x80) ||
                          (count == 3 && value < 0x800) ||
                          (count == 4 && value < 0x10000);
    if (overlong || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        return {0xfffd, 1};
    }
    return {value, static_cast<std::size_t>(count)};
}

ScePvfFontId open_system_font(ScePvfLibId library, ScePvfLanguageCode language,
                              ScePvfError* error) {
    ScePvfFontStyleInfo style{};
    style.languageCode = language;
    style.familyCode = SCE_PVF_FAMILY_SANSERIF;
    style.style = SCE_PVF_STYLE_REGULAR;
    const auto index = scePvfFindOptimumFont(library, &style, error);
    if (*error < 0) return nullptr;
    return scePvfOpen(library, index, SCE_PVF_FILEBASEDSTREAM, error);
}

} // namespace

struct VitaRenderer::Impl {
    struct CachedGlyph {
        int atlas_x = 0;
        int atlas_y = 0;
        int width = 0;
        int height = 0;
        float bearing_x = 0;
        float bearing_y = 0;
        float advance = 0;
        ScePvfFontId font = nullptr;
    };

    bool gl_ready = false;
    ScePvfLibId pvf_library = nullptr;
    ScePvfFontId japanese_font = nullptr;
    ScePvfFontId latin_font = nullptr;
    GLuint atlas_texture = 0;
    int atlas_x = kGlyphMargin;
    int atlas_y = kGlyphMargin;
    int atlas_row_height = 0;
    std::unordered_map<std::uint16_t, CachedGlyph> glyphs;

    ~Impl() {
        if (atlas_texture != 0 && gl_ready) glDeleteTextures(1, &atlas_texture);
        if (latin_font) scePvfClose(latin_font);
        if (japanese_font) scePvfClose(japanese_font);
        if (pvf_library) scePvfDoneLib(pvf_library);
    }

    bool initialize_font(std::string* error) {
        ScePvfInitRec init{};
        init.maxNumFonts = 2;
        init.allocFunc = pvf_allocate;
        init.reallocFunc = pvf_reallocate;
        init.freeFunc = pvf_free;

        ScePvfError pvf_error = 0;
        pvf_library = scePvfNewLib(&init, &pvf_error);
        if (!pvf_library || pvf_error < 0) {
            if (error) *error = "SYSTEM JAPANESE FONT INIT FAILED";
            return false;
        }
        if (scePvfSetResolution(pvf_library, 128.0f, 128.0f) < 0 ||
            scePvfSetEM(pvf_library, 72.0f / (kPvfBaseSize * 128.0f)) < 0) {
            if (error) *error = "SYSTEM JAPANESE FONT METRICS FAILED";
            return false;
        }

        japanese_font = scePvfOpenDefaultJapaneseFontOnSharedMemory(pvf_library, &pvf_error);
        if (!japanese_font || pvf_error < 0) {
            japanese_font = open_system_font(pvf_library, SCE_PVF_LANGUAGE_J, &pvf_error);
        }
        if (!japanese_font || pvf_error < 0 ||
            scePvfSetCharSize(japanese_font, kPvfBaseSize, kPvfBaseSize) < 0) {
            if (japanese_font) scePvfClose(japanese_font);
            japanese_font = nullptr;
            if (error) *error = "SYSTEM JAPANESE FONT OPEN FAILED";
            return false;
        }

        pvf_error = 0;
        latin_font = scePvfOpenDefaultLatinFontOnSharedMemory(pvf_library, &pvf_error);
        if (!latin_font || pvf_error < 0) {
            latin_font = open_system_font(pvf_library, SCE_PVF_LANGUAGE_LATIN, &pvf_error);
        }
        if (latin_font && scePvfSetCharSize(latin_font, kPvfBaseSize, kPvfBaseSize) < 0) {
            scePvfClose(latin_font);
            latin_font = nullptr;
        }

        scePvfSetAltCharacterCode(pvf_library, 0x25a1);
        glGenTextures(1, &atlas_texture);
        glBindTexture(GL_TEXTURE_2D, atlas_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        std::vector<std::uint8_t> empty(kAtlasWidth * kAtlasHeight, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, kAtlasWidth, kAtlasHeight, 0,
                     GL_ALPHA, GL_UNSIGNED_BYTE, empty.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        return atlas_texture != 0;
    }

    ScePvfFontId font_for(std::uint16_t character) const {
        if (japanese_font && scePvfIsElement(japanese_font, character)) return japanese_font;
        if (latin_font && scePvfIsElement(latin_font, character)) return latin_font;
        return japanese_font ? japanese_font : latin_font;
    }

    void clear_atlas() {
        glyphs.clear();
        atlas_x = kGlyphMargin;
        atlas_y = kGlyphMargin;
        atlas_row_height = 0;
        std::vector<std::uint8_t> empty(kAtlasWidth * kAtlasHeight, 0);
        glBindTexture(GL_TEXTURE_2D, atlas_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAtlasWidth, kAtlasHeight,
                        GL_ALPHA, GL_UNSIGNED_BYTE, empty.data());
    }

    const CachedGlyph* cache_glyph(std::uint16_t character) {
        if (const auto existing = glyphs.find(character); existing != glyphs.end()) {
            return &existing->second;
        }
        const auto font = font_for(character);
        if (!font) return nullptr;

        ScePvfCharInfo info{};
        ScePvfIrect image_rect{};
        if (scePvfGetCharInfo(font, character, &info) < 0 ||
            scePvfGetCharImageRect(font, character, &image_rect) < 0) {
            return nullptr;
        }
        const int padded_width = static_cast<int>(image_rect.width) + kGlyphMargin * 2;
        const int padded_height = static_cast<int>(image_rect.height) + kGlyphMargin * 2;
        if (padded_width >= kAtlasWidth || padded_height >= kAtlasHeight) return nullptr;

        if (atlas_x + padded_width > kAtlasWidth) {
            atlas_x = kGlyphMargin;
            atlas_y += atlas_row_height;
            atlas_row_height = 0;
        }
        if (atlas_y + padded_height > kAtlasHeight) clear_atlas();

        std::vector<std::uint8_t> pixels(padded_width * padded_height, 0);
        ScePvfUserImageBufferRec image{};
        image.pixelFormat = SCE_PVF_USERIMAGE_DIRECT8;
        image.xPos64 = (kGlyphMargin << 6) - info.glyphMetrics.horizontalBearingX64;
        image.yPos64 = (kGlyphMargin << 6) + info.glyphMetrics.horizontalBearingY64;
        image.rect.width = static_cast<ScePvfU16>(padded_width);
        image.rect.height = static_cast<ScePvfU16>(padded_height);
        image.bytesPerLine = static_cast<ScePvfU16>(padded_width);
        image.buffer = pixels.data();
        if (scePvfGetCharGlyphImage(font, character, &image) < 0) return nullptr;

        glBindTexture(GL_TEXTURE_2D, atlas_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, atlas_x, atlas_y, padded_width, padded_height,
                        GL_ALPHA, GL_UNSIGNED_BYTE, pixels.data());
        CachedGlyph cached;
        cached.atlas_x = atlas_x + kGlyphMargin;
        cached.atlas_y = atlas_y + kGlyphMargin;
        cached.width = image_rect.width;
        cached.height = image_rect.height;
        cached.bearing_x = info.glyphMetrics.horizontalBearingX64 / 64.0f;
        cached.bearing_y = info.glyphMetrics.horizontalBearingY64 / 64.0f;
        cached.advance = info.glyphMetrics.horizontalAdvance64 / 64.0f;
        cached.font = font;
        atlas_x += padded_width;
        atlas_row_height = std::max(atlas_row_height, padded_height);
        return &glyphs.emplace(character, cached).first->second;
    }

    void draw_pvf(float x, float y, float scale, std::string_view value,
                  float red, float green, float blue) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, atlas_texture);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(red, green, blue, 1.0f);

        float pen_x = x;
        float baseline = y + kPvfBaseSize * scale;
        std::uint16_t previous = 0;
        ScePvfFontId previous_font = nullptr;
        for (std::size_t offset = 0; offset < value.size();) {
            const auto [decoded, consumed] = decode_utf8(value, offset);
            offset += consumed;
            if (decoded == '\r') continue;
            if (decoded == '\n') {
                pen_x = x;
                baseline += (kPvfBaseSize + 2.0f) * scale;
                previous = 0;
                previous_font = nullptr;
                continue;
            }
            const auto character = static_cast<std::uint16_t>(
                decoded <= 0xffff ? decoded : 0xfffd);
            const auto* cached = cache_glyph(character);
            if (!cached) continue;

            if (previous && previous_font == cached->font) {
                ScePvfKerningInfo kerning{};
                if (scePvfGetKerningInfo(cached->font, previous, character, &kerning) >= 0) {
                    pen_x += kerning.iKerningInfo.xOffset64 / 64.0f * scale;
                }
            }
            if (cached->width > 0 && cached->height > 0) {
                const float left = pen_x + cached->bearing_x * scale;
                const float top = baseline - cached->bearing_y * scale;
                const float right = left + cached->width * scale;
                const float bottom = top + cached->height * scale;
                const float u0 = static_cast<float>(cached->atlas_x) / kAtlasWidth;
                const float v0 = static_cast<float>(cached->atlas_y) / kAtlasHeight;
                const float u1 = static_cast<float>(cached->atlas_x + cached->width) / kAtlasWidth;
                const float v1 = static_cast<float>(cached->atlas_y + cached->height) / kAtlasHeight;
                glBegin(GL_QUADS);
                glTexCoord2f(u0, v0); glVertex2f(left, top);
                glTexCoord2f(u1, v0); glVertex2f(right, top);
                glTexCoord2f(u1, v1); glVertex2f(right, bottom);
                glTexCoord2f(u0, v1); glVertex2f(left, bottom);
                glEnd();
            }
            pen_x += cached->advance * scale;
            previous = character;
            previous_font = cached->font;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
};

VitaRenderer::VitaRenderer() : impl_(std::make_unique<Impl>()) {}
VitaRenderer::~VitaRenderer() = default;

bool VitaRenderer::initialize(std::string* error) {
    if (!vglInitExtended(0, 960, 544, 24 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE)) {
        if (error) *error = "VitaGL initialization failed; verify libshacccg.suprx";
        return false;
    }
    impl_->gl_ready = true;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 960, 544, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    std::string font_error;
    if (!impl_->initialize_font(&font_error) && error) *error = font_error;
    return true;
}

bool VitaRenderer::system_font_ready() const {
    return impl_->japanese_font && impl_->atlas_texture != 0;
}

void VitaRenderer::begin() {
    glClearColor(0.025f, 0.035f, 0.055f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void VitaRenderer::rectangle(float x, float y, float width, float height,
                             float red, float green, float blue, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(red, green, blue, alpha);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + width, y);
    glVertex2f(x + width, y + height);
    glVertex2f(x, y + height);
    glEnd();
}

void VitaRenderer::text(float x, float y, float scale, std::string_view value,
                        float red, float green, float blue) {
    if (system_font_ready()) {
        impl_->draw_pvf(x, y, scale, value, red, green, blue);
        return;
    }

    glDisable(GL_TEXTURE_2D);
    glColor4f(red, green, blue, 1.0f);
    glBegin(GL_QUADS);
    float cursor = x;
    for (const char c : value) {
        if (c == '\n') {
            y += 9 * scale;
            cursor = x;
            continue;
        }
        const auto bitmap = fallback_glyph(c);
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((bitmap[column] & (1u << row)) == 0) continue;
                const auto left = cursor + column * scale;
                const auto top = y + row * scale;
                glVertex2f(left, top);
                glVertex2f(left + scale, top);
                glVertex2f(left + scale, top + scale);
                glVertex2f(left, top + scale);
            }
        }
        cursor += 6 * scale;
    }
    glEnd();
}

void VitaRenderer::end() {
    vglSwapBuffers(GL_FALSE);
}

} // namespace krkrvita

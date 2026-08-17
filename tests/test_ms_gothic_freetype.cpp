#include "krkrvita/ms_gothic_fast_path.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct CachedFontStream {
    explicit CachedFontStream(const std::vector<std::uint8_t>& source)
        : source(source), cache(std::make_unique<krkrvita::MsGothicPageCache>(
                              source.size(), &read_at, this)) {
        stream.size = static_cast<unsigned long>(source.size());
        stream.descriptor.pointer = this;
        stream.read = &read;
        stream.close = &close;
    }

    static std::size_t read_at(void* context, std::uint64_t offset,
                               void* destination, std::size_t bytes) {
        auto& self = *static_cast<CachedFontStream*>(context);
        if (offset >= self.source.size()) return 0;
        // Exercise the cache's exact short-read join, as sceIoPread is allowed
        // to return fewer bytes than requested.
        const std::size_t count = std::min<std::size_t>(
            {bytes, 997u, self.source.size() -
                              static_cast<std::size_t>(offset)});
        std::copy_n(self.source.data() + offset, count,
                    static_cast<std::uint8_t*>(destination));
        return count;
    }

    static unsigned long read(FT_Stream stream, unsigned long offset,
                              unsigned char* destination,
                              unsigned long count) {
        if (count == 0) return 0;
        auto& self = *static_cast<CachedFontStream*>(
            stream->descriptor.pointer);
        return static_cast<unsigned long>(
            self.cache->read(offset, destination, count));
    }

    static void close(FT_Stream) {}

    const std::vector<std::uint8_t>& source;
    std::unique_ptr<krkrvita::MsGothicPageCache> cache;
    FT_StreamRec stream{};
};

struct GlyphSnapshot {
    FT_Pos measured_hori_advance = 0;
    FT_Pos measured_vert_advance = 0;
    FT_Pos raw_advance_x = 0;
    FT_Pos raw_advance_y = 0;
    FT_Pos width = 0;
    FT_Pos height = 0;
    FT_Pos hori_bearing_x = 0;
    FT_Pos hori_bearing_y = 0;
    int baseline = 0;
    int bitmap_left = 0;
    int bitmap_top = 0;
    unsigned width_pixels = 0;
    unsigned rows = 0;
    int pitch = 0;
    unsigned pixel_mode = 0;
    unsigned num_grays = 0;
    std::vector<std::uint8_t> bitmap;

    friend bool operator==(const GlyphSnapshot& left,
                           const GlyphSnapshot& right) {
        return std::tie(left.measured_hori_advance,
                        left.measured_vert_advance,
                        left.raw_advance_x, left.raw_advance_y, left.width,
                        left.height, left.hori_bearing_x,
                        left.hori_bearing_y, left.baseline, left.bitmap_left,
                        left.bitmap_top, left.width_pixels, left.rows,
                        left.pitch, left.pixel_mode, left.num_grays,
                        left.bitmap) ==
               std::tie(right.measured_hori_advance,
                        right.measured_vert_advance,
                        right.raw_advance_x, right.raw_advance_y, right.width,
                        right.height, right.hori_bearing_x,
                        right.hori_bearing_y, right.baseline,
                        right.bitmap_left, right.bitmap_top,
                        right.width_pixels, right.rows, right.pitch,
                        right.pixel_mode, right.num_grays, right.bitmap);
    }
};

void apply_style(FT_Face face, bool bold, bool italic) {
    if (bold) FT_GlyphSlot_Embolden(face->glyph);
    if (italic) FT_GlyphSlot_Oblique(face->glyph);
}

void load(FT_Face face, FT_UInt index, FT_Int32 flags,
          bool bold, bool italic) {
    require(FT_Load_Glyph(face, index, flags) == 0,
            "FT_Load_Glyph failed");
    apply_style(face, bold, italic);
}

GlyphSnapshot snapshot(FT_Face face, std::uint32_t character, int pixels,
                       FT_Int32 flags, bool bold, bool italic,
                       bool prepared_reuse) {
    require(FT_Set_Pixel_Sizes(face, 0, pixels) == 0,
            "FT_Set_Pixel_Sizes failed");
    const FT_UInt index = FT_Get_Char_Index(face, character);
    require(index != 0, "MS Gothic face zero lacks golden-test glyph");

    load(face, index, flags, bold, italic);
    GlyphSnapshot result;
    result.measured_hori_advance = face->glyph->metrics.horiAdvance;
    result.measured_vert_advance = face->glyph->metrics.vertAdvance;

    // Yuri's old path loads again after GetGlyphSizeFromCharcode. The new path
    // renders the still-unmodified measured slot only when its exact key
    // matches. Both must produce identical raw advance and raster bytes.
    if (!prepared_reuse) load(face, index, flags, bold, italic);
    result.raw_advance_x = face->glyph->advance.x;
    result.raw_advance_y = face->glyph->advance.y;
    result.width = face->glyph->metrics.width;
    result.height = face->glyph->metrics.height;
    result.hori_bearing_x = face->glyph->metrics.horiBearingX;
    result.hori_bearing_y = face->glyph->metrics.horiBearingY;
    result.baseline = static_cast<int>(face->ascender) *
                      face->size->metrics.y_ppem / face->units_per_EM;

    const FT_Render_Mode mode = (flags & FT_LOAD_TARGET_MONO)
        ? FT_RENDER_MODE_MONO : FT_RENDER_MODE_NORMAL;
    require(FT_Render_Glyph(face->glyph, mode) == 0,
            "FT_Render_Glyph failed");
    const FT_Bitmap& bitmap = face->glyph->bitmap;
    result.bitmap_left = face->glyph->bitmap_left;
    result.bitmap_top = face->glyph->bitmap_top;
    result.width_pixels = bitmap.width;
    result.rows = bitmap.rows;
    result.pitch = bitmap.pitch;
    result.pixel_mode = bitmap.pixel_mode;
    result.num_grays = bitmap.num_grays;
    const std::size_t row_bytes = static_cast<std::size_t>(
        bitmap.pitch < 0 ? -bitmap.pitch : bitmap.pitch);
    result.bitmap.reserve(row_bytes * bitmap.rows);
    for (unsigned row = 0; row < bitmap.rows; ++row) {
        const unsigned source_row = bitmap.pitch < 0
            ? bitmap.rows - 1u - row : row;
        const std::uint8_t* begin = bitmap.buffer + source_row * row_bytes;
        result.bitmap.insert(result.bitmap.end(), begin, begin + row_bytes);
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: test_ms_gothic_freetype msgothic.ttc");
        std::ifstream input(argv[1], std::ios::binary);
        require(static_cast<bool>(input), "cannot open msgothic.ttc");
        const std::istreambuf_iterator<char> end;
        const std::vector<std::uint8_t> bytes(
            std::istreambuf_iterator<char>(input), end);
        require(!bytes.empty(), "msgothic.ttc is empty");

        FT_Library library = nullptr;
        require(FT_Init_FreeType(&library) == 0,
                "FT_Init_FreeType failed");
        FT_Face memory_face = nullptr;
        require(FT_New_Memory_Face(library, bytes.data(), bytes.size(), 0,
                                   &memory_face) == 0,
                "cannot open memory MS Gothic face zero");

        CachedFontStream cached_baseline_stream(bytes);
        CachedFontStream cached_prepared_stream(bytes);
        FT_Open_Args baseline_arguments{};
        baseline_arguments.flags = FT_OPEN_STREAM;
        baseline_arguments.stream = &cached_baseline_stream.stream;
        FT_Open_Args prepared_arguments{};
        prepared_arguments.flags = FT_OPEN_STREAM;
        prepared_arguments.stream = &cached_prepared_stream.stream;
        FT_Face cached_baseline_face = nullptr;
        FT_Face cached_prepared_face = nullptr;
        require(FT_Open_Face(library, &baseline_arguments, 0,
                            &cached_baseline_face) == 0 &&
                    FT_Open_Face(library, &prepared_arguments, 0,
                                 &cached_prepared_face) == 0,
                "cannot open cached MS Gothic face zero");
        require(memory_face->face_index == 0 &&
                    cached_baseline_face->face_index == 0 &&
                    cached_prepared_face->face_index == 0,
                "golden test opened a non-MS-Gothic TTC face");

        constexpr std::uint32_t characters[] = {
            0x4ffau,  // 俺
            0x5973u,  // 女
            0x5915u,  // 夕
            0x65e5u,  // 日
            0x3042u,  // あ
            0x3002u,  // 。
            0x0041u,  // A
        };
        constexpr int heights[] = {18, 24, 28, 32};
        constexpr FT_Int32 load_flags[] = {
            FT_LOAD_NO_BITMAP,
            FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT,
            FT_LOAD_TARGET_MONO,
            FT_LOAD_TARGET_MONO | FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT,
        };

        std::size_t cases = 0;
        for (const int height : heights) {
            for (const FT_Int32 flags : load_flags) {
                for (const bool bold : {false, true}) {
                    for (const bool italic : {false, true}) {
                        for (const std::uint32_t character : characters) {
                            const GlyphSnapshot memory_baseline = snapshot(
                                memory_face, character, height, flags, bold,
                                italic, false);
                            const GlyphSnapshot cached_baseline = snapshot(
                                cached_baseline_face, character, height, flags,
                                bold, italic, false);
                            const GlyphSnapshot cached_prepared = snapshot(
                                cached_prepared_face, character, height, flags,
                                bold, italic, true);
                            require(memory_baseline == cached_baseline,
                                    "cached stream changed MS Gothic metrics or raster bytes");
                            require(cached_baseline == cached_prepared,
                                    "prepared slot changed MS Gothic metrics or raster bytes");
                            ++cases;
                        }
                    }
                }
            }
        }

        FT_Done_Face(cached_prepared_face);
        FT_Done_Face(cached_baseline_face);
        FT_Done_Face(memory_face);
        FT_Done_FreeType(library);
        std::cout << "MS Gothic face-zero golden A/B passed: " << cases
                  << " exact metric/raster cases\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MS Gothic golden test failure: " << exception.what()
                  << '\n';
        return 1;
    }
}

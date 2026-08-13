#include "krkrvita/png.hpp"

#include <png.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace krkrvita {
namespace {

template <typename T>
std::optional<T> read_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return std::nullopt;
    using U = std::make_unsigned_t<T>;
    U value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) value |= U(bytes[offset + i]) << (i * 8);
    return static_cast<T>(value);
}

std::optional<RgbaImage> decode_png(const std::vector<std::uint8_t>& bytes,
                                    std::string* error) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, bytes.data(), bytes.size())) {
        if (error) *error = image.message;
        return std::nullopt;
    }
    image.format = PNG_FORMAT_RGBA;
    RgbaImage result{image.width, image.height};
    result.pixels.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, result.pixels.data(), 0, nullptr)) {
        if (error) *error = image.message;
        png_image_free(&image);
        return std::nullopt;
    }
    return result;
}

std::optional<RgbaImage> decode_dib(const EmbeddedIcon& icon, std::string* error) {
    const auto header_size = read_le<std::uint32_t>(icon.bytes, 0);
    const auto width_value = read_le<std::int32_t>(icon.bytes, 4);
    const auto doubled_height = read_le<std::int32_t>(icon.bytes, 8);
    const auto depth = read_le<std::uint16_t>(icon.bytes, 14);
    const auto compression = read_le<std::uint32_t>(icon.bytes, 16);
    if (!header_size || *header_size < 40 || !width_value || !doubled_height || !depth ||
        !compression || *width_value <= 0 || *doubled_height == 0 ||
        (*depth != 24 && *depth != 32) || (*compression != 0 && *compression != 3)) {
        if (error) *error = "unsupported embedded icon DIB";
        return std::nullopt;
    }
    const auto width = static_cast<std::uint32_t>(*width_value);
    const auto height = static_cast<std::uint32_t>(std::abs(*doubled_height) / 2);
    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        if (error) *error = "invalid embedded icon dimensions";
        return std::nullopt;
    }
    std::size_t pixels_at = *header_size;
    if (*compression == 3 && *header_size == 40) pixels_at += 12;
    const auto row_bytes = ((static_cast<std::size_t>(width) * *depth + 31) / 32) * 4;
    if (pixels_at > icon.bytes.size() || row_bytes * height > icon.bytes.size() - pixels_at) {
        if (error) *error = "truncated embedded icon pixels";
        return std::nullopt;
    }

    RgbaImage result{width, height};
    result.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    bool has_alpha = false;
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto source_y = *doubled_height > 0 ? height - 1 - y : y;
        const auto* source = icon.bytes.data() + pixels_at + source_y * row_bytes;
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto source_offset = x * (*depth / 8);
            const auto target_offset = (static_cast<std::size_t>(y) * width + x) * 4;
            result.pixels[target_offset] = source[source_offset + 2];
            result.pixels[target_offset + 1] = source[source_offset + 1];
            result.pixels[target_offset + 2] = source[source_offset];
            result.pixels[target_offset + 3] = *depth == 32 ? source[source_offset + 3] : 255;
            has_alpha |= result.pixels[target_offset + 3] != 0;
        }
    }

    const auto mask_at = pixels_at + row_bytes * height;
    const auto mask_row = ((static_cast<std::size_t>(width) + 31) / 32) * 4;
    if (mask_at <= icon.bytes.size() && mask_row * height <= icon.bytes.size() - mask_at) {
        for (std::uint32_t y = 0; y < height; ++y) {
            const auto source_y = height - 1 - y;
            const auto* mask = icon.bytes.data() + mask_at + source_y * mask_row;
            for (std::uint32_t x = 0; x < width; ++x) {
                const bool transparent = (mask[x / 8] & (0x80 >> (x % 8))) != 0;
                auto& alpha = result.pixels[(static_cast<std::size_t>(y) * width + x) * 4 + 3];
                if (transparent) alpha = 0;
                else if (!has_alpha) alpha = 255;
            }
        }
    } else if (!has_alpha) {
        for (std::size_t i = 3; i < result.pixels.size(); i += 4) result.pixels[i] = 255;
    }
    return result;
}

} // namespace

std::optional<RgbaImage> decode_icon(const EmbeddedIcon& icon, std::string* error) {
    static constexpr std::uint8_t png_signature[] =
        {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (icon.bytes.size() >= sizeof(png_signature) &&
        std::memcmp(icon.bytes.data(), png_signature, sizeof(png_signature)) == 0) {
        return decode_png(icon.bytes, error);
    }
    return decode_dib(icon, error);
}

RgbaImage fit_icon(const RgbaImage& source, std::uint32_t width,
                   std::uint32_t height, std::array<std::uint8_t, 3> background) {
    RgbaImage output{width, height};
    output.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < output.pixels.size(); i += 4) {
        output.pixels[i] = background[0];
        output.pixels[i + 1] = background[1];
        output.pixels[i + 2] = background[2];
        output.pixels[i + 3] = 255;
    }
    if (source.width == 0 || source.height == 0) return output;
    const auto scale = std::min(static_cast<double>(width) / source.width,
                                static_cast<double>(height) / source.height);
    const auto fitted_width = std::max(1u, static_cast<std::uint32_t>(std::lround(source.width * scale)));
    const auto fitted_height = std::max(1u, static_cast<std::uint32_t>(std::lround(source.height * scale)));
    const auto left = (width - fitted_width) / 2;
    const auto top = (height - fitted_height) / 2;
    for (std::uint32_t y = 0; y < fitted_height; ++y) {
        const auto source_y = std::min(source.height - 1,
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) * source.height / fitted_height));
        for (std::uint32_t x = 0; x < fitted_width; ++x) {
            const auto source_x = std::min(source.width - 1,
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * source.width / fitted_width));
            const auto source_at = (static_cast<std::size_t>(source_y) * source.width + source_x) * 4;
            const auto target_at = (static_cast<std::size_t>(top + y) * width + left + x) * 4;
            const auto alpha = source.pixels[source_at + 3];
            for (int c = 0; c < 3; ++c) {
                output.pixels[target_at + c] = static_cast<std::uint8_t>(
                    (source.pixels[source_at + c] * alpha + background[c] * (255 - alpha) + 127) / 255);
            }
        }
    }
    return output;
}

bool write_vita_indexed_png(const std::filesystem::path& path,
                            const RgbaImage& image, std::string* error) {
    if (image.width == 0 || image.height == 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
        if (error) *error = "invalid RGBA image";
        return false;
    }
    std::filesystem::create_directories(path.parent_path());
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) {
        if (error) *error = "cannot create PNG";
        return false;
    }
    auto* png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    auto* info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info) {
        if (png) png_destroy_write_struct(&png, nullptr);
        std::fclose(file);
        if (error) *error = "cannot initialize libpng";
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        if (error) *error = "libpng failed writing indexed image";
        return false;
    }
    png_init_io(png, file);
    png_set_IHDR(png, info, image.width, image.height, 8, PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    std::array<png_color, 256> palette{};
    for (unsigned index = 0; index < palette.size(); ++index) {
        const auto r = (index >> 5) & 7;
        const auto g = (index >> 2) & 7;
        const auto b = index & 3;
        palette[index] = png_color{static_cast<png_byte>((r * 255 + 3) / 7),
                                   static_cast<png_byte>((g * 255 + 3) / 7),
                                   static_cast<png_byte>((b * 255 + 1) / 3)};
    }
    png_set_PLTE(png, info, palette.data(), static_cast<int>(palette.size()));
    png_write_info(png, info);
    std::vector<std::uint8_t> row(image.width);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto at = (static_cast<std::size_t>(y) * image.width + x) * 4;
            row[x] = static_cast<std::uint8_t>((image.pixels[at] & 0xe0) |
                                               ((image.pixels[at + 1] >> 3) & 0x1c) |
                                               (image.pixels[at + 2] >> 6));
        }
        png_write_row(png, row.data());
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
    return true;
}

} // namespace krkrvita


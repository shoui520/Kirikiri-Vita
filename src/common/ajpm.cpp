#include "krkrvita/ajpm.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace krkrvita {
namespace {

constexpr std::size_t kHeaderSize = 168;
constexpr std::size_t kQuantisationTableSize = 64;
constexpr std::size_t kLuminanceQuantisationOffset = 0x28;
constexpr std::size_t kChrominanceQuantisationOffset = 0x68;
constexpr std::size_t kFrameHeaderSize = 16;
constexpr std::size_t kColourPlaneOffset = 1;
// A frame's alpha plane is one byte per pixel. Refuse a declared size that
// could not describe an image before allocating for it.
constexpr std::size_t kMaximumFramePixels = 8192u * 8192u;

// The JPEG standard's example Huffman tables (ITU-T T.81 Annex K). AJPM does
// not store Huffman tables, so the encoder must have used these; a frame that
// decodes cleanly with them, as every shipped frame does, confirms it.
constexpr std::array<std::uint8_t, 16> kDcLuminanceBits = {
    0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr std::array<std::uint8_t, 12> kDcLuminanceValues = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::array<std::uint8_t, 16> kDcChrominanceBits = {
    0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
constexpr std::array<std::uint8_t, 12> kDcChrominanceValues = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::array<std::uint8_t, 16> kAcLuminanceBits = {
    0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
constexpr std::array<std::uint8_t, 162> kAcLuminanceValues = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
    0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
constexpr std::array<std::uint8_t, 16> kAcChrominanceBits = {
    0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
constexpr std::array<std::uint8_t, 162> kAcChrominanceValues = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
    0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

void fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

std::uint16_t read_le16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] |
                                      (std::uint32_t(bytes[at + 1]) << 8));
}

std::uint32_t read_le32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at + 1]) << 8) |
           (std::uint32_t(bytes[at + 2]) << 16) |
           (std::uint32_t(bytes[at + 3]) << 24);
}

void append_marker(std::vector<std::uint8_t>& out, std::uint8_t marker) {
    out.push_back(0xff);
    out.push_back(marker);
}

void append_segment(std::vector<std::uint8_t>& out, std::uint8_t marker,
                    std::span<const std::uint8_t> payload) {
    append_marker(out, marker);
    const auto length = payload.size() + 2;
    out.push_back(static_cast<std::uint8_t>(length >> 8));
    out.push_back(static_cast<std::uint8_t>(length));
    out.insert(out.end(), payload.begin(), payload.end());
}

void append_quantisation(std::vector<std::uint8_t>& out, std::uint8_t id,
                         std::span<const std::uint8_t> table) {
    std::vector<std::uint8_t> payload;
    payload.reserve(1 + table.size());
    payload.push_back(id);
    payload.insert(payload.end(), table.begin(), table.end());
    append_segment(out, 0xdb, payload);
}

void append_huffman(std::vector<std::uint8_t>& out, std::uint8_t id,
                    std::span<const std::uint8_t> bits,
                    std::span<const std::uint8_t> values) {
    std::vector<std::uint8_t> payload;
    payload.reserve(1 + bits.size() + values.size());
    payload.push_back(id);
    payload.insert(payload.end(), bits.begin(), bits.end());
    payload.insert(payload.end(), values.begin(), values.end());
    append_segment(out, 0xc4, payload);
}

// Rebuild an ordinary baseline JPEG file around AJPM's stored entropy data.
std::vector<std::uint8_t> build_jpeg(
    std::span<const std::uint8_t> luminance_quantisation,
    std::span<const std::uint8_t> chrominance_quantisation,
    std::uint16_t width, std::uint16_t height,
    std::span<const std::uint8_t> entropy) {
    std::vector<std::uint8_t> out;
    out.reserve(entropy.size() + entropy.size() / 8 + 1024);
    append_marker(out, 0xd8); // SOI
    append_quantisation(out, 0, luminance_quantisation);
    append_quantisation(out, 1, chrominance_quantisation);

    // SOF0: 8-bit, three components, Y at 2x2 with Cb/Cr at 1x1 -- 4:2:0.
    const std::array<std::uint8_t, 15> frame_header = {
        8,
        static_cast<std::uint8_t>(height >> 8), static_cast<std::uint8_t>(height),
        static_cast<std::uint8_t>(width >> 8), static_cast<std::uint8_t>(width),
        3,
        1, 0x22, 0,
        2, 0x11, 1,
        3, 0x11, 1,
    };
    append_segment(out, 0xc0, frame_header);

    append_huffman(out, 0x00, kDcLuminanceBits, kDcLuminanceValues);
    append_huffman(out, 0x10, kAcLuminanceBits, kAcLuminanceValues);
    append_huffman(out, 0x01, kDcChrominanceBits, kDcChrominanceValues);
    append_huffman(out, 0x11, kAcChrominanceBits, kAcChrominanceValues);

    const std::array<std::uint8_t, 10> scan_header = {
        3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0};
    append_segment(out, 0xda, scan_header);

    // AJPM stores entropy data unstuffed. Restore the 0x00 that must follow a
    // literal 0xFF so the result is a legal JPEG scan.
    for (const auto byte : entropy) {
        out.push_back(byte);
        if (byte == 0xff) out.push_back(0x00);
    }
    append_marker(out, 0xd9); // EOI
    return out;
}

} // namespace

std::optional<AjpmMovie> AjpmMovie::open(std::span<const std::uint8_t> bytes,
                                         std::string* error) {
    if (bytes.size() < kHeaderSize) {
        fail(error, "AJPM file is shorter than its header");
        return std::nullopt;
    }
    if (!std::equal(bytes.begin(), bytes.begin() + 4, "AJPM")) {
        fail(error, "not an AJPM file");
        return std::nullopt;
    }

    AjpmMovie movie;
    movie.bytes_ = bytes;
    const auto declared_size = read_le32(bytes, 0x04);
    const auto data_offset = read_le32(bytes, 0x0c);
    movie.declared_frames_ = read_le32(bytes, 0x14);
    movie.fps_ = read_le32(bytes, 0x1c);

    if (declared_size != bytes.size()) {
        fail(error, "AJPM header declares " + std::to_string(declared_size) +
                        " bytes but the file holds " +
                        std::to_string(bytes.size()));
        return std::nullopt;
    }
    if (data_offset != kHeaderSize) {
        fail(error, "AJPM frame data starts at " + std::to_string(data_offset) +
                        " rather than " + std::to_string(kHeaderSize));
        return std::nullopt;
    }
    if (!movie.fps_) {
        fail(error, "AJPM header declares no frame rate");
        return std::nullopt;
    }

    movie.luminance_quantisation_ =
        bytes.subspan(kLuminanceQuantisationOffset, kQuantisationTableSize);
    movie.chrominance_quantisation_ =
        bytes.subspan(kChrominanceQuantisationOffset, kQuantisationTableSize);

    std::size_t at = kHeaderSize;
    while (at + 8 <= bytes.size()) {
        if (!std::equal(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                        bytes.begin() + static_cast<std::ptrdiff_t>(at) + 4,
                        "FRAM")) {
            fail(error, "unrecognized AJPM chunk at offset " +
                            std::to_string(at));
            return std::nullopt;
        }
        const std::size_t size = read_le32(bytes, at + 4);
        if (size < kFrameHeaderSize || at + 8 + size > bytes.size()) {
            fail(error, "AJPM frame at offset " + std::to_string(at) +
                            " runs past the end of the file");
            return std::nullopt;
        }
        movie.frames_.push_back({at + 8, size});
        at += 8 + size;
    }
    if (at != bytes.size()) {
        fail(error, "AJPM frame chunks leave " +
                        std::to_string(bytes.size() - at) + " trailing bytes");
        return std::nullopt;
    }
    if (movie.frames_.size() != movie.declared_frames_) {
        fail(error, "AJPM header declares " +
                        std::to_string(movie.declared_frames_) +
                        " frames but the file holds " +
                        std::to_string(movie.frames_.size()));
        return std::nullopt;
    }
    return movie;
}

std::optional<AjpmFrame> AjpmMovie::frame(std::size_t index,
                                          std::string* error) const {
    if (index >= frames_.size()) {
        fail(error, "AJPM frame index is out of range");
        return std::nullopt;
    }
    const auto& record = frames_[index];
    const auto body = bytes_.subspan(record.offset, record.size);

    AjpmFrame frame;
    frame.index = read_le32(body, 0x00);
    frame.field_04 = read_le16(body, 0x04);
    frame.field_06 = read_le16(body, 0x06);
    frame.width = read_le16(body, 0x08);
    frame.height = read_le16(body, 0x0a);
    const std::size_t alpha_compressed = read_le32(body, 0x0c);

    if (!frame.width || !frame.height) {
        fail(error, "AJPM frame has no area");
        return std::nullopt;
    }
    const std::size_t pixels =
        static_cast<std::size_t>(frame.width) * frame.height;
    if (pixels > kMaximumFramePixels) {
        fail(error, "AJPM frame is implausibly large");
        return std::nullopt;
    }
    if (alpha_compressed > body.size() - kFrameHeaderSize) {
        fail(error, "AJPM alpha plane runs past the end of its frame");
        return std::nullopt;
    }

    frame.alpha.resize(pixels);
    uLongf produced = static_cast<uLongf>(pixels);
    const auto status =
        ::uncompress(frame.alpha.data(), &produced,
                     body.data() + kFrameHeaderSize,
                     static_cast<uLong>(alpha_compressed));
    if (status != Z_OK || produced != pixels) {
        fail(error, "AJPM alpha plane does not inflate to " +
                        std::to_string(pixels) + " bytes");
        return std::nullopt;
    }

    // One byte separates the alpha stream from the colour plane's entropy
    // data. Its purpose is unknown; what is established is that the entropy
    // data starts after it, because the scan decodes to exactly the frame's
    // 4:2:0 block count from there and not from one byte earlier.
    if (body.size() < kFrameHeaderSize + alpha_compressed + kColourPlaneOffset) {
        fail(error, "AJPM frame carries no colour plane");
        return std::nullopt;
    }
    frame.colour = body.subspan(kFrameHeaderSize + alpha_compressed +
                                kColourPlaneOffset);
    if (frame.colour.empty()) {
        fail(error, "AJPM frame carries no colour plane");
        return std::nullopt;
    }
    return frame;
}

std::vector<std::uint8_t> rebuild_baseline_jpeg(const AjpmMovie& movie,
                                                const AjpmFrame& frame) {
    return build_jpeg(movie.luminance_quantisation(),
                      movie.chrominance_quantisation(), frame.width,
                      frame.height, frame.colour);
}

} // namespace krkrvita

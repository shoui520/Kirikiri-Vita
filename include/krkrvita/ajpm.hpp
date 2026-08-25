#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace krkrvita {

// AJPM ("AlphaMovie") is the container written by the AlphaMovie.dll Kirikiri
// plug-in: a sequence of frames that each carry an 8-bit alpha plane and a
// colour plane, so a game can play video with per-pixel transparency over its
// layers.
//
// The layout is not documented anywhere; it was recovered from the shipped
// files and every claim below is checked by tests/test_ajpm.cpp:
//
//   header, 168 bytes
//     0x00  "AJPM"
//     0x04  u32  total file size
//     0x08  u32  zero
//     0x0c  u32  offset of the first frame (always 168)
//     0x10  u32  per-file identifier; differs between files, unused here
//     0x14  u32  frame count
//     0x18  u32  version (1)
//     0x1c  u32  frames per second
//     0x20  8    unclassified
//     0x28  64   luminance quantisation table, zig-zag order
//     0x68  64   chrominance quantisation table, zig-zag order
//
//   frame chunk
//     0x00  "FRAM"
//     0x04  u32  body size
//     body:
//       0x00  u32  frame index
//       0x04  u16  unclassified
//       0x06  u16  unclassified, decreases as the frame grows
//       0x08  u16  width
//       0x0a  u16  height
//       0x0c  u32  size of the compressed alpha plane
//       then  zlib stream, inflating to exactly width * height alpha bytes
//       then  one byte of unknown purpose
//       then  JPEG entropy-coded data for the colour plane
//
// What is established, and what is not:
//
//   * The container walk and the alpha plane hold for every frame of every
//     shipped movie -- 47 files, 3,638 frames. The tests assert exactly that.
//   * The colour plane is *not* fully understood, and this file deliberately
//     does not pretend otherwise. It is exposed as raw bytes.
//
// What is known about the colour plane, for whoever picks this up next:
//
//   * For the `video/effect/*` movies it is baseline JPEG, YCbCr 4:2:0, using
//     the header's quantisation tables and the standard Annex K Huffman
//     tables -- neither of which is stored per frame. Its entropy data is
//     stored *without* JPEG byte stuffing, so a literal 0xFF is not followed
//     by 0x00. Rebuilding an ordinary JPEG around it (SOI, both DQT segments,
//     an SOF0 declaring 2x2/1x1/1x1 sampling at the frame's own size, the
//     four standard DHT segments, SOS, the entropy data with stuffing
//     restored, EOI) produces a file a baseline decoder reads, and decoding
//     the scan yields exactly the 4:2:0 block count the frame declares.
//     rebuild_baseline_jpeg() performs that reconstruction.
//   * For the `video/tentacle/*` movies the same reconstruction does not
//     decode at any byte offset or sampling factor, although their headers,
//     quantisation tables, version and frame rate are identical. Something
//     about their colour encoding differs and has not been identified.
//
// So: rebuild_baseline_jpeg() is a best-known reconstruction, not a contract.
// Hand its output to a decoder and check the decoder's verdict; do not assume
// a frame is renderable because a JPEG file came back.

struct AjpmFrame {
    std::uint32_t index = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    // The two unclassified per-frame fields, carried through rather than
    // guessed at, so a caller that later works out their meaning has them.
    std::uint16_t field_04 = 0;
    std::uint16_t field_06 = 0;
    // width * height bytes, one per pixel, row-major from the top left.
    std::vector<std::uint8_t> alpha;
    // The colour plane exactly as stored, pointing into the movie's bytes.
    // See the note above before assuming anything about its encoding.
    std::span<const std::uint8_t> colour;
};

class AjpmMovie {
public:
    // `bytes` must stay alive for the lifetime of the movie; frames are read
    // on demand rather than all at once, because a single file can hold tens
    // of megabytes of frames.
    static std::optional<AjpmMovie> open(std::span<const std::uint8_t> bytes,
                                         std::string* error = nullptr);

    std::size_t frame_count() const { return frames_.size(); }
    std::uint32_t frames_per_second() const { return fps_; }
    // The frame count the header declares, which a truncated file will not
    // match. open() rejects that mismatch; this exposes it for reporting.
    std::uint32_t declared_frame_count() const { return declared_frames_; }

    std::optional<AjpmFrame> frame(std::size_t index,
                                   std::string* error = nullptr) const;

    // The two 64-entry tables from the file header, in zig-zag order.
    std::span<const std::uint8_t> luminance_quantisation() const {
        return luminance_quantisation_;
    }
    std::span<const std::uint8_t> chrominance_quantisation() const {
        return chrominance_quantisation_;
    }

private:
    struct FrameRecord {
        std::size_t offset = 0; // start of the frame body
        std::size_t size = 0;   // body size
    };

    std::span<const std::uint8_t> bytes_;
    std::vector<FrameRecord> frames_;
    std::uint32_t fps_ = 0;
    std::uint32_t declared_frames_ = 0;
    std::span<const std::uint8_t> luminance_quantisation_;
    std::span<const std::uint8_t> chrominance_quantisation_;
};

// Wrap a frame's colour plane in an ordinary baseline JPEG file, as described
// above. This is a reconstruction of an encoding that is only partly
// understood: it is right for some of the shipped corpus and wrong for the
// rest, and it cannot tell which. Always let a decoder decide.
std::vector<std::uint8_t> rebuild_baseline_jpeg(const AjpmMovie& movie,
                                                const AjpmFrame& frame);

} // namespace krkrvita

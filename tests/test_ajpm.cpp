// AJPM is the container the AlphaMovie.dll Kirikiri plug-in plays: video with
// a per-pixel alpha plane, used for effects that have to sit over the scene.
// Nothing documents it, so include/krkrvita/ajpm.hpp records a layout read out
// of shipped files and this test is what keeps that reading honest.
//
// Synthetic fixtures pin the structural contract and the fail-closed paths.
// An explicitly configured external corpus can also walk shipped frames, which
// proves that the layout is right rather than merely self-consistent.

#include "krkrvita/ajpm.hpp"
#include "krkrvita/phase1_filter.hpp"
#include "krkrvita/xp3_archive.hpp"
#include "krkrvita/xp3_filter_vm.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace krkrvita;

int failures = 0;

void require(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void write_le16(std::vector<std::uint8_t>& bytes, std::size_t at,
                std::uint16_t value) {
    bytes[at] = static_cast<std::uint8_t>(value);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write_le32(std::vector<std::uint8_t>& bytes, std::size_t at,
                std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::vector<std::uint8_t> deflate_bytes(std::span<const std::uint8_t> input) {
    uLongf bound = ::compressBound(static_cast<uLong>(input.size()));
    std::vector<std::uint8_t> out(bound);
    if (::compress(out.data(), &bound, input.data(),
                   static_cast<uLong>(input.size())) != Z_OK) {
        throw std::runtime_error("test fixture could not deflate");
    }
    out.resize(bound);
    return out;
}

struct FrameSpec {
    std::uint16_t width;
    std::uint16_t height;
    std::vector<std::uint8_t> alpha;
    std::vector<std::uint8_t> entropy;
};

// Build a well-formed AJPM file so the parser is exercised without needing an
// installed game.
std::vector<std::uint8_t> build_ajpm(const std::vector<FrameSpec>& frames,
                                     std::uint32_t fps = 30) {
    std::vector<std::uint8_t> bytes(168, 0);
    bytes[0] = 'A'; bytes[1] = 'J'; bytes[2] = 'P'; bytes[3] = 'M';
    write_le32(bytes, 0x0c, 168);
    write_le32(bytes, 0x14, static_cast<std::uint32_t>(frames.size()));
    write_le32(bytes, 0x18, 1);
    write_le32(bytes, 0x1c, fps);
    // Quantisation tables: any distinguishable non-zero pattern will do, since
    // the parser copies rather than interprets them.
    for (std::size_t i = 0; i < 64; ++i) {
        bytes[0x28 + i] = static_cast<std::uint8_t>(i + 1);
        bytes[0x68 + i] = static_cast<std::uint8_t>(128 + i);
    }

    std::uint32_t index = 0;
    for (const auto& frame : frames) {
        const auto compressed = deflate_bytes(frame.alpha);
        std::vector<std::uint8_t> body(16, 0);
        write_le32(body, 0x00, index++);
        write_le16(body, 0x04, 0);
        write_le16(body, 0x06, 0);
        write_le16(body, 0x08, frame.width);
        write_le16(body, 0x0a, frame.height);
        write_le32(body, 0x0c, static_cast<std::uint32_t>(compressed.size()));
        body.insert(body.end(), compressed.begin(), compressed.end());
        body.push_back(0); // the byte that separates alpha from colour
        body.insert(body.end(), frame.entropy.begin(), frame.entropy.end());

        const auto at = bytes.size();
        bytes.resize(at + 8);
        bytes[at] = 'F'; bytes[at + 1] = 'R'; bytes[at + 2] = 'A';
        bytes[at + 3] = 'M';
        write_le32(bytes, at + 4, static_cast<std::uint32_t>(body.size()));
        bytes.insert(bytes.end(), body.begin(), body.end());
    }
    write_le32(bytes, 0x04, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}

FrameSpec simple_frame(std::uint16_t width, std::uint16_t height,
                       std::vector<std::uint8_t> entropy) {
    FrameSpec frame{width, height, {}, std::move(entropy)};
    frame.alpha.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0; i < frame.alpha.size(); ++i)
        frame.alpha[i] = static_cast<std::uint8_t>(i * 7);
    return frame;
}

std::size_t find_marker(std::span<const std::uint8_t> jpeg,
                        std::uint8_t marker) {
    for (std::size_t at = 0; at + 1 < jpeg.size(); ++at)
        if (jpeg[at] == 0xff && jpeg[at + 1] == marker) return at;
    return jpeg.size();
}

std::size_t count_marker(std::span<const std::uint8_t> jpeg,
                         std::uint8_t marker, std::size_t before) {
    std::size_t total = 0;
    for (std::size_t at = 0; at + 1 < before; ++at)
        if (jpeg[at] == 0xff && jpeg[at + 1] == marker) ++total;
    return total;
}

void test_header_and_frames() {
    const auto bytes = build_ajpm({simple_frame(16, 8, {1, 2, 3}),
                                   simple_frame(32, 16, {4, 5, 6, 7})}, 24);
    std::string error;
    const auto movie = AjpmMovie::open(bytes, &error);
    require(movie.has_value(), "well-formed AJPM opens: " + error);
    if (!movie) return;
    require(movie->frame_count() == 2, "both frames are found");
    require(movie->frames_per_second() == 24, "the declared frame rate is read");

    const auto first = movie->frame(0, &error);
    require(first.has_value(), "the first frame decodes: " + error);
    if (!first) return;
    require(first->index == 0, "the frame carries its own index");
    require(first->width == 16 && first->height == 8, "frame size is read");
    require(first->alpha.size() == 16u * 8, "the alpha plane inflates fully");
    require(first->alpha[3] == 21, "alpha bytes survive the round trip");

    const auto second = movie->frame(1, &error);
    require(second.has_value(), "the second frame decodes: " + error);
    if (second) require(second->index == 1, "frame indices advance");
}

// The colour plane is stored as bare entropy data. What comes back has to be a
// JPEG file a decoder will accept, or the frame is useless.
void test_rebuilt_jpeg_structure() {
    const auto bytes = build_ajpm({simple_frame(320, 240, {0x11, 0x22, 0x33})});
    const auto movie = AjpmMovie::open(bytes);
    require(movie.has_value(), "fixture opens");
    if (!movie) return;
    const auto frame = movie->frame(0);
    require(frame.has_value(), "frame decodes");
    if (!frame) return;

    const auto jpeg = rebuild_baseline_jpeg(*movie, *frame);
    require(jpeg.size() > 4 && jpeg[0] == 0xff && jpeg[1] == 0xd8,
            "the rebuilt colour plane starts with SOI");
    require(jpeg.size() >= 2 && jpeg[jpeg.size() - 2] == 0xff &&
                jpeg[jpeg.size() - 1] == 0xd9,
            "the rebuilt colour plane ends with EOI");

    const auto sos = find_marker(jpeg, 0xda);
    require(sos < jpeg.size(), "a scan header is emitted");
    if (sos >= jpeg.size()) return;
    require(count_marker(jpeg, 0xdb, sos) == 2,
            "both quantisation tables are emitted");
    require(count_marker(jpeg, 0xc4, sos) == 4,
            "all four standard Huffman tables are emitted");

    const auto sof = find_marker(jpeg, 0xc0);
    require(sof < sos, "a baseline frame header precedes the scan");
    if (sof >= sos) return;
    require(jpeg[sof + 4] == 8, "the frame header declares 8-bit samples");
    const auto height =
        static_cast<std::uint16_t>((jpeg[sof + 5] << 8) | jpeg[sof + 6]);
    const auto width =
        static_cast<std::uint16_t>((jpeg[sof + 7] << 8) | jpeg[sof + 8]);
    require(width == 320 && height == 240,
            "the frame header carries the frame's own size");
    require(jpeg[sof + 9] == 3, "three colour components are declared");
    require(jpeg[sof + 11] == 0x22,
            "luminance is sampled 2x2, which is 4:2:0 alongside 1x1 chroma");
    require(jpeg[sof + 14] == 0x11 && jpeg[sof + 17] == 0x11,
            "both chrominance components are sampled 1x1");

    // The quantisation tables must be the file's, not invented ones.
    const auto dqt = find_marker(jpeg, 0xdb);
    require(dqt < sos && jpeg[dqt + 4] == 0 && jpeg[dqt + 5] == 1,
            "the luminance table is copied from the file header");
}

// AJPM omits JPEG's byte stuffing. Putting it back is what makes the scan
// legal, and getting it wrong corrupts every frame containing a 0xFF.
void test_entropy_is_restuffed() {
    const auto bytes = build_ajpm(
        {simple_frame(16, 16, {0x01, 0xff, 0x02, 0xff, 0xff, 0x03})});
    const auto movie = AjpmMovie::open(bytes);
    require(movie.has_value(), "fixture opens");
    if (!movie) return;
    const auto frame = movie->frame(0);
    require(frame.has_value(), "frame decodes");
    if (!frame) return;

    const auto jpeg = rebuild_baseline_jpeg(*movie, *frame);
    const auto sos = find_marker(jpeg, 0xda);
    require(sos < jpeg.size(), "a scan header is emitted");
    if (sos >= jpeg.size()) return;
    const auto scan_start = sos + 2 + 2 + 10; // marker, length, scan header
    const std::vector<std::uint8_t> expected = {
        0x01, 0xff, 0x00, 0x02, 0xff, 0x00, 0xff, 0x00, 0x03, 0xff, 0xd9};
    const std::vector<std::uint8_t> actual(
        jpeg.begin() + static_cast<std::ptrdiff_t>(scan_start), jpeg.end());
    require(actual == expected,
            "every literal 0xFF in the scan is followed by a stuffed 0x00");
}

void test_malformed_files_fail_closed() {
    std::string error;
    const auto good = build_ajpm({simple_frame(16, 8, {1, 2, 3})});

    auto wrong_magic = good;
    wrong_magic[1] = 'X';
    require(!AjpmMovie::open(wrong_magic, &error).has_value(),
            "a file without the AJPM magic is refused");

    auto short_file = good;
    short_file.resize(64);
    require(!AjpmMovie::open(short_file, &error).has_value(),
            "a file shorter than the header is refused");

    auto wrong_size = good;
    write_le32(wrong_size, 0x04, static_cast<std::uint32_t>(good.size() + 1));
    require(!AjpmMovie::open(wrong_size, &error).has_value(),
            "a declared size that disagrees with the file is refused");

    auto wrong_count = good;
    write_le32(wrong_count, 0x14, 5);
    require(!AjpmMovie::open(wrong_count, &error).has_value(),
            "a declared frame count that disagrees with the file is refused");

    auto no_fps = good;
    write_le32(no_fps, 0x1c, 0);
    require(!AjpmMovie::open(no_fps, &error).has_value(),
            "a file with no frame rate is refused");

    auto truncated = good;
    truncated.resize(truncated.size() - 4);
    write_le32(truncated, 0x04, static_cast<std::uint32_t>(truncated.size()));
    require(!AjpmMovie::open(truncated, &error).has_value(),
            "a frame that runs past the end of the file is refused");

    auto bad_chunk = good;
    bad_chunk[168] = 'X';
    require(!AjpmMovie::open(bad_chunk, &error).has_value(),
            "an unrecognized chunk tag is refused");

    // A frame whose alpha plane does not inflate to width * height is a
    // misread layout, not a decodable frame.
    auto wrong_alpha = build_ajpm({simple_frame(16, 8, {1, 2, 3})});
    write_le16(wrong_alpha, 168 + 8 + 0x08, 64); // claim a wider frame
    write_le32(wrong_alpha, 0x04,
               static_cast<std::uint32_t>(wrong_alpha.size()));
    const auto movie = AjpmMovie::open(wrong_alpha, &error);
    require(movie.has_value(), "the container still parses");
    if (movie) {
        require(!movie->frame(0, &error).has_value(),
                "an alpha plane of the wrong size is refused");
    }
}

// Everything above proves the parser is self-consistent; an optional external
// corpus proves the layout matches what AlphaMovie.dll actually wrote.
void test_external_corpus() {
    const char* corpus = std::getenv("KRKRVITA_TEST_AJPM_GAME_DIR");
    if (!corpus || !*corpus) {
        std::cout << "external corpus not configured; structural checks only\n";
        return;
    }
    const std::string game = corpus;
    require(std::filesystem::is_directory(game),
            "external AJPM corpus directory is missing");
    std::string error;
    const auto phase1 = infer_phase1_filter(game, &error);
    require(phase1.has_value(), "the game's extraction filter is recovered: " +
                                    error);
    if (!phase1) return;

    Xp3FilterVm filter;
    require(filter.load(phase1->script, &error),
            "the recovered filter script loads: " + error);

    auto archive = Xp3Archive::open(game + "/data.xp3", &error);
    require(archive.has_value(), "the archive opens: " + error);
    if (!archive) return;

    // Inflating every alpha plane in the set moves 2.3 GB and takes about two
    // minutes, which is too slow for a test that runs on every build. Cover
    // every movie's opening frames by default -- enough to catch a layout
    // error anywhere in the corpus -- and walk all of them on request.
    const bool exhaustive = std::getenv("KRKRVITA_AJPM_EXHAUSTIVE") != nullptr;
    constexpr std::size_t kFramesPerMovie = 6;

    std::size_t movies = 0;
    std::size_t frames = 0;
    std::uint64_t alpha_bytes = 0;
    for (const auto& entry : archive->entries()) {
        const auto& name = entry.name;
        if (name.size() < 4 ||
            name.compare(name.size() - 4, 4, ".amv") != 0) continue;
        const auto bytes = archive->read(entry, 64u * 1024u * 1024u, &filter,
                                         &error);
        require(bytes.has_value(), name + " extracts: " + error);
        if (!bytes) continue;

        const auto movie = AjpmMovie::open(*bytes, &error);
        require(movie.has_value(), name + " parses as AJPM: " + error);
        if (!movie) continue;
        ++movies;
        const auto limit = exhaustive
            ? movie->frame_count()
            : std::min<std::size_t>(movie->frame_count(), kFramesPerMovie);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto frame = movie->frame(index, &error);
            require(frame.has_value(), name + " frame " +
                                           std::to_string(index) + ": " + error);
            if (!frame) break;
            require(frame->index == index, name + " frame indices are ordered");
            require(frame->alpha.size() ==
                        static_cast<std::size_t>(frame->width) * frame->height,
                    name + " alpha plane covers the frame");
            require(!frame->colour.empty(),
                    name + " frame carries a colour plane");
            ++frames;
            alpha_bytes += frame->alpha.size();
        }
    }
    require(movies > 0, "the archive holds AJPM movies");
    std::cout << "installed corpus: " << movies << " movies, " << frames
              << " frames walked, " << alpha_bytes << " alpha bytes"
              << (exhaustive ? " (exhaustive)" : "") << '\n';
}

} // namespace

int main() {
    try {
        test_header_and_frames();
        test_rebuilt_jpeg_structure();
        test_entropy_is_restuffed();
        test_malformed_files_fail_closed();
        test_external_corpus();
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: unexpected exception: " << exception.what() << '\n';
        ++failures;
    }
    if (failures) {
        std::cerr << failures << " AJPM check(s) failed\n";
        return 1;
    }
    std::cout << "AJPM checks passed\n";
    return 0;
}

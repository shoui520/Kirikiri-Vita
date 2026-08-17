#include "krkrvita/bubble.hpp"
#include "krkrvita/audio_playback_state.hpp"
#include "krkrvita/deferred_recycle.hpp"
#include "krkrvita/engine_tick_pacer.hpp"
#include "krkrvita/filter_heuristic.hpp"
#include "krkrvita/frame_damage.hpp"
#include "krkrvita/frame_update_gate.hpp"
#include "krkrvita/frame_probe.hpp"
#include "krkrvita/game.hpp"
#include "krkrvita/key_repeat.hpp"
#include "krkrvita/kag_system_variables.hpp"
#include "krkrvita/layer_draw_completion.hpp"
#include "krkrvita/ms_gothic_fast_path.hpp"
#include "krkrvita/patch_manifest.hpp"
#include "krkrvita/patch_repository.hpp"
#include "krkrvita/pvf_metrics.hpp"
#include "krkrvita/presentation_reference.hpp"
#include "krkrvita/presentation_surface_transaction.hpp"
#include "krkrvita/render_task_policy.hpp"
#include "krkrvita/render_task_pool.hpp"
#include "krkrvita/read_all.hpp"
#include "krkrvita/sfo.hpp"
#include "krkrvita/sha256.hpp"
#include "krkrvita/storage.hpp"
#include "krkrvita/system_app_id_compat.hpp"
#include "krkrvita/text_codec.hpp"
#include "krkrvita/text_prefix_width.hpp"
#include "krkrvita/touch_mapping.hpp"
#include "krkrvita/vita_bitmap_allocator.hpp"
#include "krkrvita/vita_memory_budget.hpp"
#include "krkrvita/vita_render_surface.hpp"
#include "krkrvita/vita_storage_path.hpp"
#include "krkrvita/virtual_cd.hpp"
#include "krkrvita/vita_pthread_priority.hpp"
#include "krkrvita/vita_thread_policy.hpp"
#include "krkrvita/write_all.hpp"
#include "krkrvita/yuri_window_update_policy.hpp"
#include "krkrvita/xp3_archive.hpp"
#include "krkrvita/xp3_filter_vm.hpp"
#include "tjs.h"
#include "tjsError.h"
#include "tjsObject.h"

#include <png.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <thread>

namespace {

using namespace krkrvita;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

void test_layer_draw_completion_guard() {
    struct Rect {
        int left;
        int top;
        int right;
        int bottom;
    };
    enum class LayerType { opaque, alpha };

    int draw_buffer = 0;
    int other_bitmap = 0;
    constexpr Rect exact{20, 30, 220, 130};
    check(is_noop_drawbuffer_completion(
              &draw_buffer, &draw_buffer, exact, exact, LayerType::opaque,
              LayerType::opaque, 255),
          "exact opaque DrawBuffer completion was not recognized");

    check(!is_noop_drawbuffer_completion(
              &draw_buffer, &other_bitmap, exact, exact, LayerType::opaque,
              LayerType::opaque, 255),
          "a distinct completed bitmap bypassed the final Blt");
    check(!is_noop_drawbuffer_completion(
              &draw_buffer, &draw_buffer, exact, Rect{21, 30, 221, 130},
              LayerType::opaque, LayerType::opaque, 255),
          "a shifted self-copy bypassed the final Blt");
    check(!is_noop_drawbuffer_completion(
              &draw_buffer, &draw_buffer, exact, Rect{20, 30, 219, 130},
              LayerType::opaque, LayerType::opaque, 255),
          "a differently sized self-copy bypassed the final Blt");
    check(!is_noop_drawbuffer_completion(
              &draw_buffer, &draw_buffer, exact, exact, LayerType::alpha,
              LayerType::opaque, 255),
          "an alpha self-blend bypassed the final Blt");
    check(!is_noop_drawbuffer_completion(
              &draw_buffer, &draw_buffer, exact, exact, LayerType::opaque,
              LayerType::opaque, 254),
          "a partial-opacity self-blend bypassed the final Blt");
    check(!is_noop_drawbuffer_completion(
              nullptr, nullptr, exact, exact, LayerType::opaque,
              LayerType::opaque, 255),
          "null draw buffers were classified as completed surfaces");
}

void test_hybrid_render_task_policy() {
    constexpr int full_surface_pixels = 1280 * 960;
    check(hybrid_render_task_pixel_threshold == 262144,
          "hybrid render-task absolute threshold changed");
    check(select_adaptive_render_task_count(
              hybrid_render_task_pixel_threshold - 1, 1.0f, 960, 3) == 1,
          "small dirty work escaped the serial path");
    check(select_adaptive_render_task_count(
              hybrid_render_task_pixel_threshold, 600.0f, 960, 3) == 1,
          "hybrid task policy discarded Yuri's operation-factor gate");
    check(select_adaptive_render_task_count(
              hybrid_render_task_pixel_threshold, 52.0f, 960, 3) == 3,
          "large qualifying work did not use all three Vita cores");
    check(select_adaptive_render_task_count(
              full_surface_pixels, 150.0f, 960, 3) == 3,
          "a full-surface primitive did not enter the three-way pool");
    check(select_adaptive_render_task_count(
              full_surface_pixels, 52.0f, 960, 1) == 1,
          "explicit serial draw-thread policy was not preserved");
    check(select_adaptive_render_task_count(
              full_surface_pixels, 52.0f, 960, 2) == 2,
          "explicit numeric draw-thread policy was not preserved");
    check(select_adaptive_render_task_count(
              full_surface_pixels, 52.0f, 2, 3) == 2,
          "adaptive task count was not clamped to rectangle height");
    check(select_adaptive_render_task_count(
              full_surface_pixels, 52.0f, 1, 3) == 1,
          "a one-row primitive created empty worker tasks");
}

void test_sha256() {
    Sha256 hash;
    hash.update("abc");
    check(Sha256::hex(hash.finish()) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 vector failed");
}

void test_write_all_bytes() {
    std::vector<std::uint32_t> offsets;
    const std::uint32_t complete = write_all_bytes(
        10, [&](std::uint32_t offset, std::uint32_t remaining) {
            offsets.push_back(offset);
            return static_cast<std::int32_t>(std::min(remaining, 3u));
        });
    check(complete == 10 &&
              offsets == std::vector<std::uint32_t>({0, 3, 6, 9}),
          "partial writes were not joined into one complete request");

    int zero_size_calls = 0;
    check(write_all_bytes(0, [&](std::uint32_t, std::uint32_t) {
              ++zero_size_calls;
              return 1;
          }) == 0 && zero_size_calls == 0,
          "zero-byte write called the backend");

    int zero_after_prefix_calls = 0;
    check(write_all_bytes(8, [&](std::uint32_t, std::uint32_t) {
              return ++zero_after_prefix_calls == 1 ? 3 : 0;
          }) == 3,
          "zero write did not preserve the completed prefix");
    check(write_all_bytes(8, [](std::uint32_t, std::uint32_t) {
              return -17;
          }) == 0,
          "negative write result was accepted");
    check(write_all_bytes(8, [](std::uint32_t, std::uint32_t remaining) {
              return static_cast<std::uint64_t>(remaining) + 1u;
          }) == 0,
          "oversized write result was accepted");
}

void test_read_all_bytes() {
    std::vector<std::uint32_t> offsets;
    const std::uint32_t complete = read_all_bytes(
        10, [&](std::uint32_t offset, std::uint32_t remaining) {
            offsets.push_back(offset);
            return static_cast<std::int32_t>(std::min(remaining, 3u));
        });
    check(complete == 10 &&
              offsets == std::vector<std::uint32_t>({0, 3, 6, 9}),
          "partial reads were not joined into one complete request");

    std::vector<std::uint32_t> retail_offsets;
    int retail_calls = 0;
    check(read_all_bytes(
              90154, [&](std::uint32_t offset, std::uint32_t remaining) {
                  retail_offsets.push_back(offset);
                  ++retail_calls;
                  return static_cast<std::int32_t>(
                      retail_calls == 1 ? 4608u : remaining);
              }) == 90154 &&
              retail_offsets == std::vector<std::uint32_t>({0, 4608}),
          "a Vita-sized UTF-16 system-variable short read stayed truncated");

    int zero_size_calls = 0;
    check(read_all_bytes(0, [&](std::uint32_t, std::uint32_t) {
              ++zero_size_calls;
              return 1;
          }) == 0 && zero_size_calls == 0,
          "zero-byte read called the backend");

    int eof_after_prefix_calls = 0;
    check(read_all_bytes(8, [&](std::uint32_t, std::uint32_t) {
              return ++eof_after_prefix_calls == 1 ? 3 : 0;
          }) == 3,
          "EOF did not preserve the completed read prefix");

    int error_after_prefix_calls = 0;
    check(read_all_bytes(8, [&](std::uint32_t, std::uint32_t) {
              return ++error_after_prefix_calls == 1 ? 3 : -17;
          }) == 3,
          "a read error discarded the completed prefix");
    check(read_all_bytes(8, [](std::uint32_t, std::uint32_t) {
              return -17;
          }) == 0,
          "an initial read error was accepted");
    check(read_all_bytes(8, [](std::uint32_t, std::uint32_t remaining) {
              return static_cast<std::uint64_t>(remaining) + 1u;
          }) == 0,
          "an oversized read result was accepted");
}

void test_kag_system_variable_recovery_primitives() {
    constexpr std::u16string_view data_path =
        u"file://./ux0:data/krkrvita/games/title/savedata/";
    check(is_kag_system_variable_storage(
              u"file://./ux0:data/krkrvita/games/title/savedata/datasc.ksd",
              data_path),
          "KAG compact system-variable file was not recognized");
    check(is_kag_system_variable_storage(
              u"file://./ux0:data/krkrvita/games/title/savedata/DATASU.KSD",
              data_path),
          "KAG user system-variable suffix was not case folded");
    check(!is_kag_system_variable_storage(
              u"file://./ux0:data/krkrvita/games/title/savedata/data0.ksd",
              data_path),
          "ordinary bookmark data was classified as reconstructable state");
    check(!is_kag_system_variable_storage(
              u"file://./ux0:data/krkrvita/games/other/savedata/datasu.ksd",
              data_path),
          "another game's system-variable file crossed the data-path boundary");
    check(!is_kag_system_variable_storage(
              u"file://./ux0:data/krkrvita/games/title/data/datasu.ksd",
              data_path),
          "an archive/script path was classified as persisted system state");

    check(fnv1a_utf16(u"") == UINT64_C(14695981039346656037),
          "empty UTF-16 diagnostic fingerprint changed");
    check(fnv1a_utf16(u"(const) %[]") ==
              fnv1a_utf16(u"(const) %[]"),
          "UTF-16 diagnostic fingerprint is not deterministic");
    check(fnv1a_utf16(u"(const) %[]") !=
              fnv1a_utf16(u"(const) %[,]"),
          "UTF-16 diagnostic fingerprint ignored changed syntax");

    std::set<std::string> existing = {
        "ux0:data/game/datasu.ksd.krkrvita-corrupt",
        "ux0:data/game/datasu.ksd.krkrvita-corrupt.1",
    };
    std::vector<std::pair<std::string, std::string>> renames;
    const std::string backup = quarantine_corrupt_system_variable(
        "ux0:data/game/datasu.ksd",
        [&](std::string_view path) {
            return existing.contains(std::string(path));
        },
        [&](std::string_view from, std::string_view to) {
            renames.emplace_back(from, to);
            return true;
        });
    check(backup == "ux0:data/game/datasu.ksd.krkrvita-corrupt.2" &&
              renames == std::vector<std::pair<std::string, std::string>>({
                  {"ux0:data/game/datasu.ksd",
                   "ux0:data/game/datasu.ksd.krkrvita-corrupt.2"}}),
          "corrupt KAG state did not select a unique non-overwriting backup");

    int failed_renames = 0;
    check(quarantine_corrupt_system_variable(
              "ux0:data/game/datasc.ksd",
              [](std::string_view) { return false; },
              [&](std::string_view, std::string_view) {
                  ++failed_renames;
                  return false;
              }).empty() &&
              failed_renames == 1,
          "failed quarantine was retried under another name or accepted");

    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        engine(new TJS::tTJS(), release_tjs);
    bool caught_script_error = false;
    try {
        engine->EvalExpression(TJS_W("%[ broken:"), nullptr);
    } catch (const TJS::eTJSScriptError&) {
        caught_script_error = true;
    }
    check(caught_script_error,
          "malformed persisted state bypassed the recovery exception type");
    TJS::tTJSVariant fresh_state;
    engine->EvalExpression(TJS_W("%[]"), &fresh_state);
    check(fresh_state.Type() == TJS::tvtObject,
          "KAG recovery expression did not produce a dictionary object");
}

std::vector<std::uint8_t> deflate_with_output_window(
    const std::vector<std::uint8_t>& input,
    std::size_t output_window_size) {
    check(output_window_size > 0 &&
              output_window_size <= std::numeric_limits<uInt>::max(),
          "invalid zlib output-window test size");

    z_stream stream{};
    check(deflateInit(&stream, Z_DEFAULT_COMPRESSION) == Z_OK,
          "cannot initialize zlib output-window test");

    std::vector<std::uint8_t> compressed;
    std::vector<std::uint8_t> output_window(output_window_size);
    stream.next_out = output_window.data();
    stream.avail_out = static_cast<uInt>(output_window.size());

    const auto drain_full_window = [&] {
        compressed.insert(compressed.end(), output_window.begin(),
                          output_window.end());
        stream.next_out = output_window.data();
        stream.avail_out = static_cast<uInt>(output_window.size());
    };

    // Match TextStream's succession of WriteRawData calls instead of handing
    // zlib one monolithic input. Only avail_out differs between both runs.
    constexpr std::array<std::size_t, 7> input_chunks = {
        1, 37, 4093, 65519, 257, 131071, 8191};
    std::size_t input_offset = 0;
    std::size_t chunk_index = 0;
    while (input_offset < input.size()) {
        const std::size_t chunk = std::min(
            input_chunks[chunk_index++ % input_chunks.size()],
            input.size() - input_offset);
        stream.next_in = const_cast<Bytef*>(input.data() + input_offset);
        stream.avail_in = static_cast<uInt>(chunk);
        while (stream.avail_in > 0) {
            check(deflate(&stream, Z_NO_FLUSH) == Z_OK,
                  "zlib rejected streamed test input");
            if (stream.avail_out == 0) drain_full_window();
        }
        input_offset += chunk;
    }

    int result = Z_OK;
    do {
        result = deflate(&stream, Z_FINISH);
        check(result == Z_OK || result == Z_STREAM_END,
              "zlib could not finish output-window test");
        if (stream.avail_out == 0) drain_full_window();
    } while (result != Z_STREAM_END);

    compressed.insert(compressed.end(), output_window.begin(),
                      output_window.begin() +
                          static_cast<std::ptrdiff_t>(output_window.size() -
                                                      stream.avail_out));
    check(deflateEnd(&stream) == Z_OK,
          "zlib could not release output-window test state");
    return compressed;
}

void test_zlib_output_window_equivalence() {
    std::vector<std::uint8_t> input(3u * 1024u * 1024u + 173u);
    std::uint32_t state = 0x6d2b79f5u;
    for (std::size_t index = 0; index < input.size(); ++index) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        input[index] = index % 113 < 19
            ? static_cast<std::uint8_t>(index % 11)
            : static_cast<std::uint8_t>(state);
    }

    const auto yuri_window = deflate_with_output_window(input, 1024u * 1024u);
    const auto vita_window = deflate_with_output_window(input, 64u * 1024u);
    check(yuri_window == vita_window,
          "64 KiB zlib window changed compressed save bytes");

    std::vector<std::uint8_t> restored(input.size());
    uLongf restored_size = static_cast<uLongf>(restored.size());
    check(uncompress(restored.data(), &restored_size, vita_window.data(),
                     static_cast<uLong>(vita_window.size())) == Z_OK &&
              restored_size == input.size() && restored == input,
          "64 KiB zlib window did not round-trip save bytes");
}

struct FontCacheBacking {
    std::vector<std::uint8_t> bytes;
    std::size_t calls = 0;
    std::size_t max_chunk = std::numeric_limits<std::size_t>::max();

    static std::size_t read_at(void* context,
                               std::uint64_t offset,
                               void* destination,
                               std::size_t requested) {
        auto& backing = *static_cast<FontCacheBacking*>(context);
        ++backing.calls;
        if (offset >= backing.bytes.size() || requested == 0) return 0;
        const std::size_t available = backing.bytes.size() -
            static_cast<std::size_t>(offset);
        const std::size_t count = std::min(
            {requested, available, backing.max_chunk});
        std::memcpy(destination, backing.bytes.data() + offset, count);
        return count;
    }
};

void test_ms_gothic_fast_path_primitives() {
    check(ms_gothic_cache_payload_bytes == 512u * 1024u,
          "MS Gothic page-cache payload budget changed");

    std::uint64_t seek = 123;
    check(ms_gothic_resolve_seek(20, 100, 15, TJS_BS_SEEK_CUR, seek) &&
              seek == 35,
          "MS Gothic logical relative seek is wrong");
    check(ms_gothic_resolve_seek(20, 100, -15, TJS_BS_SEEK_END, seek) &&
              seek == 85,
          "MS Gothic logical end-relative seek is wrong");
    const std::uint64_t unchanged = seek;
    check(!ms_gothic_resolve_seek(3, 100,
                                  std::numeric_limits<std::int64_t>::min(),
                                  TJS_BS_SEEK_CUR, seek) && seek == unchanged,
          "invalid MS Gothic seek changed the logical position");
    check(!ms_gothic_resolve_seek(
              std::numeric_limits<std::uint64_t>::max() - 2u, 100, 4,
              TJS_BS_SEEK_CUR, seek),
          "overflowing MS Gothic seek was accepted");

    FontCacheBacking backing;
    backing.bytes.resize(ms_gothic_page_size * (ms_gothic_page_count + 3u) +
                         137u);
    for (std::size_t index = 0; index < backing.bytes.size(); ++index)
        backing.bytes[index] = static_cast<std::uint8_t>(
            ((index * 131u) ^ (index >> 7u) ^ 0x5au) & 0xffu);
    // Force the cache's exact-read loop to handle short sceIoPread-like reads.
    backing.max_chunk = 997;
    auto cache = std::make_unique<MsGothicPageCache>(
        backing.bytes.size(), &FontCacheBacking::read_at, &backing);

    std::array<std::uint8_t, 160> crossing{};
    const std::uint64_t crossing_offset = ms_gothic_page_size - 73u;
    check(cache->read(crossing_offset, crossing.data(), crossing.size()) ==
              crossing.size() &&
              std::equal(crossing.begin(), crossing.end(),
                         backing.bytes.begin() + crossing_offset),
          "cached cross-page MS Gothic read changed bytes");
    const std::size_t cached_calls = backing.calls;
    std::array<std::uint8_t, 160> crossing_again{};
    check(cache->read(crossing_offset, crossing_again.data(),
                      crossing_again.size()) == crossing_again.size() &&
              crossing_again == crossing && backing.calls == cached_calls,
          "MS Gothic page-cache hit performed storage I/O or changed bytes");

    // A table-sized request must bypass the cache, while still joining short
    // reads into the exact byte sequence FreeType requested.
    std::vector<std::uint8_t> direct(ms_gothic_page_size + 257u);
    const std::uint64_t direct_offset = ms_gothic_page_size * 4u + 19u;
    const std::size_t calls_before_direct = backing.calls;
    check(cache->read(direct_offset, direct.data(), direct.size()) ==
              direct.size() &&
              std::equal(direct.begin(), direct.end(),
                         backing.bytes.begin() + direct_offset) &&
              backing.calls > calls_before_direct,
          "direct MS Gothic table read was truncated or cached incorrectly");
    const std::size_t calls_after_direct = backing.calls;
    check(cache->read(direct_offset, direct.data(), direct.size()) ==
              direct.size() && backing.calls > calls_after_direct,
          "large MS Gothic table read unexpectedly occupied the page cache");

    std::array<std::uint8_t, 256> tail{};
    const std::uint64_t tail_offset = backing.bytes.size() - 71u;
    check(cache->read(tail_offset, tail.data(), tail.size()) == 71u &&
              std::equal(tail.begin(), tail.begin() + 71,
                         backing.bytes.begin() + tail_offset),
          "MS Gothic EOF read did not preserve exact partial-read semantics");

    struct Metrics {
        int x = 0;
        int y = 0;
    };
    YuriGlyphMetricsCache<Metrics, 8> metrics_cache;
    const YuriGlyphCacheKey first{0x4ffau, 0x20000u, 28};
    Metrics value{17, 29};
    metrics_cache.store(first, value);
    Metrics found{};
    check(metrics_cache.find(first, found) && found.x == 17 && found.y == 29,
          "exact MS Gothic glyph metrics were not retained");
    check(!metrics_cache.find(YuriGlyphCacheKey{0x4ffbu, 0x20000u, 28},
                              found),
          "MS Gothic metrics cache accepted a different glyph key");

    // Font tags alternate between dialogue/name/menu heights. Height is part
    // of the exact key, so changing the active FreeType size must not flush
    // a bounded per-face cache or discard the prior height's hot metrics.
    const YuriGlyphCacheKey alternate_height{0x4ffau, 0x20000u, 32};
    metrics_cache.store(alternate_height, Metrics{21, 33});
    check(metrics_cache.find(first, found) && found.x == 17 && found.y == 29,
          "alternating MS Gothic height discarded prior exact metrics");
    check(metrics_cache.find(alternate_height, found) &&
              found.x == 21 && found.y == 33,
          "alternating MS Gothic height was not cached independently");
    metrics_cache.clear();
    check(!metrics_cache.find(first, found) &&
              !metrics_cache.find(alternate_height, found),
          "cleared MS Gothic metrics remained observable");

    check(YuriPreparedGlyphKey{0x4ffau, 3, 0, 28, 8} ==
              YuriPreparedGlyphKey{0x4ffau, 3, 0, 28, 8} &&
              !(YuriPreparedGlyphKey{0x4ffau, 3, 0, 28, 8} ==
                YuriPreparedGlyphKey{0x4ffau, 3, 0, 28, 9}),
          "prepared FreeType slot key omitted exact load flags");
}

void test_yuri_text_width_prefix_reuse() {
    constexpr std::uint64_t regular_22 =
        (std::uint64_t{0x00000000u} << 32u) | 22u;
    constexpr std::uint64_t bold_28 =
        (std::uint64_t{0x00000001u} << 32u) | 28u;
    const std::u16string cached = u"夕日に照らされる";
    const std::u16string extended = cached + u"公園";

    const auto reused = yuri_find_text_width_prefix(
        cached.data(), cached.size(), 154u, true, regular_22,
        extended.data(), extended.size(), true, regular_22);
    check(reused.reused && reused.code_units == cached.size() &&
              reused.width == 154u,
          "KAG growing line did not resume at its exact measured prefix");

    const std::u16string changed = u"夕日に照らされない";
    check(!yuri_find_text_width_prefix(
               cached.data(), cached.size(), 154u, true, regular_22,
               changed.data(), changed.size(), true, regular_22)
               .reused,
          "text-prefix cache accepted a non-identical UTF-16 prefix");
    check(!yuri_find_text_width_prefix(
               cached.data(), cached.size(), 154u, true, regular_22,
               extended.data(), extended.size(), true, bold_28)
               .reused,
          "text-prefix cache mixed MS Gothic size/style metric state");
    check(!yuri_find_text_width_prefix(
               cached.data(), cached.size(), 154u, false, regular_22,
               extended.data(), extended.size(), true, regular_22)
               .reused,
          "failed glyph measurement became a reusable prefix");
    check(!yuri_find_text_width_prefix(
               cached.data(), cached.size(), 154u, true, regular_22,
               extended.data(), extended.size(), false, regular_22)
               .reused,
          "uninitialized FreeType state reused prior metrics");
    check(!yuri_find_text_width_prefix(
               extended.data(), extended.size(), 198u, true, regular_22,
               cached.data(), cached.size(), true, regular_22)
               .reused,
          "KAG reline/truncation incorrectly reused a longer string");
    const std::u16string embedded_nul = {u'夕', u'日', u'\0', u'影'};
    const std::u16string embedded_nul_extended = {
        u'夕', u'日', u'\0', u'影', u'が'};
    check(!yuri_find_text_width_prefix(
               embedded_nul.data(), embedded_nul.size(), 44u, true,
               regular_22, embedded_nul_extended.data(),
               embedded_nul_extended.size(), true, regular_22)
               .reused,
          "embedded NUL skipped beyond Yuri's effective text terminator");

    // Golden additive-width model from Yuri's GetTextSize: every UTF-16 code
    // unit contributes independently, with no kerning or shaping between
    // units. Appending each unit must match a fresh full scan exactly.
    const std::u16string line = u"奇妙なことに、夕日に照らされる公園";
    std::u16string prior;
    std::uint32_t prior_width = 0;
    bool prior_valid = false;
    std::size_t measured_code_units = 0;
    const auto advance = [](char16_t ch) -> std::uint32_t {
        return ch == u'、' ? 11u : 22u;
    };
    for (char16_t ch : line) {
        std::u16string next = prior;
        next.push_back(ch);
        const auto prefix = yuri_find_text_width_prefix(
            prior.data(), prior.size(), prior_width, prior_valid, regular_22,
            next.data(), next.size(), true, regular_22);
        std::uint32_t incremental = prefix.width;
        for (std::size_t i = prefix.code_units; i < next.size(); ++i) {
            incremental += advance(next[i]);
            ++measured_code_units;
        }
        std::uint32_t full = 0;
        for (char16_t unit : next) full += advance(unit);
        check(incremental == full,
              "incremental KAG history width diverged from a full Yuri scan");
        prior = std::move(next);
        prior_width = incremental;
        prior_valid = true;
    }
    check(measured_code_units == line.size(),
          "growing KAG history line still rescanned its accumulated prefix");

    static_assert(sizeof(YuriTextWidthPrefixReuse) <= 16,
                  "prefix decision unexpectedly owns unbounded text state");
}

void test_rgba_frame_probe() {
    constexpr int width = 3;
    constexpr int height = 2;
    constexpr int pitch_pixels = 5;
    const std::array<std::uint32_t, pitch_pixels * height> black = {
        0xff000000u, 0xff000000u, 0xff000000u, 0x00ffffffu, 0x00ffffffu,
        0xff000000u, 0xff000000u, 0xff000000u, 0x00ffffffu, 0x00ffffffu,
    };
    const auto black_probe = probe_rgba_frame(
        black.data(), pitch_pixels * 4, width, height);
    check(!black_probe.has_visible_color() &&
              black_probe.opaque_pixels == width * height,
          "pitched opaque-black frame was accepted as game video");

    auto content = black;
    content[pitch_pixels + 2] = 0xff010203u;
    const auto content_probe = probe_rgba_frame(
        content.data(), pitch_pixels * 4, width, height);
    check(content_probe.has_visible_color() && content_probe.color_pixels == 1,
          "visible RGBA pixel was not detected in a pitched frame");
    check(content_probe.center_pixel == 0xff000000u,
          "RGBA frame center probe ignored source pitch");
}

void test_frame_update_gate() {
    FrameUpdateGate gate;
    check(!gate.pending() && gate.begin() == 0,
          "new frame gate requested a duplicate upload");

    gate.request();
    const auto first = gate.begin();
    check(first != 0 && gate.pending(),
          "draw-buffer update did not request presentation");
    gate.complete(first, false);
    check(gate.pending() && gate.begin() == first,
          "failed presentation discarded Yuri's draw-buffer update");

    gate.request();
    const auto second = gate.begin();
    check(second != first, "frame generation did not advance");
    gate.complete(first, true);
    check(gate.pending() && gate.begin() == second,
          "presentation consumed a newer in-flight update");
    gate.complete(second, true);
    check(!gate.pending() && gate.begin() == 0,
          "successful latest presentation remained dirty");

    // BasicDrawDevice reuses its software texture; another Show() still has
    // to become a new generation even though pointer identity is unchanged.
    gate.request();
    check(gate.pending() && gate.begin() > second,
          "same-texture Yuri update was deduplicated incorrectly");
}

void test_frame_damage_tracking() {
    FrameDamageRegion damage;
    damage.configure(100, 80);
    check(damage.empty(), "configured frame damage was not initially empty");
    damage.add(-20, 10, 20, 100);
    damage.add(50, 5, 120, 30);
    check(!damage.empty() && !damage.full() &&
              damage.rect().left == 0 && damage.rect().top == 5 &&
              damage.rect().right == 100 && damage.rect().bottom == 80,
          "frame damage was not clamped and bounded correctly");
    damage.mark_full(100, 80);
    check(damage.full(), "full-frame damage was not recognized");

    PresentationDamageTracker<5> tracker;
    tracker.configure(1280, 960);
    for (int frame = 0; frame < 5; ++frame) {
        FrameDamageRegion update;
        update.configure(1280, 960);
        update.add(frame * 10, 100, frame * 10 + 8, 140);
        tracker.add_frame(update);
        check(tracker.current().full(),
              "new presentation texture was not initialized in full");
        tracker.complete_current();
    }

    FrameDamageRegion sixth;
    sixth.configure(1280, 960);
    sixth.add(50, 90, 58, 150);
    tracker.add_frame(sixth);
    check(tracker.current_index() == 0 && !tracker.current().full() &&
              tracker.current().rect().left == 10 &&
              tracker.current().rect().top == 90 &&
              tracker.current().rect().right == 58 &&
              tracker.current().rect().bottom == 150,
          "rotating texture lost accumulated damage since its prior frame");

    // A failed upload does not advance the texture. The next engine update
    // must extend the same pending region before that texture is retried.
    FrameDamageRegion retry_update;
    retry_update.configure(1280, 960);
    retry_update.add(5, 80, 70, 160);
    tracker.add_frame(retry_update);
    check(tracker.current_index() == 0 &&
              tracker.current().rect().left == 5 &&
              tracker.current().rect().top == 80 &&
              tracker.current().rect().right == 70 &&
              tracker.current().rect().bottom == 160,
          "failed presentation discarded damage before retry");
    tracker.complete_current();
    check(tracker.current_index() == 1 &&
              tracker.current().rect().left == 5 &&
              tracker.current().rect().right == 70,
          "next rotating texture did not retain its own damage history");
}

void test_presentation_surface_transaction() {
    using State = PresentationSurfaceState<5>;
    State state;
    const State::TextureArray initial{11, 12, 13, 14, 15};
    const State::TextureArray replacement{21, 22, 23, 24, 25};

    check(!state.valid() && state.resize_required(1280, 960),
          "an unallocated presentation surface suppressed its first resize");
    const auto partial_initial =
        state.complete_resize(1280, 960, initial, 4);
    check(!partial_initial.committed && !state.valid() &&
              state.resize_required(1280, 960),
          "partial initial texture allocation poisoned the same-size retry");

    const auto first = state.complete_resize(1280, 960, initial, 5);
    check(first.committed && state.matches(1280, 960) &&
              first.retired == State::TextureArray{},
          "complete initial texture allocation was not committed atomically");
    check(!state.resize_required(1280, 960),
          "a committed same-size surface was allocated again");

    const auto partial_replacement =
        state.complete_resize(960, 544, replacement, 3);
    check(!partial_replacement.committed && state.matches(1280, 960) &&
              state.textures() == initial &&
              state.resize_required(960, 544),
          "failed resize replaced the last valid surface or blocked retry");

    State::TextureArray duplicate = replacement;
    duplicate[4] = duplicate[0];
    const auto invalid_replacement =
        state.complete_resize(960, 544, duplicate, 5);
    check(!invalid_replacement.committed && state.matches(1280, 960) &&
              state.textures() == initial,
          "invalid candidate texture names mutated the active surface");

    const auto retry = state.complete_resize(960, 544, replacement, 5);
    check(retry.committed && retry.retired == initial &&
              state.matches(960, 544) && state.textures() == replacement,
          "successful retry did not atomically replace and retire the old set");
}

void test_yuri_window_update_policy() {
    check(yuri_needs_full_window_exposure(false, false),
          "first normal window update did not bootstrap the surface");
    check(!yuri_needs_full_window_exposure(false, true),
          "normal window update discarded Yuri's dirty regions");
    check(yuri_needs_full_window_exposure(true, true),
          "utEntire did not invalidate the complete window");
}

void test_vita_thread_policy() {
    check(kVitaMainThreadPolicy.priority == 160 &&
              kVitaMainThreadPolicy.cpu_affinity_mask == 0x00010000 &&
              kVitaRenderWorkerPolicies[0].priority == 64 &&
              kVitaRenderWorkerPolicies[0].cpu_affinity_mask == 0x00020000 &&
              kVitaRenderWorkerPolicies[1].cpu_affinity_mask == 0x00040000,
          "Vita thread placement diverged from the documented port policy");

    constexpr std::array<int, kYuriThreadPriorityCount> pthread_priorities{
        128, 139, 149, 160, 170, 181, 191};
    constexpr std::array<int, kYuriThreadPriorityCount> native_priorities{
        191, 180, 170, 159, 149, 138, 128};
    for (int rank = 0; rank < kYuriThreadPriorityCount; ++rank) {
        const int pthread_priority =
            vita_pthread_priority_for_yuri_rank(rank);
        check(pthread_priority == pthread_priorities[rank],
              "Yuri priority rank did not map across Vita pthread range");
        check(vita_yuri_rank_for_pthread_priority(pthread_priority) == rank,
              "Vita pthread priority did not round-trip to its Yuri rank");
        check(vita_native_priority_for_pthread_priority(pthread_priority) ==
                  native_priorities[rank],
              "Vita pthread priority inversion no longer matches its backend");
    }
    check(vita_pthread_priority_for_yuri_rank(-1) ==
                  kVitaPthreadPriorityMin &&
              vita_pthread_priority_for_yuri_rank(
                  kYuriThreadPriorityCount) == kVitaPthreadPriorityMax &&
              vita_yuri_rank_for_pthread_priority(
                  kVitaPthreadPriorityMin - 1) == 0 &&
              vita_yuri_rank_for_pthread_priority(
                  kVitaPthreadPriorityMax + 1) ==
                  kYuriThreadPriorityCount - 1,
          "Vita pthread priority mapping did not clamp invalid boundaries");
}

void test_render_task_pool() {
    std::atomic<int> initialized_workers{0};
    RenderTaskPool pool(2, [&](int worker_index) {
        check(worker_index == 1 || worker_index == 2,
              "render worker received an invalid stable index");
        initialized_workers.fetch_add(1, std::memory_order_relaxed);
    });
    check(initialized_workers.load(std::memory_order_relaxed) == 2,
          "persistent render workers were not initialized exactly once");

    std::array<std::atomic<int>, 17> visits{};
    std::atomic<int> worker_context_visits{0};
    const std::thread::id owner = std::this_thread::get_id();
    pool.run(static_cast<int>(visits.size()), [&](int index) {
        visits[static_cast<std::size_t>(index)].fetch_add(
            1, std::memory_order_relaxed);
        if (index == 0) {
            check(std::this_thread::get_id() == owner &&
                      !RenderTaskPool::in_worker_context(),
                  "render job zero left the engine thread");
        } else if (RenderTaskPool::in_worker_context()) {
            worker_context_visits.fetch_add(1, std::memory_order_relaxed);
        }
    });
    for (const auto& visit : visits)
        check(visit.load(std::memory_order_relaxed) == 1,
              "render task pool did not execute every partition exactly once");
    check(worker_context_visits.load(std::memory_order_relaxed) == 16,
          "numbered render partitions did not stay on persistent workers");

    std::atomic<int> zero_visits{0};
    pool.run(0, [&](int) {
        zero_visits.fetch_add(1, std::memory_order_relaxed);
    });
    check(zero_visits.load(std::memory_order_relaxed) == 0,
          "zero-sized render dispatch executed a task");
    for (int task_count = 1; task_count <= 3; ++task_count) {
        std::array<std::atomic<int>, 3> small_visits{};
        pool.run(task_count, [&](int index) {
            small_visits[static_cast<std::size_t>(index)].fetch_add(
                1, std::memory_order_relaxed);
        });
        for (int index = 0; index < task_count; ++index)
            check(small_visits[static_cast<std::size_t>(index)].load(
                      std::memory_order_relaxed) == 1,
                  "small render dispatch lost or duplicated a partition");
    }

    // A persistent pool must actually overlap work; exact visit counts alone
    // would also pass with Yuri's old serial compatibility shim.
    std::atomic<int> active_tasks{0};
    std::atomic<int> maximum_active_tasks{0};
    pool.run(3, [&](int) {
        const int active =
            active_tasks.fetch_add(1, std::memory_order_acq_rel) + 1;
        int observed = maximum_active_tasks.load(std::memory_order_relaxed);
        while (observed < active &&
               !maximum_active_tasks.compare_exchange_weak(
                   observed, active, std::memory_order_relaxed)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        active_tasks.fetch_sub(1, std::memory_order_acq_rel);
    });
    check(maximum_active_tasks.load(std::memory_order_relaxed) >= 2,
          "render task pool never overlapped owner and worker execution");

    // A worker failure must return to the engine thread and leave the pool
    // reusable; a detached exception would otherwise terminate the process.
    bool propagated = false;
    try {
        pool.run(3, [](int index) {
            if (index == 1) throw std::runtime_error("worker failure");
        });
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    check(propagated, "render worker exception was not propagated");

    bool owner_failure_propagated = false;
    try {
        pool.run(3, [](int index) {
            if (index == 0) throw std::runtime_error("owner failure");
        });
    } catch (const std::runtime_error&) {
        owner_failure_propagated = true;
    }
    check(owner_failure_propagated,
          "render owner exception was not propagated after the worker barrier");

    std::atomic<int> reuse_count{0};
    pool.run(3, [&](int) {
        reuse_count.fetch_add(1, std::memory_order_relaxed);
    });
    check(reuse_count.load(std::memory_order_relaxed) == 3 &&
              initialized_workers.load(std::memory_order_relaxed) == 2,
          "render task pool was not reusable after a failed dispatch");

    std::atomic<int> nested_visits{0};
    pool.run(2, [&](int index) {
        if (index != 1) return;
        const std::thread::id worker = std::this_thread::get_id();
        pool.run(2, [&](int) {
            check(std::this_thread::get_id() == worker &&
                      RenderTaskPool::in_worker_context(),
                  "nested worker dispatch escaped its serial fallback");
            nested_visits.fetch_add(1, std::memory_order_relaxed);
        });
    });
    check(nested_visits.load(std::memory_order_relaxed) == 2,
          "nested render dispatch did not execute serially on its caller");

    std::atomic<int> generation_visits{0};
    for (int generation = 0; generation < 2000; ++generation)
        pool.run(3, [&](int) {
            generation_visits.fetch_add(1, std::memory_order_relaxed);
        });
    check(generation_visits.load(std::memory_order_relaxed) == 6000,
          "persistent render pool lost work across repeated generations");

    bool initializer_failure_propagated = false;
    try {
        RenderTaskPool failing_pool(2, [](int worker_index) {
            if (worker_index == 1)
                throw std::runtime_error("initializer failure");
        });
    } catch (const std::runtime_error&) {
        initializer_failure_propagated = true;
    }
    check(initializer_failure_propagated,
          "render worker initializer failure did not cleanly propagate");
}

void test_yuri_engine_tick_pacer() {
    check(yuri_frame_delay_us(1000000, 1000000) == 16667,
          "Yuri loop did not retain its 60 Hz frame lifetime");
    check(yuri_frame_delay_us(1000000, 1005000) == 11667,
          "Yuri pacing ignored time already spent executing a frame");
    check(yuri_frame_delay_us(1000000, 1016667) == 0 &&
              yuri_frame_delay_us(1000000, 1020000) == 0,
          "slow Yuri frame acquired an additional sleep delay");

    check(!yuri_performance_snapshot_is_safe(4999999, 5000000, 0),
          "performance snapshot ran before its deadline");
    check(!yuri_performance_snapshot_is_safe(5200000, 5000000, 5000000),
          "performance snapshot interrupted active rendering");
    check(yuri_performance_snapshot_is_safe(5250000, 5000000, 5000000),
          "performance snapshot did not run after a visual idle window");
    check(!yuri_performance_snapshot_is_safe(5000000, 5000000, 5100000),
          "performance snapshot accepted a clock-ordering violation");
}

void test_yuri_presentation_reference_contract() {
    check(yuri_texture_has_single_mutable_owner(1, 0),
          "a sole Yuri texture owner stopped being independent");
    check(yuri_texture_has_single_mutable_owner(2, 1) &&
              yuri_texture_has_single_mutable_owner(3, 2),
          "read-only presentation holds triggered bitmap copy-on-write");
    check(!yuri_texture_has_single_mutable_owner(3, 1),
          "two mutable owners were mistaken for one owner plus a presenter");
    check(!yuri_texture_has_single_mutable_owner(1, 1) &&
              !yuri_texture_has_single_mutable_owner(0, 0) &&
              !yuri_texture_has_single_mutable_owner(1, -1),
          "invalid or presentation-only texture ownership became writable");

    // Mirror Yuri's deferred Release() contract without importing the engine's
    // visual subsystem into the host test binary. A last release queues the
    // texture with RefCount still at one; RecycleProcess() deletes it later.
    struct ReferenceState {
        int total = 1;
        int presentations = 0;
        int queue_entries = 0;
        int recycler_deletes = 0;
        bool queued = false;
        bool deleted = false;

        bool independent() const {
            return yuri_texture_has_single_mutable_owner(
                total, presentations);
        }

        void add_owner() {
            require_live();
            ++total;
        }

        void release_owner() { release(); }

        void add_presentation() {
            require_live();
            ++total;
            ++presentations;
        }

        void release_presentation() {
            require_live();
            if (presentations <= 0)
                throw std::logic_error("presentation reference underflow");
            --presentations;
            release();
        }

        void recycle() {
            if (queued && !deleted) {
                deleted = true;
                ++recycler_deletes;
            }
        }

    private:
        void require_live() const {
            if (queued || deleted)
                throw std::logic_error("reference operation after last release");
        }

        void release() {
            require_live();
            if (total <= 0)
                throw std::logic_error("total reference underflow");
            if (total == 1) {
                queued = true;
                ++queue_entries;
            } else {
                --total;
            }
        }
    };

    ReferenceState owner_first;
    owner_first.add_presentation();
    check(owner_first.independent(),
          "one presenter made a sole mutable owner shared");
    owner_first.release_owner();
    check(!owner_first.queued && !owner_first.independent() &&
              owner_first.total == 1 && owner_first.presentations == 1,
          "owner-first release destroyed a presenter-held texture");
    owner_first.release_presentation();
    check(owner_first.queued && !owner_first.deleted &&
              owner_first.queue_entries == 1,
          "last presenter did not queue an owner-released texture exactly once");
    owner_first.recycle();
    owner_first.recycle();
    check(owner_first.deleted && owner_first.recycler_deletes == 1,
          "deferred recycler deleted a queued texture more than once");

    ReferenceState presenter_first;
    presenter_first.add_presentation();
    presenter_first.release_presentation();
    check(!presenter_first.queued && presenter_first.independent() &&
              presenter_first.total == 1 &&
              presenter_first.presentations == 0,
          "presenter-first release discarded the remaining mutable owner");
    presenter_first.release_owner();
    check(presenter_first.queued && presenter_first.queue_entries == 1,
          "mutable owner did not queue after the presenter released first");

    ReferenceState multiple_presenters;
    multiple_presenters.add_presentation();
    multiple_presenters.add_presentation();
    check(multiple_presenters.independent(),
          "multiple read-only presenters triggered copy-on-write");
    multiple_presenters.release_owner();
    check(!multiple_presenters.independent() && !multiple_presenters.queued,
          "presentation-only ownership was treated as mutable or destroyed");
    multiple_presenters.release_presentation();
    check(!multiple_presenters.queued &&
              multiple_presenters.total == 1 &&
              multiple_presenters.presentations == 1,
          "first of multiple presenters released the texture prematurely");
    multiple_presenters.release_presentation();
    check(multiple_presenters.queued &&
              multiple_presenters.queue_entries == 1,
          "last of multiple presenters did not queue exactly once");

    ReferenceState mutable_alias;
    mutable_alias.add_owner();
    mutable_alias.add_presentation();
    check(!mutable_alias.independent(),
          "a real second mutable owner was hidden by a presentation hold");
    mutable_alias.release_owner();
    check(mutable_alias.independent(),
          "releasing the real alias did not restore sole mutable ownership");
    mutable_alias.release_presentation();
    check(mutable_alias.independent() && !mutable_alias.queued,
          "presenter release discarded the sole mutable owner after aliasing");
    mutable_alias.release_owner();
    check(mutable_alias.queue_entries == 1,
          "aliased texture was not queued exactly once after all valid releases");

    bool underflow_rejected = false;
    try {
        ReferenceState invalid_release;
        invalid_release.release_presentation();
    } catch (const std::logic_error&) {
        underflow_rejected = true;
    }
    check(underflow_rejected,
          "presentation-reference underflow was silently accepted");
}

void test_deferred_recycle_fixed_point() {
    std::vector<int> pending{3};
    std::uint64_t completed = 0;
    std::size_t calls = 0;
    const std::size_t batches = drain_deferred_recycle_batches(
        [&]() { return completed; },
        [&]() {
            ++calls;
            std::vector<int> current;
            current.swap(pending);
            for (const int depth : current) {
                ++completed;
                if (depth > 0) pending.push_back(depth - 1);
            }
        });
    check(batches == 4 && completed == 4 && calls == 5 && pending.empty(),
          "deferred recycler stopped before destructor-enqueued batches drained");

    calls = 0;
    const std::size_t empty_batches = drain_deferred_recycle_batches(
        [&]() { return completed; }, [&]() { ++calls; });
    check(empty_batches == 0 && calls == 1,
          "empty deferred recycler did not terminate after one stable probe");
}

void test_vita_memory_budget_contract() {
    check(bitmap_allocation_bytes(1280, 960) == 4915240,
          "Vita bitmap budget no longer matches TVPAllocBitmapBits");
    check(kVitaNewlibHeapBytes == 128u * 1024u * 1024u,
          "Vita newlib heap no longer preserves bitmap memblock space");
    check(kVitaGlApplicationRamThresholdBytes == 80 * 1024 * 1024,
          "VitaGL threshold no longer reserves bitmap memblock space");
    check(kVitaNewlibHeapBytes + kVitaGlApplicationRamThresholdBytes ==
              208u * 1024u * 1024u,
          "VitaGL pool budget changed while splitting bitmap storage");
    check(kVitaGlApplicationRamThresholdBytes ==
              static_cast<int>(kVitaBitmapMemblockBudget +
                               kVitaGlNonBitmapHeadroomBytes),
          "bitmap memblocks no longer have independent USER_RW headroom");
    check(vita_bitmap_uses_memblock(1024u * 1024u) &&
              !vita_bitmap_uses_memblock(1024u * 1024u - 1),
          "large bitmap allocation threshold changed at its boundary");
    check(vita_bitmap_memblock_bytes(4915240) == 4919296,
          "retail bitmap memblock rounding is not page exact");
    check(vita_bitmap_memblock_bytes(
              std::numeric_limits<std::size_t>::max()) == 0,
          "bitmap memblock size overflow was not rejected");
    check(vita_bitmap_memblock_budget_allows(0, 4915240) &&
              !vita_bitmap_memblock_budget_allows(
                  kVitaBitmapMemblockBudget - 4096, 4915240),
          "bitmap memblock live-budget gate is not overflow safe");
}

void test_vita_render_surface_contract() {
    const auto retail = fit_vita_render_surface(1280, 960);
    check(retail.width == 725 && retail.height == 544 && retail.is_scaled(),
          "1280x960 render target was not fitted to the Vita display");
    check(static_cast<std::size_t>(retail.width) * retail.height * 4 <
              kRetailBitmapAllocationBytes / 3,
          "Vita-sized retail render target does not remove enough GPU pressure");

    const auto widescreen = fit_vita_render_surface(1920, 1080);
    check(widescreen.width == 960 && widescreen.height == 540,
          "widescreen render target aspect fit is incorrect");

    const auto native = fit_vita_render_surface(640, 480);
    check(native.width == 640 && native.height == 480 && !native.is_scaled(),
          "already fitting render target was unnecessarily rescaled");
}

void test_empty_yuri_string() {
    const TJS::ttstr empty;
    check(empty.GetLen() == 0, "Yuri empty ttstr length dereferenced null");
    check(empty.length() == 0, "Yuri empty ttstr length alias failed");
    const TJS::tTJSVariant variant(TJS_W(""));
    check(variant.Type() == tvtString, "empty Yuri variant lost string type");
    check(variant.GetString() && variant.GetString()[0] == 0,
          "empty Yuri variant has no stable string value");
    check(variant.AsInteger() == 0 && variant.AsReal() == 0.0,
          "empty Yuri string numeric conversion failed");
    check((+variant).AsInteger() == 0,
          "empty Yuri unary numeric conversion failed");

    const TJS::tTJSVariant integer(static_cast<tjs_int>(42));
    const TJS::tTJSVariant real(static_cast<tTVReal>(1.5));
    const auto empty_then_integer = variant + integer;
    const auto integer_then_empty = integer + variant;
    check(TJS::ttstr(empty_then_integer).AsStdString() == "42",
          "empty + integer Yuri concatenation failed");
    check(TJS::ttstr(integer_then_empty).AsStdString() == "42",
          "integer + empty Yuri concatenation failed");

    TJS::tTJSVariant append_right(TJS_W(""));
    append_right += real;
    check(TJS::ttstr(append_right).AsStdString() == "1.5",
          "empty += real Yuri concatenation failed");
    TJS::tTJSVariant append_left(real);
    append_left += TJS::tTJSVariant(TJS_W(""));
    check(TJS::ttstr(append_left).AsStdString() == "1.5",
          "real += empty Yuri concatenation failed");

    const TJS::tTJSVariant another_empty(TJS_W(""));
    check(!variant.GreaterThan(another_empty) &&
              !variant.LittlerThan(another_empty),
          "empty Yuri relational comparison dereferenced null");
}

void test_normalize() {
    check(normalize_game_name(" ＡＢＣ／色情・教団！ ") == "abc色情教団",
          "game title normalization failed");
}

void test_vita_storage_paths() {
    constexpr std::u16string_view game =
        u"ux0:data/krkrvita/games/色情教団";
    constexpr std::u16string_view patch =
        u"ux0:data/krkrvita/patches/44bb539bf9510882/patch.tjs";

    check(vita_native_to_storage_path(game) ==
              u"file://./ux0:data/krkrvita/games/色情教団",
          "Vita game path normalized to the wrong Kirikiri URI");
    check(vita_storage_to_native_path(
              u"file://./ux0:data/krkrvita/games/色情教団/data.xp3") ==
              u"ux0:data/krkrvita/games/色情教団/data.xp3",
          "Kirikiri URI did not round-trip to a Vita device path");
    check(vita_storage_to_native_path(
              u"./ux0:/data/krkrvita/games/色情教団/data.xp3") ==
              u"ux0:data/krkrvita/games/色情教団/data.xp3",
          "legacy slash-after-device path was not canonicalized");
    check(vita_storage_to_native_path(vita_native_to_storage_path(patch)) == patch,
          "retail patch path did not round-trip exactly");
    check(vita_directory_path(game) ==
              u"ux0:data/krkrvita/games/色情教団/",
          "project directory lost the Japanese game component");
    check(vita_directory_path(game) + u"savedata/" ==
              u"ux0:data/krkrvita/games/色情教団/savedata/",
          "savedata escaped the selected game directory");
    check(vita_device_prefix_length(u"file://./ux0:data") == 0,
          "storage URI was mistaken for a native Vita path");
}

void test_virtual_cd() {
    using krkrvita::virtual_cd_is_present;
    check(!virtual_cd_is_present("", "ux0:data/krkrvita/games/sharin"),
          "empty CD labels must remain absent");
    check(!virtual_cd_is_present("syarin", ""),
          "a missing project cannot satisfy a virtual CD check");
    check(virtual_cd_is_present("syarin", "ux0:data/krkrvita/games/sharin"),
          "mounted Vita projects must satisfy non-empty CD labels");
    check(virtual_cd_is_present("任意のボリューム",
                                "file://./ux0:data/krkrvita/games/sharin"),
          "virtual CD labels are not limited to ASCII");
}

void test_vita_title_id() {
    GameDescriptor game;
    game.fingerprint = "44bb539bf9510882";
    check(bubble_title_id(game) == "KRVG27323", "legal Vita title ID derivation failed");
    check(is_vita_title_id("KRVG27323"), "legal Vita title ID rejected");
    check(!is_vita_title_id("K44BB539"), "hex Vita title ID accepted");
    check(!is_vita_title_id("A-K44BB53"), "punctuated Vita title ID accepted");
}

void test_vita_input_profile() {
    GameDescriptor game;
    game.fingerprint = "44bb539bf9510882";
    game.root = "/games/sample";
    game.display_name = "sample";
    const GameProfile profile = GameProfile::defaults(game);
    const std::map<std::string, std::string> required = {
        {"dpad_up", "key_up"}, {"dpad_down", "key_down"},
        {"dpad_left", "key_left"}, {"dpad_right", "key_right"},
        {"circle", "key_enter"}, {"cross", "mouse_right"},
        {"triangle", "mouse_wheel_up"}, {"ltrigger", "mouse_left"},
        {"rtrigger", "key_control"}, {"square", "disabled"},
        {"start", "disabled"}, {"select", "disabled"},
        {"left_stick", "mouse_cursor"},
        {"front_touch", "mouse_absolute"},
    };
    check(profile.input.mapping_version == vita_input_mapping_version,
          "global Vita mapping did not use its current profile version");
    for (const auto& [source, action] : required) {
        const auto binding = profile.input.bindings.find(source);
        check(binding != profile.input.bindings.end() &&
                  binding->second == action,
              "wrong default Vita binding for " + source);
    }

    const auto root =
        std::filesystem::temp_directory_path() / "krkrvita-input-profile-test";
    const auto path = root / "active.ini";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::string error;
    check(profile.save(path, &error), "cannot save Vita input profile: " + error);
    const auto loaded = GameProfile::load(path, &error);
    check(loaded.has_value(), "cannot reload Vita input profile: " + error);
    check(loaded->input.mapping_version == vita_input_mapping_version &&
              loaded->input.bindings == profile.input.bindings,
          "Vita input mapping did not survive profile serialization");

    check(vita_input_binding_is_superseded_default(
              1, "ltrigger", "key_pageup") &&
              vita_input_binding_is_superseded_default(
                  2, "ltrigger", "key_pageup"),
          "old L-trigger defaults would override the global click mapping");
    check(vita_input_binding_is_superseded_default(
              2, "cross", "mouse_right") &&
              vita_input_binding_is_superseded_default(
                  1, "cross", "mouse_left"),
          "historical face-button defaults were not recognized");
    check(vita_input_binding_is_superseded_default(
              3, "cross", "mouse_left") &&
              vita_input_binding_is_superseded_default(
                  3, "select", "menu"),
          "version-3 defaults would override the corrected global mapping");
    check(!vita_input_binding_is_superseded_default(
              2, "ltrigger", "mouse_right") &&
              !vita_input_binding_is_superseded_default(
                  vita_input_mapping_version, "ltrigger", "key_pageup"),
          "genuine or current-version profile overrides were discarded");

    const auto unversioned_path = root / "unversioned.ini";
    {
        std::ofstream stream(unversioned_path);
        stream << "game_id=legacy\n"
               << "game_path=/games/legacy\n"
               << "bind.ltrigger=key_pageup\n";
    }
    const auto unversioned = GameProfile::load(unversioned_path, &error);
    check(unversioned.has_value() &&
              unversioned->input.mapping_version == 1,
          "unversioned input profiles were not classified for migration");
    std::filesystem::remove_all(root, ec);
}

void test_keyboard_repeat_scheduler() {
    KeyboardRepeatScheduler scheduler;
    std::vector<std::uint16_t> repeats;
    scheduler.press(0x11, 1000000);
    scheduler.pump(1529999, [&](std::uint16_t key) { repeats.push_back(key); });
    check(repeats.empty(), "keyboard repeat fired before Yuri's hold delay");
    scheduler.pump(1530000, [&](std::uint16_t key) { repeats.push_back(key); });
    check(repeats == std::vector<std::uint16_t>{0x11},
          "held Ctrl did not produce its first repeat key-down");
    scheduler.pump(1620000, [&](std::uint16_t key) { repeats.push_back(key); });
    check(repeats.size() == 4,
          "keyboard repeat did not preserve Yuri's 30 ms interval");
    scheduler.release(0x11);
    scheduler.pump(3000000, [&](std::uint16_t key) { repeats.push_back(key); });
    check(repeats.size() == 4, "released key continued to repeat");

    scheduler.press(0x26, 0);
    int catch_up = 0;
    scheduler.pump(5000000, [&](std::uint16_t) { ++catch_up; });
    check(catch_up == KeyboardRepeatScheduler::catch_up_limit,
          "stalled input frame was allowed to flood Kirikiri's event queue");
}

void test_pvf_kirikiri_baseline() {
    check(pvf_kirikiri_raster_height(28) == 30 &&
              pvf_kirikiri_raster_height(22) == 24,
          "PVF system glyphs were not enlarged by two increments");
    check(pvf_kirikiri_raster_height(256) == 256,
          "PVF raster growth exceeded libpvf's supported engine limit");
    check(pvf_kirikiri_internal_leading(28) == 5,
          "28-pixel dialogue font has the wrong PVF internal leading");
    check(pvf_kirikiri_internal_leading(22) == 4,
          "22-pixel history font has the wrong PVF internal leading");
    check(pvf_kirikiri_baseline(28, 26) == 41,
          "PVF glyph baseline lacks the calibrated ten-pixel correction");
    check(pvf_kirikiri_internal_leading(0) == 0,
          "empty PVF font cell acquired internal leading");
}

void test_audio_playback_state() {
    AudioPlaybackState state;
    check(state.start_action(true, false) == AudioStartAction::none,
          "fresh audio started before Kirikiri requested playback");
    state.play();
    check(state.start_action(true, false) ==
              AudioStartAction::rewind_and_start,
          "fresh voice playback did not require a sample-zero rewind");
    state.started();
    check(state.start_action(true, true) == AudioStartAction::none,
          "playing audio requested a redundant device restart");
    check(state.start_action(true, false) == AudioStartAction::resume,
          "an OpenAL underrun discarded Kirikiri's play intent");

    state.pause();
    check(!state.requested() &&
              state.start_action(true, false) == AudioStartAction::none,
          "paused audio continued to request device playback");
    state.play();
    check(state.start_action(true, false) == AudioStartAction::resume,
          "pause/resume incorrectly rewound the voice line");

    state.reset();
    state.play();
    check(state.start_action(true, false) ==
              AudioStartAction::rewind_and_start,
          "reused OpenAL source retained the preceding line's offset");
}

void test_vita_touch_mapping() {
    constexpr TouchPanelBounds vita_panel{0, 0, 1919, 1087};
    const TouchPoint full_top_left =
        map_vita_touch_to_layer(0, 0, vita_panel, 960, 544);
    const TouchPoint full_bottom_right =
        map_vita_touch_to_layer(1919, 1087, vita_panel, 960, 544);
    check(full_top_left.x == 0 && full_top_left.y == 0,
          "front touch origin did not map to the layer origin");
    check(full_bottom_right.x == 959 && full_bottom_right.y == 543,
          "front touch extent did not reach the full layer");

    const TouchPoint game_center =
        map_vita_touch_to_layer(960, 544, vita_panel, 1280, 960);
    check(std::abs(game_center.x - 640) <= 1 &&
              std::abs(game_center.y - 480) <= 1,
          "4:3 retail touch center did not map to the paint-box center");
    const TouchPoint game_bottom_right =
        map_vita_touch_to_layer(1919, 1087, vita_panel, 1280, 960);
    check(game_bottom_right.x == 1279 && game_bottom_right.y == 959,
          "4:3 retail touch was restricted to a top-left subregion");
}

void test_manifest_and_resolver() {
    constexpr auto source = R"JS(
      var all_data = [
        [1520434816, "ORCSOFT／DWARFSOFT", "色情教団", "色情教団",
          ["ORCSOFT／DWARFSOFT/色情教団/patch.tjs",
           "ORCSOFT／DWARFSOFT/色情教団/xp3filter.tjs"]],
        [1, "Other", "Different Game", "Different Game", ["Other/xp3filter.tjs"]]
      ];
    )JS";
    const auto manifest = PatchManifest::parse(source);
    check(manifest.entries().size() == 2, "manifest entry count failed");
    GameDescriptor game;
    game.directory_name = "色情教団";
    game.display_name = game.directory_name;
    game.executable_stem = "sikijokyodan";
    const auto resolution = PatchResolver::resolve(game, manifest);
    check(resolution.automatic, "sample patch was not selected automatically");
    check(resolution.best() && resolution.best()->entry->brand == "ORCSOFT／DWARFSOFT",
          "wrong patch selected");
    check(is_safe_patch_path("Brand/Game/xp3filter.tjs"), "safe patch rejected");
    check(!is_safe_patch_path("../xp3filter.tjs"), "traversal patch accepted");
    check(is_safe_patch_path("../patch/old_core_patch/Override2.tjs"),
          "shared compatibility patch rejected");
    check(is_safe_patch_path(
              "https://github.com/zeas2/Kirikiroid2_patch/releases/download/tag/patch2.xp3"),
          "trusted release XP3 rejected");
    check(!is_safe_patch_path("https://example.com/patch2.xp3"),
          "untrusted patch URL accepted");
    check(!is_safe_patch_path("Brand/tool.exe"), "executable patch accepted");
}

class MemoryHttpClient final : public HttpClient {
public:
    std::unordered_map<std::string, std::vector<std::uint8_t>> responses;
    int requests = 0;

    HttpResult get(std::string_view url) override {
        ++requests;
        const auto found = responses.find(std::string(url));
        if (found == responses.end()) return {404, {}, "not found"};
        return {200, found->second, {}};
    }
};

void test_patch_cache_integrity() {
    const auto root = std::filesystem::temp_directory_path() / "krkrvita-patch-cache-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    PatchEntry entry;
    entry.files = {"Brand/Game/xp3filter.tjs"};
    const std::string script =
        "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,h);});";
    MemoryHttpClient http;
    http.responses[patch_file_url(entry.files.front())] =
        std::vector<std::uint8_t>(script.begin(), script.end());
    PatchRepository repository(root);
    auto fetched = repository.fetch_bundle(http, entry);
    check(fetched.size() == 1 && http.requests == 1, "patch was not fetched");
    std::ofstream(fetched.front().cache_path, std::ios::binary | std::ios::trunc)
        << "corrupt";
    fetched = repository.fetch_bundle(http, entry);
    check(http.requests == 2, "corrupt cached patch was trusted");
    std::ifstream repaired(fetched.front().cache_path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(repaired)),
                               std::istreambuf_iterator<char>());
    check(contents == script, "corrupt patch cache was not repaired");
    std::filesystem::remove_all(root, ec);
}

void test_filter_detection() {
    const std::vector<std::uint8_t> png = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
        0, 0, 0, 13, 'I', 'H', 'D', 'R',
        0, 0, 5, 0, 0, 0, 3, 0, 8, 6, 0, 0, 0,
    };
    const std::vector<std::uint8_t> ogg = {'O', 'g', 'g', 'S', 0, 2, 0, 0, 0, 0};
    std::vector<FilterSample> samples = {
        {0x123456a5, 0, "image.png", png},
        {0xabcdef3c, 0, "voice.ogg", ogg},
    };
    FilterRule encrypt{FilterOperation::XorHash};
    for (auto& sample : samples) encrypt.apply(sample.hash, 0, sample.bytes);
    const auto analysis = FilterHeuristic::analyze(samples);
    const auto detected = analysis.rule;
    check(detected.has_value(), "hash XOR filter not detected: " + analysis.reason);
    check(detected->operation == FilterOperation::XorHash, "wrong filter detected");
    check(detected->to_tjs().find("b.xor(0,l,h)") != std::string::npos,
          "wrong filter script generated");
}

std::vector<FilterSample> filter_known_plaintext_samples() {
    std::vector<std::uint8_t> png = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
        0, 0, 0, 13, 'I', 'H', 'D', 'R',
        0, 0, 5, 0, 0, 0, 3, 0, 8, 6, 0, 0, 0,
        0, 0, 0, 0,
    };
    std::vector<std::uint8_t> ogg(32, 0);
    std::copy_n("OggS", 4, ogg.begin());
    ogg[4] = 0;
    ogg[26] = 1;
    ogg[27] = 4;
    std::vector<std::uint8_t> wav(32, 0);
    std::copy_n("RIFF", 4, wav.begin());
    std::copy_n("WAVEfmt ", 8, wav.begin() + 8);
    std::vector<std::uint8_t> script;
    const std::string script_line = "@iscript\nSystem.title = 'heuristic validation';\n@endscript\n";
    while (script.size() < 512)
        script.insert(script.end(), script_line.begin(), script_line.end());
    return {
        {0x123456a5, 0, "image.png", std::move(png)},
        {0xabcdef3c, 0, "voice.ogg", std::move(ogg)},
        {0x73a91e62, 0, "effect.wav", std::move(wav)},
        {0x4ed819b7, 0, "validation.scn", std::move(script)},
    };
}

void test_generalized_filter_detection() {
    const auto detect_xor = [](FilterRule encryption) {
        auto samples = filter_known_plaintext_samples();
        for (auto& sample : samples)
            encryption.apply(sample.hash, sample.offset, sample.bytes);
        return FilterHeuristic::analyze(samples);
    };
    const auto check_generated_vm = [](const FilterRule& rule, FilterSample encrypted,
                                       const std::vector<std::uint8_t>& expected) {
        Xp3FilterVm vm;
        std::string error;
        check(vm.load(rule.to_tjs(), &error),
              "generated generalized filter did not compile: " + error);
        check(vm.decode(encrypted.hash, encrypted.offset, encrypted.bytes,
                        encrypted.filename, &error),
              "generated generalized filter did not execute: " + error);
        check(encrypted.bytes == expected,
              "generated generalized filter disagrees with native rule");
    };

    {
        std::vector<std::uint8_t> bytecode(512);
        bytecode[0] = 0xfe;
        bytecode[1] = 0xfe;
        bytecode[2] = 0x01;
        for (std::size_t i = 3; i < bytecode.size(); ++i)
            bytecode[i] = static_cast<std::uint8_t>(i * 37u);
        check(FilterHeuristic::score_plaintext("compiled.tjs", bytecode) >= 80,
              "valid compiled TJS was penalized as corrupt text");
        std::vector<FilterSample> scripts = {
            {0x01ebd086, 0, "first.tjs", bytecode},
            {0x06ece6b5, 0, "second.ks", bytecode},
        };
        FilterRule encryption{FilterOperation::XorHashShift3};
        for (auto& sample : scripts)
            encryption.apply(sample.hash, sample.offset, sample.bytes);
        const auto script_result = FilterHeuristic::analyze(scripts);
        check(script_result.rule.has_value() &&
              script_result.rule->operation == FilterOperation::XorHashShift3,
              "compiled-script hash-shift filter was not inferred: " +
                  script_result.reason);
    }
    {
        std::vector<std::uint8_t> bytecode(316);
        const std::array<std::uint8_t, 16> header = {
            'T', 'J', 'S', '2', '1', '0', '0', 0,
            0x3c, 0x01, 0x00, 0x00, 'D', 'A', 'T', 'A'};
        std::copy(header.begin(), header.end(), bytecode.begin());
        for (std::size_t i = header.size(); i < bytecode.size(); ++i)
            bytecode[i] = static_cast<std::uint8_t>(i * 73u + 19u);
        check(FilterHeuristic::score_plaintext("startup.tjs", bytecode) >= 150,
              "TJS2100 compiled bootstrap was not recognized as plaintext");
        bytecode[8] = 0xff;
        check(FilterHeuristic::score_plaintext("startup.tjs", bytecode) < 80,
              "structurally invalid TJS2100 container was accepted");
    }
    check(FilterHeuristic::score_plaintext(
              "movie.wmv", std::vector<std::uint8_t>{
                  0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
                  0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c}) >= 80,
          "ASF/WMV signature was not recognized");
    check(FilterHeuristic::score_plaintext(
              "model.psb", std::vector<std::uint8_t>{'P', 'S', 'B', 0}) >= 80,
          "PSB signature was not recognized");

    {
        // Printable-byte scoring must not invent a header-only boundary when
        // an unbounded rule already satisfies every exact format constraint.
        // The opaque ASD payload deliberately looks more text-like while it
        // is still encrypted, reproducing the general mixed-asset failure
        // mode without relying on any title-specific bytes or hashes.
        std::vector<std::uint8_t> png = {
            0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
            0, 0, 0, 13, 'I', 'H', 'D', 'R',
            0, 0, 5, 0, 0, 0, 3, 0, 8, 6, 0, 0, 0,
            0, 0, 0, 0,
        };
        std::vector<std::uint8_t> ogg(32, 0);
        std::copy_n("OggS", 4, ogg.begin());
        ogg[4] = 0;
        ogg[26] = 1;
        ogg[27] = 4;
        std::vector<std::uint8_t> wav(32, 0);
        std::copy_n("RIFF", 4, wav.begin());
        std::copy_n("WAVEfmt ", 8, wav.begin() + 8);
        std::vector<FilterSample> mixed = {
            {0x123456a5, 0, "image.png", std::move(png)},
            {0xabcdef3c, 0, "voice.ogg", std::move(ogg)},
            {0x73a91e62, 0, "effect.wav", std::move(wav)},
            {0x76543220, 0, "layout.asd", std::vector<std::uint8_t>(4096, 0x01)},
        };
        FilterRule encryption{FilterOperation::XorHash};
        for (auto& sample : mixed)
            encryption.apply(sample.hash, sample.offset, sample.bytes);
        const auto mixed_result = FilterHeuristic::analyze(mixed);
        check(mixed_result.rule.has_value(),
              "unbounded hash-XOR was lost to an unproven range: " +
                  mixed_result.reason);
        check(mixed_result.rule->operation == FilterOperation::XorHash &&
                  mixed_result.rule->start_offset == 0 &&
                  mixed_result.rule->end_offset ==
                      std::numeric_limits<std::uint64_t>::max(),
              "printable encrypted payload invented a filter boundary: " +
                  mixed_result.rule->name());
    }

    FilterRule shifted{FilterOperation::XorHashShiftConstant};
    shifted.shift = 11;
    shifted.constant = 0x6d;
    auto result = detect_xor(shifted);
    check(result.rule.has_value(), "shift/constant filter was not inferred: " + result.reason);
    check(result.rule->operation == FilterOperation::XorHashShiftConstant &&
          result.rule->shift == 11 && result.rule->constant == 0x6d,
          "shift/constant filter inference was incorrect");
    {
        auto sample = filter_known_plaintext_samples().front();
        const auto expected = sample.bytes;
        shifted.apply(sample.hash, sample.offset, sample.bytes);
        check_generated_vm(*result.rule, std::move(sample), expected);
    }

    FilterRule folded{FilterOperation::XorHashFoldConstant};
    folded.modulus = 0b1111;
    folded.constant = 0x91;
    result = detect_xor(folded);
    check(result.rule.has_value(), "hash-fold filter was not inferred: " + result.reason);
    check(result.rule->operation == FilterOperation::XorHashFoldConstant &&
          result.rule->modulus == 0b1111 && result.rule->constant == 0x91,
          "hash-fold filter inference was incorrect");

    FilterRule periodic{FilterOperation::XorPeriodic};
    periodic.table = {0x52, 0xa7, 0x19, 0xe3, 0x64};
    result = detect_xor(periodic);
    check(result.rule.has_value(), "periodic filter was not inferred: " + result.reason);
    check(result.rule->operation == FilterOperation::XorPeriodic &&
          result.rule->table == periodic.table,
          "periodic filter inference was incorrect");
    {
        auto sample = filter_known_plaintext_samples().front();
        const auto expected = sample.bytes;
        periodic.apply(sample.hash, sample.offset, sample.bytes);
        check_generated_vm(*result.rule, std::move(sample), expected);
    }

    FilterRule variable{FilterOperation::XorHashShiftByOffset};
    variable.modulus = 5;
    result = detect_xor(variable);
    check(result.rule.has_value(), "position-selected filter was not inferred: " + result.reason);
    check(result.rule->operation == FilterOperation::XorHashShiftByOffset &&
          result.rule->modulus == 5,
          "position-selected filter inference was incorrect");

    FilterRule lanes{FilterOperation::XorHashByteLanes};
    lanes.table = {16, 24, 0, 8};
    result = detect_xor(lanes);
    check(result.rule.has_value(), "hash-lane filter was not inferred: " + result.reason);
    check(result.rule->operation == FilterOperation::XorHashByteLanes &&
          result.rule->table == lanes.table,
          "hash-lane filter inference was incorrect");
    {
        auto sample = filter_known_plaintext_samples().front();
        const auto expected = sample.bytes;
        lanes.apply(sample.hash, sample.offset, sample.bytes);
        check_generated_vm(*result.rule, std::move(sample), expected);
    }

    FilterRule rotating{FilterOperation::XorRotatingHash};
    rotating.seed_xor = 0x2f91de55;
    rotating.modulus = 31;
    result = detect_xor(rotating);
    check(result.rule.has_value(), "rotating-hash filter was not inferred: " + result.reason);
    check(result.rule->operation == FilterOperation::XorRotatingHash &&
          result.rule->seed_xor == (rotating.seed_xor & 0x7fffffffU) &&
          result.rule->modulus == 31,
          "rotating-hash filter inference was incorrect");
    {
        auto sample = filter_known_plaintext_samples().front();
        const auto expected = sample.bytes;
        rotating.apply(sample.hash, sample.offset, sample.bytes);
        check_generated_vm(*result.rule, std::move(sample), expected);
    }

    FilterRule lcg{FilterOperation::XorLcgHash};
    result = detect_xor(lcg);
    check(result.rule.has_value(), "LCG filter was not inferred: " + result.reason);
    check(result.rule->operation == FilterOperation::XorLcgHash,
          "LCG filter inference was incorrect");
    {
        auto sample = filter_known_plaintext_samples().front();
        const auto expected = sample.bytes;
        lcg.apply(sample.hash, sample.offset, sample.bytes);
        check_generated_vm(*result.rule, std::move(sample), expected);
    }

    {
        auto samples = filter_known_plaintext_samples();
        FilterRule decrypt{FilterOperation::XorThenAdd};
        decrypt.constant = 0xa9;
        decrypt.post_add = 0x37;
        for (auto& sample : samples) {
            for (auto& byte : sample.bytes)
                byte = static_cast<std::uint8_t>(byte - decrypt.post_add) ^
                       decrypt.constant;
        }
        result = FilterHeuristic::analyze(samples);
        check(result.rule.has_value() && result.rule->operation ==
                  FilterOperation::XorThenAdd,
              "XOR/add post-transform was not inferred: " + result.reason);
        const auto expected = filter_known_plaintext_samples().front().bytes;
        check_generated_vm(*result.rule, std::move(samples.front()), expected);
    }

    {
        auto samples = filter_known_plaintext_samples();
        FilterRule decrypt{FilterOperation::XorThenNibbleSwap};
        decrypt.constant = 0x5c;
        for (auto& sample : samples) {
            for (auto& byte : sample.bytes) {
                const auto unswapped = static_cast<std::uint8_t>(
                    (byte >> 4) | (byte << 4));
                byte = unswapped ^ decrypt.constant;
            }
        }
        result = FilterHeuristic::analyze(samples);
        check(result.rule.has_value() && result.rule->operation ==
                  FilterOperation::XorThenNibbleSwap &&
                  result.rule->constant == 0x5c,
              "nibble-swap post-transform was not inferred: " + result.reason);
        const auto expected = filter_known_plaintext_samples().front().bytes;
        check_generated_vm(*result.rule, std::move(samples.front()), expected);
    }

    {
        auto samples = filter_known_plaintext_samples();
        const auto inverse_rotate = [](std::uint8_t plain) {
            for (unsigned candidate = 0; candidate < 256; ++candidate) {
                const auto value = static_cast<std::uint8_t>(candidate);
                const auto amount = std::popcount(value) & 7u;
                const auto decoded = amount ? static_cast<std::uint8_t>(
                    (value << amount) | (value >> (8 - amount))) : value;
                if (decoded == plain) return value;
            }
            throw std::runtime_error("popcount rotation is not invertible");
        };
        for (auto& sample : samples)
            for (auto& byte : sample.bytes) byte = inverse_rotate(byte);
        result = FilterHeuristic::analyze(samples);
        check(result.rule.has_value() && result.rule->operation ==
                  FilterOperation::RotateLeftByPopcount,
              "popcount rotation was not inferred: " + result.reason);
        const auto expected = filter_known_plaintext_samples().front().bytes;
        check_generated_vm(*result.rule, std::move(samples.front()), expected);
    }

    FilterRule skipped{FilterOperation::XorHashShift};
    skipped.shift = 12;
    skipped.start_offset = 5;
    result = detect_xor(skipped);
    check(result.rule.has_value(), "prefix-skipping filter was not inferred: " + result.reason);
    check(result.rule->start_offset == 5,
          "prefix-skipping filter inferred the wrong boundary");
    {
        auto sample = filter_known_plaintext_samples().front();
        const auto expected = sample.bytes;
        skipped.apply(sample.hash, sample.offset, sample.bytes);
        check_generated_vm(*result.rule, std::move(sample), expected);
    }

    FilterRule header_only{FilterOperation::XorHashShift};
    header_only.shift = 9;
    header_only.end_offset = 13;
    result = detect_xor(header_only);
    check(result.rule.has_value(),
          "header-only filter was not inferred: " + result.reason);
    check(result.rule->operation == FilterOperation::XorHashShift &&
              result.rule->shift == 9 && result.rule->end_offset == 13,
          "header-only filter inferred the wrong rule or boundary");
    {
        auto sample = filter_known_plaintext_samples().front();
        const auto expected = sample.bytes;
        header_only.apply(sample.hash, sample.offset, sample.bytes);
        check_generated_vm(*result.rule, std::move(sample), expected);
    }

    FilterRule constant_after_prefix{FilterOperation::XorConstant};
    constant_after_prefix.constant = 0x67;
    constant_after_prefix.start_offset = 6;
    result = detect_xor(constant_after_prefix);
    check(result.rule.has_value() && result.rule->operation ==
              FilterOperation::XorConstant && result.rule->constant == 0x67 &&
              result.rule->start_offset == 6,
          "range-limited constant filter was not inferred: " + result.reason);

    {
        auto base = filter_known_plaintext_samples();
        std::vector<FilterSample> branched = {base[0], base[0], base[1], base[1]};
        branched[1].hash ^= 0x6194a23d;
        branched[1].filename = "second.png";
        branched[3].hash ^= 0x34d9c10e;
        branched[3].filename = "second.ogg";
        FilterRule png_rule{FilterOperation::XorConstant};
        png_rule.constant = 0x41;
        FilterRule ogg_rule{FilterOperation::XorConstant};
        ogg_rule.constant = 0xb7;
        for (auto& sample : branched) {
            const auto& encryption = sample.filename.ends_with(".png") ? png_rule : ogg_rule;
            encryption.apply(sample.hash, sample.offset, sample.bytes);
        }
        result = FilterHeuristic::analyze(branched);
        check(result.rule.has_value() && result.rule->branches.size() == 2,
              "extension-conditioned filter was not inferred: " + result.reason);
        Xp3FilterVm vm;
        std::string error;
        check(vm.load(result.rule->to_tjs(), &error),
              "extension-conditioned generated filter did not compile: " + error);
        for (std::size_t i = 0; i < branched.size(); ++i) {
            check(vm.decode(branched[i].hash, branched[i].offset, branched[i].bytes,
                            branched[i].filename, &error),
                  "extension-conditioned generated filter did not execute: " + error);
            const auto expected = base[i < 2 ? 0 : 1].bytes;
            check(branched[i].bytes == expected,
                  "extension-conditioned generated filter produced wrong bytes");
        }
    }


    {
        auto base = filter_known_plaintext_samples();
        std::vector<FilterSample> branched = {base[0], base[0], base[0], base[0]};
        branched[0].filename = "System/first.png";
        branched[1].filename = "system/second.png";
        branched[2].filename = "Image/first.png";
        branched[3].filename = "image/second.png";
        branched[1].hash ^= 0x6194a23d;
        branched[2].hash ^= 0x34d9c10e;
        branched[3].hash ^= 0x7d31b842;
        FilterRule system_rule{FilterOperation::XorConstant};
        system_rule.constant = 0x23;
        FilterRule image_rule{FilterOperation::XorConstant};
        image_rule.constant = 0xc1;
        for (auto& sample : branched) {
            const auto& encryption = sample.filename.starts_with("System/") ||
                                     sample.filename.starts_with("system/")
                ? system_rule : image_rule;
            encryption.apply(sample.hash, sample.offset, sample.bytes);
        }
        result = FilterHeuristic::analyze(branched);
        check(result.rule.has_value() && result.rule->branches.size() == 2 &&
                  !result.rule->branches.front().path_prefixes.empty(),
              "path-conditioned filter was not inferred: " + result.reason);
        Xp3FilterVm vm;
        std::string error;
        check(vm.load(result.rule->to_tjs(), &error),
              "path-conditioned generated filter did not compile: " + error);
        for (auto& sample : branched) {
            check(vm.decode(sample.hash, sample.offset, sample.bytes,
                            sample.filename, &error),
                  "path-conditioned generated filter did not execute: " + error);
            check(sample.bytes == base[0].bytes,
                  "path-conditioned generated filter produced wrong bytes");
        }
    }

    auto opaque = filter_known_plaintext_samples();
    for (auto& sample : opaque)
        for (std::size_t i = 0; i < sample.bytes.size(); ++i)
            sample.bytes[i] ^= static_cast<std::uint8_t>(sample.hash * 37u + i * i * 19u);
    result = FilterHeuristic::analyze(opaque);
    check(!result.rule.has_value() &&
          result.disposition == FilterInferenceDisposition::RequiresExecutableAnalysis,
          "opaque archive transform was not deferred to executable analysis");

    {
        auto dictionary = filter_known_plaintext_samples();
        for (std::size_t i = 0; i < dictionary.size(); ++i) {
            FilterRule per_file{FilterOperation::XorConstant};
            per_file.constant = static_cast<std::uint8_t>(0x31 + i * 0x29);
            per_file.apply(dictionary[i].hash, dictionary[i].offset,
                           dictionary[i].bytes);
        }
        const auto dictionary_result = FilterHeuristic::analyze(dictionary);
        check(!dictionary_result.rule.has_value() &&
              dictionary_result.disposition ==
                  FilterInferenceDisposition::RequiresExecutableAnalysis &&
              dictionary_result.reason.find("per-hash mapping") != std::string::npos,
              "per-file key dictionary was not diagnosed as executable state");
    }
}

void test_yuri_filter_vm() {
    Xp3FilterVm vm;
    std::string error;
    check(vm.load("Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,h);});",
                  &error), "Yuri TJS VM rejected hash-XOR filter");
    const std::uint32_t hash = 0x123456a5;
    std::vector<std::uint8_t> bytes = {0x89, 'P', 'N', 'G'};
    for (auto& byte : bytes) byte ^= static_cast<std::uint8_t>(hash);
    check(vm.decode(hash, 0, bytes, "test.png", &error), "Yuri filter execution failed");
    check(bytes == std::vector<std::uint8_t>({0x89, 'P', 'N', 'G'}),
          "Yuri filter produced the wrong bytes");
}

int execute_system_app_id_guard(std::string_view guard,
                                std::string_view app_id,
                                std::string_view explicit_check_id = {}) {
    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        engine(new TJS::tTJS(), release_tjs);
    engine->ExecScript(TJS::ttstr(R"TJS(
global.krkrvita_exit_calls = 0;
class KrkrvitaTestSystem
{
    function exit() { ++global.krkrvita_exit_calls; }
}
var System = new KrkrvitaTestSystem();
)TJS"));
    check(install_system_app_id_compat(*engine),
          "System.checkAppId compatibility could not be installed");

    std::string setup;
    if (!explicit_check_id.empty()) {
        setup += "System.checkAppId = \"" +
                 std::string(explicit_check_id) + "\";\n";
    }
    setup += "global.APP_ID = \"" + std::string(app_id) + "\";\n";
    engine->ExecScript(TJS::ttstr(setup + std::string(guard)));
    TJS::tTJSVariant exits;
    engine->EvalExpression(TJS_W("global.krkrvita_exit_calls"), &exits);
    return static_cast<int>(exits.AsInteger());
}

void test_system_app_id_compat() {
    constexpr std::string_view guard = R"TJS(
if(System.checkAppId != global.APP_ID){
    System.exit();
}
)TJS";
    constexpr std::string_view app_id =
        "ee392b10-36f6-4ab1-a5d7-941bbfec32ab";
    check(execute_system_app_id_guard(guard, app_id) == 0,
          "lazy System.checkAppId did not follow global.APP_ID");
    check(execute_system_app_id_guard(guard, app_id, app_id) == 0,
          "matching explicit System.checkAppId override was rejected");
    check(execute_system_app_id_guard(
              guard, app_id, "d54d77cb-17e4-4d1f-8506-217b7cec0561") == 1,
          "mismatched explicit System.checkAppId override was ignored");
}

void test_sfo() {
    const auto bytes = ParamSfo::bubble("Test Game", "KRVG12345").encode();
    check(bytes.size() > 128, "SFO unexpectedly small");
    check(bytes[0] == 0 && bytes[1] == 'P' && bytes[2] == 'S' && bytes[3] == 'F',
          "SFO magic failed");
    check(std::search(bytes.begin(), bytes.end(), "KRVG12345", "KRVG12345" + 9) != bytes.end(),
          "SFO title ID missing");
}

void test_text_codec() {
    // Mode 1 swaps adjacent bits of every UTF-16 code unit.
    std::vector<std::uint8_t> encoded = {0xfe, 0xfe, 1, 0xff, 0xfe};
    for (const std::uint16_t plain : {std::uint16_t('A'), std::uint16_t(0x3042)}) {
        const auto cipher = static_cast<std::uint16_t>(((plain & 0xaaaa) >> 1) |
                                                       ((plain & 0x5555) << 1));
        encoded.push_back(static_cast<std::uint8_t>(cipher));
        encoded.push_back(static_cast<std::uint8_t>(cipher >> 8));
    }
    std::string decoded;
    check(decode_kirikiri_text(encoded, decoded), "mode-1 text decode failed");
    check(decoded == "A\xe3\x81\x82", "mode-1 text decoded incorrectly");
}

void test_loose_storage() {
    const auto root = std::filesystem::temp_directory_path() / "krkrvita-storage-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "game/system");
    std::filesystem::create_directories(root / "patch");
    {
        std::ofstream(root / "game/system/Test.tjs", std::ios::binary) << "game";
        std::ofstream(root / "game/loose.bin", std::ios::binary) << "data";
        std::ofstream(root / "patch/loose.bin", std::ios::binary) << "patch";
    }
    GameProfile profile;
    profile.game_id = "test";
    profile.game_path = root / "game";
    profile.patch_root = root / "patch";
    std::string error;
    auto storage = GameStorage::mount(profile, &error);
    check(storage.has_value(), "loose storage mount failed");
    storage->add_auto_path("system/");
    check(storage->exists("test.tjs"), "storage auto path failed");
    const auto script = storage->read_script("TEST.TJS", &error);
    check(script && *script == "game", "case-insensitive script read failed");
    const auto override = storage->read("loose.bin", 32, &error);
    check(override && std::string(override->begin(), override->end()) == "patch",
          "patch loose file did not override game file");
    check(!storage->read("../outside", 32, &error), "storage traversal was accepted");
    std::filesystem::remove_all(root, ec);
}

class MemoryBinaryStream final : public TJS::tTJSBinaryStream {
public:
    tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) override {
        tjs_int64 base = 0;
        if (whence == TJS_BS_SEEK_CUR) base = static_cast<tjs_int64>(position_);
        else if (whence == TJS_BS_SEEK_END) base = static_cast<tjs_int64>(bytes_.size());
        const tjs_int64 next = base + offset;
        if (next < 0) throw std::runtime_error("negative TJS bytecode seek");
        position_ = static_cast<std::size_t>(next);
        return position_;
    }

    tjs_uint TJS_INTF_METHOD Read(void* buffer, tjs_uint size) override {
        const std::size_t available = position_ < bytes_.size()
            ? bytes_.size() - position_ : 0;
        const std::size_t amount = std::min<std::size_t>(size, available);
        if (amount) std::memcpy(buffer, bytes_.data() + position_, amount);
        position_ += amount;
        return static_cast<tjs_uint>(amount);
    }

    tjs_uint TJS_INTF_METHOD Write(const void* buffer, tjs_uint size) override {
        if (size > std::numeric_limits<std::size_t>::max() - position_)
            throw std::runtime_error("TJS bytecode output overflow");
        const std::size_t end = position_ + size;
        if (end > bytes_.size()) bytes_.resize(end);
        if (size) std::memcpy(bytes_.data() + position_, buffer, size);
        position_ = end;
        return size;
    }

    void TJS_INTF_METHOD SetEndOfStorage() override { bytes_.resize(position_); }
    tjs_uint64 TJS_INTF_METHOD GetSize() override { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

void test_yuri_compiler_lifecycle() {
    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        compiler(new TJS::tTJS(), release_tjs);
    MemoryBinaryStream bytecode;
    compiler->CompileScript(TJS_W("var empty = \"\";"), &bytecode,
                            false, false, false, TJS_W("lifecycle.tjs"), 0);
    check(bytecode.GetSize() > 0,
          "Yuri compiler lifecycle probe emitted no bytecode");

    // Dictionary and Array literals are ordinary dispatch objects in the
    // compiler's constant table, not tTJSInterCodeContext instances.  Export
    // both forms so sanitizer builds enforce type-safe classification.
    MemoryBinaryStream object_constants;
    compiler->CompileScript(
        TJS_W("var dictionary = %[\"key\" => 1]; "
              "var array = [dictionary, 2];"),
        &object_constants, false, false, false,
        TJS_W("object-constants.tjs"), 0);
    check(object_constants.GetSize() > 0,
          "Yuri compiler emitted no object-constant bytecode");
}

void test_empty_yuri_string_properties() {
    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        engine(new TJS::tTJS(), release_tjs);

    TJS::tTJSVariant result;
    engine->EvalExpression(TJS_W("\"\".length"), &result);
    check(result.Type() == tvtInteger && result.AsInteger() == 0,
          "empty TJS string .length did not evaluate to zero");

    engine->EvalExpression(TJS_W("\"\"[0]"), &result);
    check(result.Type() == tvtString &&
              TJS::ttstr(result).AsStdString().empty(),
          "empty TJS string numeric property did not evaluate to empty");
}

void test_yuri_shift_semantics() {
    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        engine(new TJS::tTJS(), release_tjs);

    struct ShiftCase {
        std::string_view name;
        const tjs_char* folded;
        const tjs_char* runtime;
        const tjs_char* compound;
        tTVInteger expected;
    };
    const auto integer_min = std::numeric_limits<tTVInteger>::min();
    const auto integer_max = std::numeric_limits<tTVInteger>::max();
    const std::array cases = {
        ShiftCase{"phase1-left-wrap",
            TJS_W("2221053586956454128<<23"),
            TJS_W("(function(v,n){return v<<n;})(2221053586956454128,23)"),
            TJS_W("(function(v,n){v<<=n;return v;})(2221053586956454128,23)"),
            tTVInteger{4332131632845684736LL}},
        ShiftCase{"left-63", TJS_W("1<<63"),
            TJS_W("(function(v,n){return v<<n;})(1,63)"),
            TJS_W("(function(v,n){v<<=n;return v;})(1,63)"), integer_min},
        ShiftCase{"left-64", TJS_W("1<<64"),
            TJS_W("(function(v,n){return v<<n;})(1,64)"),
            TJS_W("(function(v,n){v<<=n;return v;})(1,64)"), tTVInteger{1}},
        ShiftCase{"left-65", TJS_W("1<<65"),
            TJS_W("(function(v,n){return v<<n;})(1,65)"),
            TJS_W("(function(v,n){v<<=n;return v;})(1,65)"), tTVInteger{2}},
        ShiftCase{"left-negative-1", TJS_W("1<<(-1)"),
            TJS_W("(function(v,n){return v<<n;})(1,-1)"),
            TJS_W("(function(v,n){v<<=n;return v;})(1,-1)"), integer_min},
        ShiftCase{"left-negative-65", TJS_W("1<<(-65)"),
            TJS_W("(function(v,n){return v<<n;})(1,-65)"),
            TJS_W("(function(v,n){v<<=n;return v;})(1,-65)"), integer_min},
        ShiftCase{"arithmetic-right-negative", TJS_W("(-2)>>1"),
            TJS_W("(function(v,n){return v>>n;})(-2,1)"),
            TJS_W("(function(v,n){v>>=n;return v;})(-2,1)"), tTVInteger{-1}},
        ShiftCase{"arithmetic-right-64", TJS_W("(-1)>>64"),
            TJS_W("(function(v,n){return v>>n;})(-1,64)"),
            TJS_W("(function(v,n){v>>=n;return v;})(-1,64)"), tTVInteger{-1}},
        ShiftCase{"arithmetic-right-65", TJS_W("(-8)>>65"),
            TJS_W("(function(v,n){return v>>n;})(-8,65)"),
            TJS_W("(function(v,n){v>>=n;return v;})(-8,65)"), tTVInteger{-4}},
        ShiftCase{"arithmetic-right-negative-1", TJS_W("(-1)>>(-1)"),
            TJS_W("(function(v,n){return v>>n;})(-1,-1)"),
            TJS_W("(function(v,n){v>>=n;return v;})(-1,-1)"), tTVInteger{-1}},
        ShiftCase{"arithmetic-right-negative-65", TJS_W("8>>(-65)"),
            TJS_W("(function(v,n){return v>>n;})(8,-65)"),
            TJS_W("(function(v,n){v>>=n;return v;})(8,-65)"), tTVInteger{0}},
        ShiftCase{"logical-right-negative", TJS_W("(-1)>>>1"),
            TJS_W("(function(v,n){return v>>>n;})(-1,1)"),
            TJS_W("(function(v,n){v>>>=n;return v;})(-1,1)"), integer_max},
        ShiftCase{"logical-right-63", TJS_W("(-1)>>>63"),
            TJS_W("(function(v,n){return v>>>n;})(-1,63)"),
            TJS_W("(function(v,n){v>>>=n;return v;})(-1,63)"), tTVInteger{1}},
        ShiftCase{"logical-right-64", TJS_W("(-1)>>>64"),
            TJS_W("(function(v,n){return v>>>n;})(-1,64)"),
            TJS_W("(function(v,n){v>>>=n;return v;})(-1,64)"), tTVInteger{-1}},
        ShiftCase{"logical-right-65", TJS_W("(-1)>>>65"),
            TJS_W("(function(v,n){return v>>>n;})(-1,65)"),
            TJS_W("(function(v,n){v>>>=n;return v;})(-1,65)"), integer_max},
        ShiftCase{"logical-right-negative-1", TJS_W("(-1)>>>(-1)"),
            TJS_W("(function(v,n){return v>>>n;})(-1,-1)"),
            TJS_W("(function(v,n){v>>>=n;return v;})(-1,-1)"), tTVInteger{1}},
        ShiftCase{"logical-right-negative-65", TJS_W("(-1)>>>(-65)"),
            TJS_W("(function(v,n){return v>>>n;})(-1,-65)"),
            TJS_W("(function(v,n){v>>>=n;return v;})(-1,-65)"), tTVInteger{1}},
    };

    const auto evaluate = [&](const tjs_char* expression,
                              std::string_view name,
                              std::string_view path) {
        TJS::tTJSVariant result;
        engine->EvalExpression(expression, &result);
        check(result.Type() == tvtInteger,
              std::string(name) + " " + std::string(path) +
                  " shift result was not an integer");
        return result.AsInteger();
    };
    for (const auto& test : cases) {
        check(evaluate(test.folded, test.name, "folded") == test.expected,
              std::string(test.name) + " folded shift result changed");
        check(evaluate(test.runtime, test.name, "runtime") == test.expected,
              std::string(test.name) + " runtime shift result changed");
        check(evaluate(test.compound, test.name, "compound") == test.expected,
              std::string(test.name) + " compound shift result changed");
    }
}

std::filesystem::path manifest_reference_path(
    const std::filesystem::path& snapshot, std::string reference) {
    constexpr std::string_view shared_prefix = "../patch/";
    if (reference.rfind(shared_prefix, 0) == 0)
        reference.erase(0, shared_prefix.size());
    return snapshot / "patch" / std::filesystem::path(reference);
}

bool script_extension(std::string_view name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos) return false;
    std::string extension(name.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension == ".tjs";
}

bool scenario_extension(std::string_view name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos) return false;
    std::string extension(name.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension == ".ks";
}

std::string normalized_plugin_name(std::string name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return name;
}

void collect_literal_plugin_links(std::string_view source,
                                  std::set<std::string>& plugins) {
    constexpr std::string_view marker = "Plugins.link";
    std::size_t cursor = 0;
    while ((cursor = source.find(marker, cursor)) != std::string_view::npos) {
        cursor += marker.size();
        while (cursor < source.size() &&
               std::isspace(static_cast<unsigned char>(source[cursor])))
            ++cursor;
        if (cursor >= source.size() || source[cursor++] != '(') continue;
        while (cursor < source.size() &&
               std::isspace(static_cast<unsigned char>(source[cursor])))
            ++cursor;
        if (cursor >= source.size() ||
            (source[cursor] != '"' && source[cursor] != '\''))
            continue;
        const char quote = source[cursor++];
        const auto end = source.find(quote, cursor);
        if (end == std::string_view::npos) continue;
        plugins.insert(normalized_plugin_name(
            std::string(source.substr(cursor, end - cursor))));
        cursor = end + 1;
    }
}

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "cannot hash fixture file " + path.string());
    Sha256 digest;
    std::array<char, 1024 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        if (count > 0)
            digest.update(buffer.data(), static_cast<std::size_t>(count));
    }
    check(input.eof(), "I/O error while hashing fixture file " + path.string());
    return Sha256::hex(digest.finish());
}

void compile_tjs(TJS::tTJS& engine, std::string_view source,
                 std::string_view name) {
    MemoryBinaryStream bytecode;
    const TJS::ttstr script{std::string(source)};
    const TJS::ttstr script_name{std::string(name)};
    engine.CompileScript(script.c_str(), &bytecode, false, false, false,
                         script_name.c_str(), 0);
    check(bytecode.GetSize() > 0,
          "Yuri compiler emitted no bytecode for " + std::string(name));
}

struct RetailAfterStartupProbe {
    bool has_timer = false;
    bool timer_enabled = false;
    std::string notice;
};

RetailAfterStartupProbe execute_retail_after_startup_probe(
    std::string_view resource, std::string_view setup, bool has_timer) {
    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        engine(new TJS::tTJS(), release_tjs);
    const std::string harness = R"TJS(
global.krkrvita_after_startup_notice = "";
class KrkrvitaTestDebug
{
    function notice(message)
    {
        global.krkrvita_after_startup_notice = message;
    }
}
var Debug = new KrkrvitaTestDebug();
)TJS" + std::string(setup);
    engine->ExecScript(TJS::ttstr(harness));
    engine->ExecScript(TJS::ttstr(std::string(resource)));

    RetailAfterStartupProbe probe;
    probe.has_timer = has_timer;
    if (has_timer) {
        TJS::tTJSVariant enabled;
        engine->EvalExpression(TJS_W("global.kag.menutimer.enabled"), &enabled);
        probe.timer_enabled = enabled.AsInteger() != 0;
    }
    TJS::tTJSVariant notice;
    engine->EvalExpression(
        TJS_W("global.krkrvita_after_startup_notice"), &notice);
    probe.notice = TJS::ttstr(notice).AsStdString();
    return probe;
}

void test_retail_after_startup_resource() {
    const std::filesystem::path resource_path =
        std::filesystem::path(KRKRVITA_SOURCE_DIR) /
        "resources/vita/retail-after-startup.tjs";
    std::ifstream input(resource_path, std::ios::binary);
    check(static_cast<bool>(input),
          "cannot open Vita retail post-startup resource");
    const std::string resource((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());

    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        compiler(new TJS::tTJS(), release_tjs);
    compile_tjs(*compiler, resource, "retail-after-startup.tjs");

    constexpr std::string_view matching_window = R"TJS(
class KrkrzAddMainWindow
{
    var menutimer;
    function KrkrzAddMainWindow()
    {
        menutimer = %[ enabled:true ];
    }
}
global.kag = new KrkrzAddMainWindow();
)TJS";
    const auto setup = [](std::string_view platform, std::string_view app_id,
                          std::string_view window) {
        return "var System = %[ platformName:\"" + std::string(platform) +
            "\", checkAppId:\"" + std::string(app_id) + "\" ];\n" +
            std::string(window);
    };
    constexpr std::string_view target_id =
        "c1ce5e17-e483-4195-a8f7-a019f96c6805";

    const auto positive = execute_retail_after_startup_probe(
        resource, setup("PlayStation Vita", target_id, matching_window), true);
    check(!positive.timer_enabled,
          "Vita target menubar timer remained enabled after startup");
    check(positive.notice == "krkrvita-vita-menubar-timer-disabled",
          "Vita target menubar timer disable was not observable");

    const auto windows = execute_retail_after_startup_probe(
        resource, setup("Win32", target_id, matching_window), true);
    check(windows.timer_enabled && windows.notice.empty(),
          "post-startup resource changed the Windows timer");

    const auto wrong_id = execute_retail_after_startup_probe(
        resource,
        setup("PlayStation Vita", "not-the-target-title", matching_window), true);
    check(wrong_id.timer_enabled && wrong_id.notice.empty(),
          "post-startup resource changed another Vita title's timer");

    const auto missing_id = execute_retail_after_startup_probe(
        resource,
        "var System = %[ platformName:\"PlayStation Vita\" ];\n" +
            std::string(matching_window),
        true);
    check(missing_id.timer_enabled && missing_id.notice.empty(),
          "post-startup resource did not tolerate a missing application ID");

    constexpr std::string_view other_window = R"TJS(
class OtherWindow
{
    var menutimer;
    function OtherWindow()
    {
        menutimer = %[ enabled:true ];
    }
}
global.kag = new OtherWindow();
)TJS";
    const auto wrong_class = execute_retail_after_startup_probe(
        resource, setup("PlayStation Vita", target_id, other_window), true);
    check(wrong_class.timer_enabled && wrong_class.notice.empty(),
          "post-startup resource changed a non-target Vita window");

    const auto no_window = execute_retail_after_startup_probe(
        resource, setup("PlayStation Vita", target_id, ""), false);
    check(no_window.notice.empty(),
          "post-startup resource did not tolerate a missing game window");
}

void execute_sample_patch(TJS::tTJS& engine, std::string_view source) {
    TJS::iTJSDispatch2* system = TJS::TJSCreateCustomObject();
    TJS::tTJSVariant system_value(system);
    system->Release();
    check(TJS_SUCCEEDED(engine.GetGlobalNoAddRef()->PropSet(
              TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_W("System"), nullptr,
              &system_value, engine.GetGlobalNoAddRef())),
          "cannot install System object for retail patch execution");
    check(install_system_app_id_compat(engine),
          "cannot install System.checkAppId before retail patch execution");
    engine.ExecScript(TJS::ttstr(std::string(source)));
    TJS::tTJSVariant app_id;
    check(TJS_SUCCEEDED(system->PropGet(0, TJS_W("checkAppId"), nullptr,
                                       &app_id, system)),
          "retail patch did not assign System.checkAppId");
    check(TJS::ttstr(app_id).AsStdString() ==
              "c1ce5e17-e483-4195-a8f7-a019f96c6805",
          "retail patch assigned the wrong application ID");
}

void decode_retail_png(const Xp3Archive& archive, Xp3FilterVm& filter,
                       std::string_view name, png_uint_32 expected_width,
                       png_uint_32 expected_height) {
    const Xp3Entry* entry = archive.find(name);
    check(entry, "retail PNG is missing: " + std::string(name));
    std::string error;
    const auto bytes = archive.read(*entry, 8u * 1024u * 1024u,
                                    &filter, &error);
    check(bytes.has_value(),
          "cannot decrypt retail PNG " + std::string(name) + ": " + error);
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    check(png_image_begin_read_from_memory(&image, bytes->data(), bytes->size()),
          "libpng rejected decrypted retail PNG " + std::string(name));
    check(image.width == expected_width && image.height == expected_height,
          "retail PNG dimensions changed for " + std::string(name));
    image.format = PNG_FORMAT_RGBA;
    std::vector<png_byte> pixels(PNG_IMAGE_SIZE(image));
    const bool decoded = png_image_finish_read(
        &image, nullptr, pixels.data(), 0, nullptr) != 0;
    png_image_free(&image);
    check(decoded && !pixels.empty(),
          "libpng could not decode retail PNG pixels for " +
              std::string(name));
}

void test_retail_sample(const std::filesystem::path& game_path,
                        const std::filesystem::path& patch_snapshot) {
    const auto manifest_path = patch_snapshot / "alldata.js";
    check(std::filesystem::is_directory(game_path),
          "retail sample directory is missing");
    check(std::filesystem::is_regular_file(manifest_path),
          "retail patch manifest is missing");

    const auto game = GameScanner::scan(game_path);
    check(game.fingerprint ==
              "44bb539bf9510882b2fe2a5617614a3174e06ec7b2f1ae529251344aacd6bad8",
          "retail sample fingerprint changed");
    check(game.archives.size() == 3, "retail sample archive inventory changed");
    const auto archive_only = GameScanner::scan(
        game_path, GameScanMode::ArchivesOnly);
    check(archive_only.executable.empty() && archive_only.plugins.empty() &&
              archive_only.pe.product_name.empty(),
          "archive-only scan crossed the executable-analysis boundary");
    check(archive_only.archives.size() == game.archives.size(),
          "archive-only scan changed the XP3 inventory");

    const std::map<std::string, std::string> expected_archive_hashes = {
        {"data.xp3", "651eaf966ea61f376169e8531903ff51c6a40083de7faae6cb0e1e101a7313dd"},
        {"events.xp3", "7959b63c27c2df8bc80432ab67fcdff56ce71e32e7c8e04ac40c7885ff184f4c"},
        {"voice.xp3", "52ab28864a3477e1762ee0014451d6b0faa61197bb33f048e51a3d6d96891eaa"},
    };
    for (const auto& archive : game.archives) {
        const auto expected = expected_archive_hashes.find(archive.name);
        check(expected != expected_archive_hashes.end(),
              "unexpected retail XP3 " + archive.name);
        check(sha256_file(archive.path) == expected->second,
              "retail XP3 content hash changed for " + archive.name);
    }

    const auto manifest = PatchManifest::load(manifest_path);
    const auto resolution = PatchResolver::resolve(game, manifest);
    check(resolution.automatic && resolution.best(),
          "retail sample patch did not resolve automatically");
    check(resolution.best()->entry->canonical_title == "色情教団",
          "retail sample resolved to the wrong patch");

    std::filesystem::path filter_path;
    std::filesystem::path patch_path;
    for (const auto& reference : resolution.best()->entry->files) {
        const auto path = manifest_reference_path(patch_snapshot, reference);
        if (path.filename() == "xp3filter.tjs") filter_path = path;
        if (path.filename() == "patch.tjs") patch_path = path;
    }
    check(std::filesystem::is_regular_file(filter_path),
          "retail sample xp3filter.tjs is missing");
    check(std::filesystem::is_regular_file(patch_path),
          "retail sample patch.tjs is missing");
    check(sha256_file(filter_path) ==
              "6f2e085d965c3748f2fd7f626ab68db60990a9b85b8eb4053de718c6e6bfe734",
          "pinned retail xp3filter.tjs content changed");
    check(sha256_file(patch_path) ==
              "28fd4f3b59421c9d96af96897db164ef5193ec7e19d64b02956439def525e9a8",
          "pinned retail patch.tjs content changed");

    // This loose x86 TPM is the original Windows XP3 extraction filter, not a
    // Vita voice decoder. Kirikiroid replaces it with xp3filter.tjs and does
    // not execute the PE module during its automatic plug-in scan.
    const auto windows_filter = game_path / "voice.tpm";
    check(std::filesystem::is_regular_file(windows_filter),
          "retail Windows XP3 filter module is missing");
    check(sha256_file(windows_filter) ==
              "ad217aa3df2234772e179863feaaca30626131d17f50c71d2d354f9114b54f5a",
          "retail Windows XP3 filter module changed");
    std::ifstream windows_filter_input(windows_filter, std::ios::binary);
    const std::vector<std::uint8_t> windows_filter_bytes{
        std::istreambuf_iterator<char>(windows_filter_input),
        std::istreambuf_iterator<char>()};
    const auto contains_ascii = [&windows_filter_bytes](std::string_view text) {
        return std::search(windows_filter_bytes.begin(), windows_filter_bytes.end(),
                           text.begin(), text.end()) != windows_filter_bytes.end();
    };
    check(windows_filter_bytes.size() > 0x40 &&
              windows_filter_bytes[0] == 'M' && windows_filter_bytes[1] == 'Z',
          "voice.tpm is no longer a Windows PE module");
    check(contains_ascii("xp3dec.tpm") && contains_ascii("V2Link") &&
              contains_ascii("TVPSetXP3ArchiveExtractionFilter"),
          "voice.tpm is no longer identifiable as the replaced XP3 filter");

    std::ifstream filter_input(filter_path, std::ios::binary);
    const std::string filter_source((std::istreambuf_iterator<char>(filter_input)),
                                    std::istreambuf_iterator<char>());
    Xp3FilterVm filter;
    std::string error;
    check(filter.load(filter_source, &error),
          "retail sample filter does not execute in Yuri TJS: " + error);

    std::size_t recognized = 0;
    std::size_t sampled = 0;
    std::size_t compiled_scripts = 0;
    std::size_t decoded_scenarios = 0;
    std::map<std::string, std::string> script_sources;
    std::map<std::string, std::string> scenario_sources;
    std::set<std::string> plugin_requests;
    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        compiler(new TJS::tTJS(), release_tjs);
    for (const auto& archive_file : game.archives) {
        auto archive = Xp3Archive::open(archive_file.path, &error);
        check(archive.has_value(), "cannot open retail XP3: " + error);
        const std::map<std::string, std::size_t> expected_entry_counts = {
            {"data.xp3", 1045}, {"events.xp3", 407}, {"voice.xp3", 3864},
        };
        check(archive->entries().size() == expected_entry_counts.at(archive_file.name),
              "retail XP3 entry inventory changed for " + archive_file.name);
        if (archive_file.name == "data.xp3") {
            // This is the first image loaded while constructing the sample's
            // CustomHistoryLayer. Keep the exact hardware-crash asset in the
            // gate so an XP3/PVR fast-path regression cannot masquerade as a
            // successful script-only compatibility test.
            decode_retail_png(*archive, filter,
                              "others/history/bg01.png", 1280, 960);
            decode_retail_png(*archive, filter, "bgimage/bg010.png", 1280, 960);
            decode_retail_png(*archive, filter,
                              "others/title/sys_bg_title.png", 1280, 960);
            decode_retail_png(*archive, filter,
                              "others/title/sys_bg_title_logo.png", 240, 658);
            decode_retail_png(*archive, filter,
                              "others/title/sys_bg_title_start.png", 486, 50);
        }
        const auto samples = collect_xp3_filter_samples(*archive, 16, &filter, &error);
        check(!samples.empty(), "cannot sample retail XP3: " + error);
        sampled += samples.size();
        for (const auto& sample : samples)
            if (FilterHeuristic::score_plaintext(sample.filename, sample.bytes) >= 80)
                ++recognized;

        for (const auto& entry : archive->entries()) {
            if (!script_extension(entry.name) && !scenario_extension(entry.name))
                continue;
            const auto bytes = archive->read(entry, 32u * 1024u * 1024u,
                                             &filter, &error);
            check(bytes.has_value(), "cannot decrypt " + entry.name + ": " + error);
            std::string source;
            check(decode_kirikiri_text(*bytes, source, &error),
                  "cannot decode " + entry.name + ": " + error);
            check(!source.empty(), "decoded retail text is empty: " + entry.name);
            collect_literal_plugin_links(source, plugin_requests);
            if (scenario_extension(entry.name)) {
                scenario_sources.emplace(entry.name, std::move(source));
                ++decoded_scenarios;
                continue;
            }
            try {
                compile_tjs(*compiler, source, entry.name);
            } catch (const TJS::eTJS& exception) {
                throw std::runtime_error(
                    "Yuri cannot compile " + entry.name + ": " +
                    TJS::ttstr(exception.GetMessage()).AsStdString());
            }
            script_sources.emplace(entry.name, std::move(source));
            ++compiled_scripts;
        }
    }
    check(sampled >= 32 && recognized == sampled,
          "retail filter did not decrypt every sampled asset");
    // This exact fingerprint currently contains 64 TJS units in data.xp3.
    // Pin the inventory so silently skipped, renamed, or newly unreadable
    // scripts cannot turn this into a partial-success test.
    check(compiled_scripts == 64,
          "retail TJS inventory mismatch: compiled " +
              std::to_string(compiled_scripts) + ", expected 64");
    check(decoded_scenarios == 198,
          "retail scenario inventory mismatch: decoded " +
              std::to_string(decoded_scenarios) + ", expected 198");
    const std::set<std::string> expected_plugin_requests = {
        "addfont.dll",
        "extrans.dll",
        "kagparser.dll",
        "layerexsave.dll",
        "menu.dll",
        "wuvorbis.dll",
    };
    check(plugin_requests == expected_plugin_requests,
          "retail native plugin requirements changed or were not fully decoded");
    const auto startup_script = script_sources.find("startup.tjs");
    const auto boot_script = script_sources.find("system/Boot.tjs");
    const auto history_script =
        script_sources.find("system/HistoryLayer.tjs");
    check(startup_script != script_sources.end() &&
              startup_script->second.find(
                  "Scripts.execStorage(\"system/Boot.tjs\")") != std::string::npos,
          "retail startup no longer enters the expected KAG boot script");
    check(boot_script != script_sources.end(),
          "retail KAG boot script was not decoded and compiled");
    const auto app_id_guard_begin = boot_script->second.find(
        "if(System.checkAppId != global.APP_ID){");
    const auto app_id_guard_end = boot_script->second.find(
        "kag.process(\"first.ks\")", app_id_guard_begin);
    check(app_id_guard_begin != std::string::npos &&
              app_id_guard_end != std::string::npos,
          "retail KAG application-ID guard changed");
    const std::string app_id_guard = boot_script->second.substr(
        app_id_guard_begin, app_id_guard_end - app_id_guard_begin);
    check(execute_system_app_id_guard(
              app_id_guard, "c1ce5e17-e483-4195-a8f7-a019f96c6805") == 0,
          "System.checkAppId compatibility failed the decrypted retail guard");
    check(history_script != script_sources.end() &&
              history_script->second.find(
                  "font.getTextWidth(currentLine += ch)") !=
                  std::string::npos,
          "retail KAG no longer measures the growing history-line prefix");
    for (const std::string_view required_boot_step : {
             "KAGLoadScript(\"MainWindow.tjs\")",
             "KAGLoadScript(\"Override.tjs\")",
             "global.kag = new KAGWindow()",
             "kag.process(\"first.ks\")"}) {
        check(boot_script->second.find(required_boot_step) != std::string::npos,
              "retail KAG boot chain changed at " +
                  std::string(required_boot_step));
    }

    const auto first_scenario =
        scenario_sources.find("system/gamesystem/first.ks");
    const auto title_scenario =
        scenario_sources.find("system/gamesystem/SysTitle.ks");
    const auto macro_scenario =
        scenario_sources.find("system/gamesystem/SysMacro.ks");
    check(first_scenario != scenario_sources.end(),
          "retail first.ks was not decrypted");
    for (const std::string_view required_first_step : {
             "@call storage=\"SysMacro.ks\"",
             "@call storage=\"SysMacroAdd.ks\"",
             "@call storage=\"SliderControl.ks\"",
             "[jump storage=\"SysTitle.ks\" target=*maker_logo]"}) {
        check(first_scenario->second.find(required_first_step) !=
                  std::string::npos,
              "retail first.ks title path changed at " +
                  std::string(required_first_step));
    }
    check(title_scenario != scenario_sources.end(),
          "retail SysTitle.ks was not decrypted");
    for (const std::string_view required_title_step : {
             "*mainmenu|",
             "storage=sys_bg_title_logo",
             "storage=\"MainFlow.ks\"",
             "global.config_window.show()"}) {
        check(title_scenario->second.find(required_title_step) !=
                  std::string::npos,
              "retail title flow changed at " +
                  std::string(required_title_step));
    }
    check(macro_scenario != scenario_sources.end(),
          "retail SysMacro.ks was not decrypted");
    for (const std::string_view required_transition : {
             "method=wave",
             "method=mosaic",
             "method=turn",
             "method=rotatezoom",
             "method=rotatevanish",
             "method=rotateswap",
             "method=ripple"}) {
        check(macro_scenario->second.find(required_transition) !=
                  std::string::npos,
              "retail transition contract changed at " +
                  std::string(required_transition));
    }
    const auto custom_window =
        script_sources.find("system/CustomMainWindow.tjs");
    check(custom_window != script_sources.end() &&
              custom_window->second.find(".saveLayerImagePng(") !=
                  std::string::npos,
          "retail game no longer exposes its layerExSave PNG call path");

    GameProfile profile = GameProfile::defaults(game);
    profile.xp3_filter_path = filter_path;
    profile.patch_root = patch_path.parent_path();
    auto storage = GameStorage::mount(profile, &error);
    check(storage.has_value(), "cannot mount complete retail storage: " + error);
    check(storage->exists("startup.tjs"), "retail startup.tjs is not mounted");
    const auto startup = storage->read_script("startup.tjs", &error);
    check(startup.has_value() && !startup->empty(),
          "retail startup.tjs cannot be decrypted: " + error);
    for (const std::string_view required_storage : {
             "system/Boot.tjs",
             "system/default/MainWindow.tjs",
             "system/CustomMainWindow.tjs",
             "system/KrkrzAddMainWindow.tjs",
             "system/Override.tjs",
             "system/gamesystem/first.ks"}) {
        check(storage->exists(required_storage),
              "retail startup dependency is not mounted: " +
                  std::string(required_storage));
    }

    std::ifstream patch_input(patch_path, std::ios::binary);
    const std::string patch_source((std::istreambuf_iterator<char>(patch_input)),
                                   std::istreambuf_iterator<char>());
    compile_tjs(*compiler, patch_source, "patch.tjs");
    compile_tjs(*compiler, filter_source, "xp3filter.tjs");
    execute_sample_patch(*compiler, patch_source);

    std::cout << "retail sample passed: " << game.archives.size()
              << " XP3 archives, " << sampled << " decrypted assets, "
              << compiled_scripts << " compiled TJS scripts, "
              << decoded_scenarios << " decoded KAG scenarios, "
              << "4 decoded title/game PNGs, "
              << plugin_requests.size() << " pinned native plugin contracts, "
              << "7 pinned extrans methods\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--retail-sample") {
            test_retail_sample(argv[2], argv[3]);
            return 0;
        }
        check(argc == 1, "unknown test arguments");
        test_layer_draw_completion_guard();
        test_hybrid_render_task_policy();
        test_sha256();
        test_write_all_bytes();
        test_read_all_bytes();
        test_kag_system_variable_recovery_primitives();
        test_zlib_output_window_equivalence();
        test_ms_gothic_fast_path_primitives();
        test_yuri_text_width_prefix_reuse();
        test_rgba_frame_probe();
        test_frame_update_gate();
        test_frame_damage_tracking();
        test_presentation_surface_transaction();
        test_yuri_window_update_policy();
        test_vita_thread_policy();
        test_render_task_pool();
        test_yuri_engine_tick_pacer();
        test_yuri_presentation_reference_contract();
        test_deferred_recycle_fixed_point();
        test_vita_memory_budget_contract();
        test_vita_render_surface_contract();
        test_empty_yuri_string();
        test_yuri_compiler_lifecycle();
        test_empty_yuri_string_properties();
        test_yuri_shift_semantics();
        test_retail_after_startup_resource();
        test_normalize();
        test_vita_storage_paths();
        test_virtual_cd();
        test_vita_title_id();
        test_vita_input_profile();
        test_keyboard_repeat_scheduler();
        test_pvf_kirikiri_baseline();
        test_audio_playback_state();
        test_vita_touch_mapping();
        test_manifest_and_resolver();
        test_patch_cache_integrity();
        test_filter_detection();
        test_generalized_filter_detection();
        test_yuri_filter_vm();
        test_system_app_id_compat();
        test_sfo();
        test_text_codec();
        test_loose_storage();
        std::cout << "all tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "test failure: " << exception.what() << '\n';
        return 1;
    }
}

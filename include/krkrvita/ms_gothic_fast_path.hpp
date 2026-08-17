#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace krkrvita {

// FreeType performs many small, non-sequential reads from an SFNT stream.
// Keep the cache deliberately small relative to Vita's application heap and
// make large requests bypass it.  The payload budget is exactly 512 KiB;
// metadata lives outside that budget.
inline constexpr std::size_t ms_gothic_page_size = 16u * 1024u;
inline constexpr std::size_t ms_gothic_page_count = 32u;
inline constexpr std::size_t ms_gothic_cache_payload_bytes =
    ms_gothic_page_size * ms_gothic_page_count;
inline constexpr std::size_t ms_gothic_direct_read_threshold =
    ms_gothic_page_size;
static_assert(ms_gothic_cache_payload_bytes <= 512u * 1024u);

using MsGothicReadAt = std::size_t (*)(void* context,
                                      std::uint64_t offset,
                                      void* destination,
                                      std::size_t bytes);

// Resolve tTJSBinaryStream seek arithmetic without signed overflow.  Invalid
// seeks leave the caller's logical position unchanged.
inline bool ms_gothic_resolve_seek(std::uint64_t current,
                                   std::uint64_t size,
                                   std::int64_t offset,
                                   int whence,
                                   std::uint64_t& result) noexcept {
    std::uint64_t base = 0;
    switch (whence) {
        case 0: base = 0; break;
        case 1: base = current; break;
        case 2: base = size; break;
        default: return false;
    }

    if (offset >= 0) {
        const auto positive = static_cast<std::uint64_t>(offset);
        if (positive > std::numeric_limits<std::uint64_t>::max() - base)
            return false;
        result = base + positive;
        return true;
    }

    // -(INT64_MIN) is undefined, so form its magnitude without negating it.
    const auto magnitude = static_cast<std::uint64_t>(-(offset + 1)) + 1u;
    if (magnitude > base) return false;
    result = base - magnitude;
    return true;
}

class MsGothicPageCache {
public:
    MsGothicPageCache(std::uint64_t file_size,
                      MsGothicReadAt read_at,
                      void* context) noexcept
        : file_size_(file_size), read_at_(read_at), context_(context) {}

    std::size_t read(std::uint64_t offset,
                     void* destination,
                     std::size_t requested) noexcept {
        if (!destination || requested == 0 || !read_at_ ||
            offset >= file_size_)
            return 0;

        const std::uint64_t available64 = file_size_ - offset;
        const std::size_t available = available64 < requested
            ? static_cast<std::size_t>(available64)
            : requested;
        auto* output = static_cast<std::uint8_t*>(destination);

        // Large FreeType table transfers already amortize a kernel call.  Do
        // not churn all 32 cache pages while copying a one-shot table.
        if (available >= ms_gothic_direct_read_threshold)
            return read_fully(offset, output, available);

        std::size_t completed = 0;
        while (completed < available) {
            const std::uint64_t at = offset + completed;
            const std::uint64_t page_offset =
                at - (at % ms_gothic_page_size);
            Page* page = find(page_offset);
            if (!page) page = fill(page_offset);
            if (!page) break;

            const std::size_t within = static_cast<std::size_t>(at - page_offset);
            if (within >= page->valid_bytes) {
                // A short/error read must remain retryable rather than turning
                // the unfilled suffix into a permanent false EOF cache hit.
                page->populated = false;
                page = fill(page_offset);
                if (!page || within >= page->valid_bytes) break;
            }
            const std::size_t chunk = std::min(
                available - completed, page->valid_bytes - within);
            std::memcpy(output + completed, page->bytes.data() + within, chunk);
            completed += chunk;
        }
        return completed;
    }

    std::uint64_t file_size() const noexcept { return file_size_; }

private:
    struct Page {
        // Deliberately do not zero 512 KiB when the optional cache is first
        // created. Only valid_bytes is ever exposed after a successful read.
        Page() noexcept {}
        std::array<std::uint8_t, ms_gothic_page_size> bytes;
        std::uint64_t offset = 0;
        std::uint64_t stamp = 0;
        std::size_t valid_bytes = 0;
        bool populated = false;
    };

    std::size_t read_fully(std::uint64_t offset,
                           std::uint8_t* destination,
                           std::size_t bytes) noexcept {
        std::size_t completed = 0;
        while (completed < bytes) {
            const std::size_t result = read_at_(
                context_, offset + completed, destination + completed,
                bytes - completed);
            if (result == 0 || result > bytes - completed) break;
            completed += result;
        }
        return completed;
    }

    Page* find(std::uint64_t offset) noexcept {
        for (Page& page : pages_) {
            if (page.populated && page.offset == offset) {
                page.stamp = next_stamp();
                return &page;
            }
        }
        return nullptr;
    }

    Page* fill(std::uint64_t offset) noexcept {
        Page* victim = &pages_[0];
        for (Page& page : pages_) {
            if (!page.populated) {
                victim = &page;
                break;
            }
            if (page.stamp < victim->stamp) victim = &page;
        }

        const std::uint64_t remaining64 = file_size_ - offset;
        const std::size_t wanted = remaining64 < ms_gothic_page_size
            ? static_cast<std::size_t>(remaining64)
            : ms_gothic_page_size;
        const std::size_t received =
            read_fully(offset, victim->bytes.data(), wanted);
        if (received == 0) {
            victim->populated = false;
            victim->valid_bytes = 0;
            return nullptr;
        }
        victim->offset = offset;
        victim->valid_bytes = received;
        victim->stamp = next_stamp();
        victim->populated = true;
        return victim;
    }

    std::uint64_t next_stamp() noexcept {
        // A wrap once per 2^64 cache touches is not a realistic Vita runtime,
        // but keeping zero reserved makes even that case deterministic.
        ++stamp_;
        if (stamp_ == 0) {
            std::uint64_t replacement = 1;
            for (Page& page : pages_)
                if (page.populated) page.stamp = replacement++;
            stamp_ = replacement;
        }
        return stamp_;
    }

    std::uint64_t file_size_ = 0;
    MsGothicReadAt read_at_ = nullptr;
    void* context_ = nullptr;
    std::uint64_t stamp_ = 0;
    std::array<Page, ms_gothic_page_count> pages_;
};

struct YuriGlyphCacheKey {
    std::uint32_t character = 0;
    std::uint32_t options = 0;
    std::int32_t height = 0;

    friend constexpr bool operator==(const YuriGlyphCacheKey& left,
                                     const YuriGlyphCacheKey& right) noexcept {
        return left.character == right.character &&
               left.options == right.options && left.height == right.height;
    }
};

inline constexpr std::size_t yuri_glyph_cache_hash(
    const YuriGlyphCacheKey& key) noexcept {
    std::uint32_t value = key.character * 0x9e3779b1u;
    value ^= key.options + 0x85ebca6bu + (value << 6u) + (value >> 2u);
    value ^= static_cast<std::uint32_t>(key.height) * 0xc2b2ae35u;
    value ^= value >> 16u;
    return value;
}

// Direct-mapped lookup keeps the hot measurement path allocation-free and
// bounded.  A collision only causes another exact FreeType load; key equality
// prevents stale metrics from ever becoming observable.
template <typename Value, std::size_t SlotCount = 256>
class YuriGlyphMetricsCache {
    static_assert(SlotCount > 0 && (SlotCount & (SlotCount - 1u)) == 0,
                  "glyph cache slot count must be a power of two");
    static_assert(std::is_copy_assignable_v<Value>);

public:
    void clear() noexcept {
        for (Entry& entry : entries_) entry.valid = false;
    }

    bool find(const YuriGlyphCacheKey& key, Value& value) const noexcept {
        const Entry& entry = entries_[yuri_glyph_cache_hash(key) &
                                      (SlotCount - 1u)];
        if (!entry.valid || !(entry.key == key)) return false;
        value = entry.value;
        return true;
    }

    void store(const YuriGlyphCacheKey& key, const Value& value) noexcept {
        Entry& entry = entries_[yuri_glyph_cache_hash(key) &
                                (SlotCount - 1u)];
        entry.key = key;
        entry.value = value;
        entry.valid = true;
    }

private:
    struct Entry {
        YuriGlyphCacheKey key{};
        Value value{};
        bool valid = false;
    };
    std::array<Entry, SlotCount> entries_{};
};

struct YuriPreparedGlyphKey {
    std::uint32_t character = 0;
    std::uint32_t glyph_index = 0;
    std::uint32_t options = 0;
    std::int32_t height = 0;
    std::int32_t load_flags = 0;

    friend constexpr bool operator==(const YuriPreparedGlyphKey& left,
                                     const YuriPreparedGlyphKey& right) noexcept {
        return left.character == right.character &&
               left.glyph_index == right.glyph_index &&
               left.options == right.options && left.height == right.height &&
               left.load_flags == right.load_flags;
    }
};

}  // namespace krkrvita

#pragma once

#include "krkrvita/filter_heuristic.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace krkrvita {

class Xp3FilterVm;

struct Xp3Segment {
    bool compressed = false;
    std::uint64_t archive_offset = 0;
    std::uint64_t file_offset = 0;
    std::uint64_t original_size = 0;
    std::uint64_t archived_size = 0;
};

struct Xp3Entry {
    std::string name;
    std::uint32_t flags = 0;
    std::uint32_t hash = 0;
    std::uint64_t original_size = 0;
    std::uint64_t archived_size = 0;
    std::vector<Xp3Segment> segments;
};

// A bounded, portable XP3 index reader. It supports standalone archives and
// XP3 data appended to a Windows executable, split indices, raw/zlib index
// blocks, raw/zlib file segments, and Yuri-compatible extraction filters.
class Xp3Archive {
public:
    static std::optional<Xp3Archive> open(const std::filesystem::path& path,
                                          std::string* error = nullptr);

    const std::filesystem::path& path() const { return path_; }
    std::uint64_t archive_offset() const { return archive_offset_; }
    const std::vector<Xp3Entry>& entries() const { return entries_; }
    const Xp3Entry* find(std::string_view name) const;

    std::optional<std::vector<std::uint8_t>>
    read_prefix(const Xp3Entry& entry, std::size_t byte_count,
                Xp3FilterVm* filter = nullptr,
                std::string* error = nullptr) const;
    std::optional<std::vector<std::uint8_t>>
    read(const Xp3Entry& entry, std::size_t safety_limit,
         Xp3FilterVm* filter = nullptr,
         std::string* error = nullptr) const;

private:
    std::filesystem::path path_;
    std::uint64_t archive_offset_ = 0;
    std::vector<Xp3Entry> entries_;
};

std::vector<FilterSample> collect_xp3_filter_samples(
    const Xp3Archive& archive, std::size_t maximum = 32,
    Xp3FilterVm* filter = nullptr, std::string* error = nullptr);

} // namespace krkrvita

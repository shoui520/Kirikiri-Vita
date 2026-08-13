#include "krkrvita/xp3_archive.hpp"

#include "krkrvita/xp3_filter_vm.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <cctype>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>

#include <zlib.h>

namespace krkrvita {
namespace {

constexpr std::array<std::uint8_t, 11> kXp3Mark = {
    'X', 'P', '3', 0x0d, 0x0a, 0x20, 0x0a, 0x1a, 0x8b, 0x67, 0x01,
};
constexpr std::uint64_t kMaxIndexBytes = 256ull * 1024 * 1024;
constexpr unsigned kMaxIndexParts = 4096;

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        throw std::runtime_error("truncated XP3 integer");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1]) << 8;
}

std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("truncated XP3 integer");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           static_cast<std::uint32_t>(bytes[offset + 1]) << 8 |
           static_cast<std::uint32_t>(bytes[offset + 2]) << 16 |
           static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
}

std::uint64_t u64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 8) {
        throw std::runtime_error("truncated XP3 integer");
    }
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(bytes[offset + i]) << (i * 8);
    return value;
}

void read_exact(std::ifstream& stream, void* output, std::size_t length) {
    if (length > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("XP3 read is too large");
    }
    stream.read(static_cast<char*>(output), static_cast<std::streamsize>(length));
    if (!stream) throw std::runtime_error("truncated XP3 archive");
}

std::uint64_t read_u64(std::ifstream& stream) {
    std::array<std::uint8_t, 8> bytes{};
    read_exact(stream, bytes.data(), bytes.size());
    return u64(bytes, 0);
}

void seek(std::ifstream& stream, std::uint64_t position, std::uint64_t file_size) {
    if (position > file_size ||
        position > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw std::runtime_error("XP3 offset lies outside the archive");
    }
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(position));
    if (!stream) throw std::runtime_error("cannot seek in XP3 archive");
}

std::uint64_t file_size(std::ifstream& stream) {
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) throw std::runtime_error("cannot determine XP3 size");
    return static_cast<std::uint64_t>(end);
}

std::uint64_t find_archive_offset(std::ifstream& stream, std::uint64_t size) {
    std::array<std::uint8_t, 11> mark{};
    seek(stream, 0, size);
    read_exact(stream, mark.data(), mark.size());
    if (mark == kXp3Mark) return 0;
    if (mark[0] != 'M' || mark[1] != 'Z') {
        throw std::runtime_error("file is not an XP3 archive");
    }

    // Executable-bound XP3 archives are paragraph aligned by definition.
    for (std::uint64_t position = 16; position + kXp3Mark.size() <= size; position += 16) {
        seek(stream, position, size);
        read_exact(stream, mark.data(), mark.size());
        if (mark == kXp3Mark) return position;
    }
    throw std::runtime_error("Windows executable has no bound XP3 archive");
}

std::vector<std::uint8_t> inflate_exact(std::span<const std::uint8_t> input,
                                        std::uint64_t output_size) {
    if (output_size > kMaxIndexBytes || output_size > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("XP3 index exceeds the safety limit");
    }
    std::vector<std::uint8_t> output(static_cast<std::size_t>(output_size));
    uLongf length = static_cast<uLongf>(output.size());
    if (input.size() > std::numeric_limits<uLong>::max() || output.size() > ULONG_MAX) {
        throw std::runtime_error("XP3 index is too large for zlib");
    }
    const auto result = ::uncompress(output.data(), &length, input.data(),
                                     static_cast<uLong>(input.size()));
    if (result != Z_OK || length != output.size()) {
        throw std::runtime_error("cannot decompress XP3 index");
    }
    return output;
}

std::string utf16le_to_utf8(std::span<const std::uint8_t> bytes, std::size_t units) {
    if (units > bytes.size() / 2) throw std::runtime_error("truncated XP3 filename");
    std::string output;
    output.reserve(units * 2);
    for (std::size_t i = 0; i < units; ++i) {
        std::uint32_t codepoint = u16(bytes, i * 2);
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (++i >= units) throw std::runtime_error("invalid XP3 filename surrogate");
            const auto low = u16(bytes, i * 2);
            if (low < 0xdc00 || low > 0xdfff) {
                throw std::runtime_error("invalid XP3 filename surrogate");
            }
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
        } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            throw std::runtime_error("invalid XP3 filename surrogate");
        }
        if (codepoint <= 0x7f) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }
    return output;
}

struct Chunk {
    std::array<char, 4> name{};
    std::span<const std::uint8_t> data;
};

std::vector<Chunk> chunks(std::span<const std::uint8_t> bytes) {
    std::vector<Chunk> output;
    std::size_t position = 0;
    while (position < bytes.size()) {
        if (bytes.size() - position < 12) throw std::runtime_error("truncated XP3 chunk header");
        Chunk chunk;
        std::memcpy(chunk.name.data(), bytes.data() + position, 4);
        const auto length = u64(bytes, position + 4);
        position += 12;
        if (length > bytes.size() - position) throw std::runtime_error("truncated XP3 chunk");
        chunk.data = bytes.subspan(position, static_cast<std::size_t>(length));
        output.push_back(chunk);
        position += static_cast<std::size_t>(length);
    }
    return output;
}

const Chunk* find_chunk(const std::vector<Chunk>& values, const char* name) {
    const auto found = std::find_if(values.begin(), values.end(), [&](const Chunk& chunk) {
        return std::memcmp(chunk.name.data(), name, 4) == 0;
    });
    return found == values.end() ? nullptr : &*found;
}

void parse_index(std::span<const std::uint8_t> index, std::uint64_t base,
                 std::uint64_t size, std::vector<Xp3Entry>& entries) {
    for (const auto& outer : chunks(index)) {
        if (std::memcmp(outer.name.data(), "File", 4) != 0) continue;
        const auto inner = chunks(outer.data);
        const auto* info = find_chunk(inner, "info");
        const auto* segm = find_chunk(inner, "segm");
        const auto* adlr = find_chunk(inner, "adlr");
        if (!info || !segm || !adlr || info->data.size() < 22 || adlr->data.size() < 4) {
            throw std::runtime_error("XP3 File chunk is missing required metadata");
        }

        Xp3Entry entry;
        entry.flags = u32(info->data, 0);
        entry.original_size = u64(info->data, 4);
        entry.archived_size = u64(info->data, 12);
        const auto name_units = u16(info->data, 20);
        if (std::uint64_t(name_units) * 2 > info->data.size() - 22) {
            throw std::runtime_error("truncated XP3 filename");
        }
        entry.name = utf16le_to_utf8(info->data.subspan(22), name_units);
        entry.hash = u32(adlr->data, 0);
        if (segm->data.size() % 28 != 0) throw std::runtime_error("malformed XP3 segment table");
        std::uint64_t logical_offset = 0;
        for (std::size_t position = 0; position < segm->data.size(); position += 28) {
            Xp3Segment segment;
            const auto flags = u32(segm->data, position);
            if ((flags & 7) > 1) throw std::runtime_error("unsupported XP3 segment encoding");
            segment.compressed = (flags & 7) == 1;
            const auto relative_offset = u64(segm->data, position + 4);
            if (relative_offset > size - std::min(base, size)) {
                throw std::runtime_error("XP3 segment offset lies outside the archive");
            }
            segment.archive_offset = base + relative_offset;
            segment.file_offset = logical_offset;
            segment.original_size = u64(segm->data, position + 12);
            segment.archived_size = u64(segm->data, position + 20);
            if (segment.archive_offset > size || segment.archived_size > size - segment.archive_offset) {
                throw std::runtime_error("XP3 segment lies outside the archive");
            }
            if (segment.original_size > std::numeric_limits<std::uint64_t>::max() - logical_offset) {
                throw std::runtime_error("XP3 logical file size overflow");
            }
            logical_offset += segment.original_size;
            entry.segments.push_back(segment);
        }
        if (logical_offset != entry.original_size) {
            throw std::runtime_error("XP3 segment sizes do not match file metadata");
        }
        entries.push_back(std::move(entry));
    }
}

std::vector<std::uint8_t> read_segment_prefix(std::ifstream& stream,
                                              const Xp3Segment& segment,
                                              std::size_t wanted,
                                              std::uint64_t size) {
    wanted = static_cast<std::size_t>(std::min<std::uint64_t>(wanted, segment.original_size));
    std::vector<std::uint8_t> output(wanted);
    seek(stream, segment.archive_offset, size);
    if (!segment.compressed) {
        read_exact(stream, output.data(), output.size());
        return output;
    }

    z_stream z{};
    if (inflateInit(&z) != Z_OK) throw std::runtime_error("cannot initialize zlib");
    std::array<std::uint8_t, 32 * 1024> input{};
    std::uint64_t remaining = segment.archived_size;
    z.next_out = output.data();
    z.avail_out = static_cast<uInt>(output.size());
    int result = Z_OK;
    while (z.avail_out && result != Z_STREAM_END) {
        if (!z.avail_in) {
            const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(input.size(), remaining));
            if (!amount) break;
            read_exact(stream, input.data(), amount);
            remaining -= amount;
            z.next_in = input.data();
            z.avail_in = static_cast<uInt>(amount);
        }
        result = inflate(&z, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&z);
            throw std::runtime_error("cannot decompress XP3 segment");
        }
    }
    const auto produced = output.size() - z.avail_out;
    inflateEnd(&z);
    if (produced != output.size()) throw std::runtime_error("truncated compressed XP3 segment");
    return output;
}

bool useful_filter_sample(std::string_view name) {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos) return false;
    std::string extension(name.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".ogg" || extension == ".wav" || extension == ".tlg" ||
           extension == ".tjs" || extension == ".ks";
}

} // namespace

std::optional<Xp3Archive> Xp3Archive::open(const std::filesystem::path& path,
                                            std::string* error) {
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open XP3 archive: " + path.string());
        const auto size = file_size(stream);
        if (size < 19) throw std::runtime_error("XP3 archive is too small");

        Xp3Archive archive;
        archive.path_ = path;
        archive.archive_offset_ = find_archive_offset(stream, size);
        seek(stream, archive.archive_offset_ + 11, size);

        std::uint64_t total_index_bytes = 0;
        for (unsigned part = 0; part < kMaxIndexParts; ++part) {
            const auto relative_index_offset = read_u64(stream);
            if (relative_index_offset > size - archive.archive_offset_) {
                throw std::runtime_error("XP3 index offset lies outside the archive");
            }
            seek(stream, archive.archive_offset_ + relative_index_offset, size);
            std::uint8_t flags = 0;
            read_exact(stream, &flags, 1);

            std::vector<std::uint8_t> index;
            if ((flags & 7) == 0) {
                const auto length = read_u64(stream);
                if (length > kMaxIndexBytes || total_index_bytes > kMaxIndexBytes - length) {
                    throw std::runtime_error("XP3 index exceeds the safety limit");
                }
                index.resize(static_cast<std::size_t>(length));
                read_exact(stream, index.data(), index.size());
            } else if ((flags & 7) == 1) {
                const auto compressed_size = read_u64(stream);
                const auto original_size = read_u64(stream);
                if (compressed_size > kMaxIndexBytes || original_size > kMaxIndexBytes ||
                    total_index_bytes > kMaxIndexBytes - original_size) {
                    throw std::runtime_error("XP3 index exceeds the safety limit");
                }
                std::vector<std::uint8_t> compressed(static_cast<std::size_t>(compressed_size));
                read_exact(stream, compressed.data(), compressed.size());
                index = inflate_exact(compressed, original_size);
            } else {
                throw std::runtime_error("unsupported XP3 index encoding");
            }
            total_index_bytes += index.size();
            parse_index(index, archive.archive_offset_, size, archive.entries_);
            if (!(flags & 0x80)) return archive;
            // The pointer to the next index follows the current index payload.
        }
        throw std::runtime_error("XP3 index continuation limit exceeded");
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>>
Xp3Archive::read_prefix(const Xp3Entry& entry, std::size_t byte_count,
                        Xp3FilterVm* filter, std::string* error) const {
    try {
        std::ifstream stream(path_, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot reopen XP3 archive: " + path_.string());
        const auto size = file_size(stream);
        const auto wanted = static_cast<std::size_t>(
            std::min<std::uint64_t>(byte_count, entry.original_size));
        std::vector<std::uint8_t> output;
        output.reserve(wanted);
        for (const auto& segment : entry.segments) {
            if (output.size() >= wanted) break;
            auto bytes = read_segment_prefix(stream, segment, wanted - output.size(), size);
            if (filter && !bytes.empty() &&
                !filter->decode(entry.hash, segment.file_offset, bytes, entry.name, error)) {
                return std::nullopt;
            }
            output.insert(output.end(), bytes.begin(), bytes.end());
        }
        if (output.size() != wanted) throw std::runtime_error("XP3 file has insufficient segment data");
        return output;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return std::nullopt;
    }
}

const Xp3Entry* Xp3Archive::find(std::string_view name) const {
    std::string normalized(name);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char value) {
                       if (value == '\\') return '/';
                       return static_cast<char>(std::tolower(value));
                   });
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                   [&](const Xp3Entry& entry) {
        std::string candidate = entry.name;
        std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                       [](unsigned char value) {
                           if (value == '\\') return '/';
                           return static_cast<char>(std::tolower(value));
                       });
        return candidate == normalized;
    });
    return found == entries_.end() ? nullptr : &*found;
}

std::optional<std::vector<std::uint8_t>>
Xp3Archive::read(const Xp3Entry& entry, std::size_t safety_limit,
                 Xp3FilterVm* filter, std::string* error) const {
    if (entry.original_size > safety_limit) {
        if (error) *error = "XP3 entry exceeds the caller's memory safety limit";
        return std::nullopt;
    }
    return read_prefix(entry, static_cast<std::size_t>(entry.original_size), filter, error);
}

std::vector<FilterSample> collect_xp3_filter_samples(
    const Xp3Archive& archive, std::size_t maximum,
    Xp3FilterVm* filter, std::string* error) {
    std::vector<FilterSample> samples;
    if (!maximum) return samples;
    for (const auto& entry : archive.entries()) {
        if (!useful_filter_sample(entry.name) || !entry.original_size) continue;
        auto bytes = archive.read_prefix(entry, 512, filter, error);
        if (!bytes) return {};
        samples.push_back({entry.hash, 0, entry.name, std::move(*bytes)});
        if (samples.size() == maximum) break;
    }
    return samples;
}

} // namespace krkrvita

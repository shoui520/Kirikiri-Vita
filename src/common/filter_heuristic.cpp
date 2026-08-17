#include "krkrvita/filter_heuristic.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace krkrvita {
namespace {

struct KnownByte {
    std::size_t offset;
    std::uint8_t value;
};

bool begins(std::span<const std::uint8_t> bytes,
            std::initializer_list<std::uint8_t> prefix) {
    return bytes.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), bytes.begin());
}

std::string lower_extension(std::string_view filename) {
    const auto dot = filename.rfind('.');
    if (dot == std::string_view::npos) return {};
    std::string result(filename.substr(dot));
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string normalized_filename(std::string_view filename) {
    std::string result(filename);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) {
                       if (c == '\\') return '/';
                       return static_cast<char>(std::tolower(c));
                   });
    return result;
}

std::string top_directory(std::string_view filename) {
    const auto normalized = normalized_filename(filename);
    const auto slash = normalized.find('/');
    if (slash == std::string::npos) return {};
    return normalized.substr(0, slash + 1);
}

std::vector<KnownByte> known_bytes(std::string_view filename) {
    const auto extension = lower_extension(filename);
    std::vector<KnownByte> result;
    const auto add = [&](std::size_t at, std::string_view value) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            result.push_back({at + i, static_cast<std::uint8_t>(value[i])});
        }
    };
    if (extension == ".png") {
        constexpr std::array<std::uint8_t, 16> prefix = {
            0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
            0x00, 0x00, 0x00, 0x0d, 'I', 'H', 'D', 'R',
        };
        for (std::size_t i = 0; i < prefix.size(); ++i) result.push_back({i, prefix[i]});
        result.push_back({26, 0}); // compression method
        result.push_back({27, 0}); // filter method
    } else if (extension == ".jpg" || extension == ".jpeg") {
        result = {{0, 0xff}, {1, 0xd8}, {2, 0xff}};
    } else if (extension == ".ogg") {
        add(0, "OggS");
        result.push_back({4, 0});
    } else if (extension == ".wav") {
        add(0, "RIFF");
        add(8, "WAVEfmt ");
    } else if (extension == ".webp") {
        add(0, "RIFF");
        add(8, "WEBP");
    } else if (extension == ".bmp") {
        add(0, "BM");
    } else if (extension == ".tlg") {
        add(0, "TLG");
        add(4, ".0");
        result.push_back({6, 0});
    } else if (extension == ".mp3") {
        add(0, "ID3");
    } else if (extension == ".mpg" || extension == ".mpeg") {
        result = {{0, 0x00}, {1, 0x00}, {2, 0x01}};
    } else if (extension == ".wmv") {
        constexpr std::array<std::uint8_t, 16> asf = {
            0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
            0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c,
        };
        for (std::size_t i = 0; i < asf.size(); ++i) result.push_back({i, asf[i]});
    } else if (extension == ".psb") {
        add(0, "PSB");
        result.push_back({3, 0});
    }
    return result;
}

bool has_format_constraints(std::string_view filename) {
    const auto extension = lower_extension(filename);
    return !known_bytes(filename).empty() || extension == ".tjs" ||
           extension == ".ks" || extension == ".scn" ||
           extension == ".script" || extension == ".ini" ||
           extension == ".csv" || extension == ".asd" ||
           extension == ".sli" || extension == ".json" ||
           extension == ".xml";
}

std::uint32_t read_be32(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) return 0;
    return (std::uint32_t(bytes[at]) << 24) |
           (std::uint32_t(bytes[at + 1]) << 16) |
           (std::uint32_t(bytes[at + 2]) << 8) |
           std::uint32_t(bytes[at + 3]);
}

std::uint32_t read_le32(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) return 0;
    return std::uint32_t(bytes[at]) |
           (std::uint32_t(bytes[at + 1]) << 8) |
           (std::uint32_t(bytes[at + 2]) << 16) |
           (std::uint32_t(bytes[at + 3]) << 24);
}

int png_structure_score(std::span<const std::uint8_t> bytes) {
    if (!begins(bytes, {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a})) return 0;
    if (bytes.size() < 16) return 0;
    if (read_be32(bytes, 8) != 13 ||
        !std::equal(bytes.begin() + 12, bytes.begin() + 16, "IHDR")) {
        return -160;
    }
    if (bytes.size() < 29) return 20;
    const auto width = read_be32(bytes, 16);
    const auto height = read_be32(bytes, 20);
    const auto depth = bytes[24];
    const auto color = bytes[25];
    const bool valid_depth = depth == 1 || depth == 2 || depth == 4 ||
                             depth == 8 || depth == 16;
    const bool valid_color = color == 0 || color == 2 || color == 3 ||
                             color == 4 || color == 6;
    if (!width || !height || width > 65535 || height > 65535 ||
        !valid_depth || !valid_color || bytes[26] != 0 || bytes[27] != 0 ||
        bytes[28] > 1) {
        return -120;
    }
    int score = 60;
    std::size_t at = 8;
    unsigned complete_chunks = 0;
    while (at + 12 <= bytes.size()) {
        const auto length = read_be32(bytes, at);
        if (length > 64u * 1024u * 1024u) return -140;
        if (at + 12ull + length > bytes.size()) break;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto c = bytes[at + 4 + i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return -120;
        }
        ++complete_chunks;
        at += 12 + length;
        if (complete_chunks == 1 && at >= 33) score += 20;
        if (complete_chunks >= 4) break;
    }
    return score;
}

std::uint8_t hash_fold(std::uint32_t hash, unsigned lane_mask) {
    std::uint8_t value = 0;
    for (unsigned lane = 0; lane < 4; ++lane) {
        if (lane_mask & (1u << lane)) value ^= static_cast<std::uint8_t>(hash >> (lane * 8));
    }
    return value;
}

std::uint8_t key_for(const FilterRule& rule, std::uint32_t hash,
                     std::uint64_t absolute_offset) {
    switch (rule.operation) {
    case FilterOperation::Identity: return 0;
    case FilterOperation::XorHash: return static_cast<std::uint8_t>(hash);
    case FilterOperation::XorHashShift3: return static_cast<std::uint8_t>(hash >> 3);
    case FilterOperation::XorHashShift5: return static_cast<std::uint8_t>(hash >> 5);
    case FilterOperation::XorNotHashPlus1:
        return static_cast<std::uint8_t>(~(hash + 1));
    case FilterOperation::XorConstant:
    case FilterOperation::SubConstant:
    case FilterOperation::XorThenAdd:
    case FilterOperation::XorThenNibbleSwap:
        return rule.constant;
    case FilterOperation::XorHashMultiply:
    case FilterOperation::SubHashMultiply:
        return static_cast<std::uint8_t>(hash * rule.multiplier);
    case FilterOperation::XorHashShift:
        return static_cast<std::uint8_t>(hash >> rule.shift);
    case FilterOperation::XorNotHashShift:
        return static_cast<std::uint8_t>(~(hash >> rule.shift));
    case FilterOperation::XorHashShiftConstant:
        return static_cast<std::uint8_t>((hash >> rule.shift) ^ rule.constant);
    case FilterOperation::XorHashFoldConstant:
        return static_cast<std::uint8_t>(hash_fold(hash, rule.modulus) ^ rule.constant);
    case FilterOperation::XorPeriodic:
        return rule.table.empty() ? 0 : rule.table[absolute_offset % rule.table.size()];
    case FilterOperation::XorOffsetAddConstant:
        return static_cast<std::uint8_t>(absolute_offset + rule.constant);
    case FilterOperation::XorHashOffsetParity:
        return (absolute_offset & 1) ? static_cast<std::uint8_t>(absolute_offset)
                                     : static_cast<std::uint8_t>(hash);
    case FilterOperation::XorHashShiftByOffset:
        return static_cast<std::uint8_t>(hash >> (absolute_offset % rule.modulus));
    case FilterOperation::XorHashByteLanes:
        return rule.table.empty()
            ? 0 : static_cast<std::uint8_t>(hash >> rule.table[absolute_offset % rule.table.size()]);
    case FilterOperation::XorRotatingHash: {
        std::uint32_t key = (hash ^ rule.seed_xor) & 0x7fffffffU;
        key |= key << 31;
        const auto rounds = static_cast<unsigned>(absolute_offset % rule.modulus);
        for (unsigned i = 0; i < rounds; ++i) {
            if (rule.secondary == 1)
                key = ((key & 0x1ffU) << 23) | (key >> 8);
            else
                key = (key << 23) | (key >> 8);
        }
        return static_cast<std::uint8_t>(key);
    }
    case FilterOperation::XorLcgHash: {
        std::uint32_t state = 0x015a4e35U * hash + 1;
        const auto rounds = static_cast<unsigned>(absolute_offset & 0x1ffU);
        for (unsigned i = 0; i <= rounds; ++i) state = 0x015a4e35U * state + 1;
        return static_cast<std::uint8_t>(state >> 16);
    }
    case FilterOperation::RotateLeftByPopcount:
        return 0;
    }
    return 0;
}

void transform_byte(const FilterRule& rule, std::uint32_t hash,
                    std::uint64_t absolute_offset, std::uint8_t& byte) {
    if (absolute_offset < rule.start_offset || absolute_offset >= rule.end_offset) return;
    if (!rule.segments.empty()) {
        // Each segment is bounded, so applying all of them touches every byte
        // exactly once. Ordering is irrelevant for disjoint windows and the
        // builder never produces overlapping ones.
        for (const auto& segment : rule.segments)
            if (segment) transform_byte(*segment, hash, absolute_offset, byte);
        return;
    }
    if (rule.operation == FilterOperation::RotateLeftByPopcount) {
        const unsigned amount = std::popcount(byte) & 7u;
        if (amount) byte = static_cast<std::uint8_t>((byte << amount) | (byte >> (8 - amount)));
        return;
    }
    if (rule.operation == FilterOperation::SubConstant ||
        rule.operation == FilterOperation::SubHashMultiply) {
        byte = static_cast<std::uint8_t>(byte - key_for(rule, hash, absolute_offset));
        return;
    }
    byte ^= key_for(rule, hash, absolute_offset);
    if (rule.operation == FilterOperation::XorThenAdd) byte += rule.post_add;
    if (rule.operation == FilterOperation::XorThenNibbleSwap) {
        byte = static_cast<std::uint8_t>((byte >> 4) | (byte << 4));
    }
}

std::string hex_byte(std::uint8_t value) {
    char buffer[8]{};
    std::snprintf(buffer, sizeof(buffer), "0x%02x", value);
    return buffer;
}

// The primitive transforms a segment may use. Deliberately small: every one
// is a single self-contained statement over b[i], so segments compose into a
// plain if/else chain with no per-call setup. Anything outside this set is
// refused by the segment builder rather than emitted as approximate TJS.
bool is_segment_primitive(FilterOperation operation) {
    switch (operation) {
    case FilterOperation::Identity:
    case FilterOperation::XorConstant:
    case FilterOperation::SubConstant:
    case FilterOperation::XorHashMultiply:
    case FilterOperation::SubHashMultiply:
        return true;
    default:
        return false;
    }
}

std::string segment_byte_statement(const FilterRule& rule) {
    std::ostringstream out;
    switch (rule.operation) {
    case FilterOperation::Identity: break;
    case FilterOperation::XorConstant:
        out << "b[i]^=" << unsigned(rule.constant) << ';'; break;
    case FilterOperation::SubConstant:
        out << "b[i]=(b[i]-" << unsigned(rule.constant) << ")&255;"; break;
    case FilterOperation::XorHashMultiply:
        out << "b[i]^=(h*" << unsigned(rule.multiplier) << ")&255;"; break;
    case FilterOperation::SubHashMultiply:
        out << "b[i]=(b[i]-((h*" << unsigned(rule.multiplier) << ")&255))&255;"; break;
    default: break;
    }
    return out.str();
}

bool matches_known(const FilterRule& rule, const std::vector<FilterSample>& samples,
                   std::size_t* constraint_count = nullptr) {
    std::size_t total = 0;
    std::size_t constrained_samples = 0;
    for (const auto& sample : samples) {
        std::size_t sample_constraints = 0;
        for (const auto& known : known_bytes(sample.filename)) {
            if (known.offset >= sample.bytes.size()) continue;
            auto value = sample.bytes[known.offset];
            transform_byte(rule, sample.hash, sample.offset + known.offset, value);
            if (value != known.value) return false;
            ++total;
            ++sample_constraints;
        }
        if (sample_constraints) ++constrained_samples;
    }
    if (constraint_count) *constraint_count = total;
    return total >= 8 || (total >= 4 && constrained_samples >= 2);
}

void add_if_known(std::vector<FilterRule>& rules, FilterRule rule,
                  const std::vector<FilterSample>& samples) {
    if (matches_known(rule, samples)) rules.push_back(std::move(rule));
}

std::optional<std::uint8_t> infer_constant(
    const std::vector<FilterSample>& samples,
    const std::function<std::uint8_t(const FilterSample&, std::size_t, std::uint8_t)>& infer) {
    std::optional<std::uint8_t> result;
    std::size_t constraints = 0;
    std::size_t constrained_samples = 0;
    for (const auto& sample : samples) {
        std::size_t sample_constraints = 0;
        for (const auto& known : known_bytes(sample.filename)) {
            if (known.offset >= sample.bytes.size()) continue;
            const auto value = infer(sample, known.offset, known.value);
            if (result && *result != value) return std::nullopt;
            result = value;
            ++constraints;
            ++sample_constraints;
        }
        if (sample_constraints) ++constrained_samples;
    }
    if (constraints < 8 && (constraints < 4 || constrained_samples < 2))
        return std::nullopt;
    return result;
}

std::optional<std::uint8_t> sample_uniform_xor_key(const FilterSample& sample) {
    std::optional<std::uint8_t> key;
    std::size_t constraints = 0;
    for (const auto& known : known_bytes(sample.filename)) {
        if (known.offset >= sample.bytes.size()) continue;
        const auto candidate = static_cast<std::uint8_t>(
            sample.bytes[known.offset] ^ known.value);
        if (key && *key != candidate) return std::nullopt;
        key = candidate;
        ++constraints;
    }
    return constraints >= 3 ? key : std::nullopt;
}

std::vector<FilterRule> build_candidates(const std::vector<FilterSample>& samples) {
    std::size_t known_count = 0;
    for (const auto& sample : samples)
        for (const auto& known : known_bytes(sample.filename))
            if (known.offset < sample.bytes.size()) ++known_count;
    if (!known_count) {
        std::vector<FilterRule> text_rules;
        text_rules.emplace_back(FilterOperation::Identity);
        for (unsigned key = 1; key < 256; ++key) {
            FilterRule rule{FilterOperation::XorConstant};
            rule.constant = static_cast<std::uint8_t>(key);
            text_rules.push_back(std::move(rule));
        }
        for (unsigned shift = 0; shift < 32; ++shift) {
            FilterRule direct{shift == 0 ? FilterOperation::XorHash :
                              shift == 3 ? FilterOperation::XorHashShift3 :
                              shift == 5 ? FilterOperation::XorHashShift5 :
                                           FilterOperation::XorHashShift};
            direct.shift = static_cast<std::uint8_t>(shift);
            text_rules.push_back(std::move(direct));
            FilterRule inverted{FilterOperation::XorNotHashShift};
            inverted.shift = static_cast<std::uint8_t>(shift);
            text_rules.push_back(std::move(inverted));
        }
        text_rules.emplace_back(FilterOperation::XorNotHashPlus1);
        text_rules.emplace_back(FilterOperation::XorLcgHash);
        return text_rules;
    }
    std::vector<FilterRule> base;
    add_if_known(base, FilterRule{FilterOperation::Identity}, samples);

    if (auto key = infer_constant(samples, [](const auto& sample, std::size_t at,
                                               std::uint8_t plain) {
            return static_cast<std::uint8_t>(sample.bytes[at] ^ plain);
        }); key && *key != 0) {
        FilterRule rule{FilterOperation::XorConstant};
        rule.constant = *key;
        add_if_known(base, rule, samples);
    }

    for (unsigned shift = 0; shift < 32; ++shift) {
        FilterRule direct{shift == 0 ? FilterOperation::XorHash :
                          shift == 3 ? FilterOperation::XorHashShift3 :
                          shift == 5 ? FilterOperation::XorHashShift5 :
                                       FilterOperation::XorHashShift};
        direct.shift = static_cast<std::uint8_t>(shift);
        add_if_known(base, direct, samples);

        FilterRule inverted{FilterOperation::XorNotHashShift};
        inverted.shift = static_cast<std::uint8_t>(shift);
        add_if_known(base, inverted, samples);

        const auto constant = infer_constant(samples,
            [shift](const auto& sample, std::size_t at, std::uint8_t plain) {
                return static_cast<std::uint8_t>(
                    sample.bytes[at] ^ plain ^ (sample.hash >> shift));
            });
        if (constant && *constant != 0) {
            FilterRule rule{FilterOperation::XorHashShiftConstant};
            rule.shift = static_cast<std::uint8_t>(shift);
            rule.constant = *constant;
            add_if_known(base, rule, samples);
        }
    }

    FilterRule add_reverse{FilterOperation::XorNotHashPlus1};
    add_if_known(base, add_reverse, samples);

    // Per-file keys derived by multiplying the XP3 entry hash. Multiplier 1 is
    // already covered by XorHash; the rest are only reachable here. The
    // additive variants matter because a filter that adds on write is not an
    // involution and never shows up as a XOR that fits every known byte.
    if (auto key = infer_constant(samples, [](const auto& sample, std::size_t at,
                                              std::uint8_t plain) {
            return static_cast<std::uint8_t>(sample.bytes[at] - plain);
        }); key && *key != 0) {
        FilterRule rule{FilterOperation::SubConstant};
        rule.constant = *key;
        add_if_known(base, rule, samples);
    }
    for (unsigned multiplier = 2; multiplier < 256; ++multiplier) {
        FilterRule xor_rule{FilterOperation::XorHashMultiply};
        xor_rule.multiplier = static_cast<std::uint8_t>(multiplier);
        add_if_known(base, xor_rule, samples);
    }
    for (unsigned multiplier = 1; multiplier < 256; ++multiplier) {
        FilterRule sub_rule{FilterOperation::SubHashMultiply};
        sub_rule.multiplier = static_cast<std::uint8_t>(multiplier);
        add_if_known(base, sub_rule, samples);
    }

    for (unsigned mask = 1; mask < 16; ++mask) {
        if (std::popcount(mask) < 2) continue;
        const auto constant = infer_constant(samples,
            [mask](const auto& sample, std::size_t at, std::uint8_t plain) {
                return static_cast<std::uint8_t>(sample.bytes[at] ^ plain ^
                                                 hash_fold(sample.hash, mask));
            });
        if (!constant) continue;
        FilterRule rule{FilterOperation::XorHashFoldConstant};
        rule.modulus = static_cast<std::uint8_t>(mask);
        rule.constant = *constant;
        add_if_known(base, rule, samples);
    }

    if (auto constant = infer_constant(samples,
        [](const auto& sample, std::size_t at, std::uint8_t plain) {
            return static_cast<std::uint8_t>(sample.bytes[at] ^ plain ^
                                             (sample.offset + at));
        })) {
        FilterRule rule{FilterOperation::XorOffsetAddConstant};
        rule.constant = *constant;
        add_if_known(base, rule, samples);
    }

    add_if_known(base, FilterRule{FilterOperation::XorHashOffsetParity}, samples);
    add_if_known(base, FilterRule{FilterOperation::XorLcgHash}, samples);
    for (unsigned modulus = 2; modulus <= 8; ++modulus) {
        FilterRule rule{FilterOperation::XorHashShiftByOffset};
        rule.modulus = static_cast<std::uint8_t>(modulus);
        add_if_known(base, rule, samples);
    }

    for (unsigned period = 2; period <= 16; ++period) {
        std::vector<std::optional<std::uint8_t>> inferred(period);
        bool conflict = false;
        for (const auto& sample : samples) {
            for (const auto& known : known_bytes(sample.filename)) {
                if (known.offset >= sample.bytes.size()) continue;
                const auto phase = (sample.offset + known.offset) % period;
                const auto key = static_cast<std::uint8_t>(sample.bytes[known.offset] ^ known.value);
                if (inferred[phase] && *inferred[phase] != key) conflict = true;
                inferred[phase] = key;
            }
        }
        if (conflict || std::any_of(inferred.begin(), inferred.end(),
                                    [](const auto& value) { return !value; })) continue;
        FilterRule rule{FilterOperation::XorPeriodic};
        for (const auto value : inferred) rule.table.push_back(*value);
        if (std::all_of(rule.table.begin() + 1, rule.table.end(),
                        [&](auto value) { return value == rule.table.front(); })) continue;
        add_if_known(base, rule, samples);
    }

    // Infer a periodic selection of hash shifts.  Requiring a single exact
    // shift per phase across independent hashes avoids treating arbitrary
    // literal tables as a hash-derived stream.
    for (unsigned period = 2; period <= 8; ++period) {
        std::vector<std::uint8_t> shifts;
        bool complete = true;
        for (unsigned phase = 0; phase < period; ++phase) {
            std::vector<unsigned> matching;
            for (unsigned shift = 0; shift < 32; ++shift) {
                bool okay = true;
                std::size_t seen = 0;
                std::set<std::uint32_t> hashes;
                for (const auto& sample : samples) {
                    for (const auto& known : known_bytes(sample.filename)) {
                        if (known.offset >= sample.bytes.size() ||
                            (sample.offset + known.offset) % period != phase) continue;
                        const auto key = static_cast<std::uint8_t>(
                            sample.bytes[known.offset] ^ known.value);
                        okay &= key == static_cast<std::uint8_t>(sample.hash >> shift);
                        ++seen;
                        hashes.insert(sample.hash);
                    }
                }
                if (okay && seen >= 2 && hashes.size() >= 2) matching.push_back(shift);
            }
            if (matching.size() != 1) { complete = false; break; }
            shifts.push_back(static_cast<std::uint8_t>(matching.front()));
        }
        if (complete && !std::all_of(shifts.begin() + 1, shifts.end(),
                                     [&](auto value) { return value == shifts.front(); })) {
            FilterRule rule{FilterOperation::XorHashByteLanes};
            rule.table = std::move(shifts);
            add_if_known(base, rule, samples);
        }
    }

    // The common 29/31/32-byte stream is a 31-bit seed rotated by eight bits
    // per byte. Four known bytes expose the complete seed, allowing the
    // per-game seed XOR to be derived rather than enumerated or imported.
    // `(k << 23)` and `((k & 0x1ff) << 23)` are identical in the 32-bit
    // arithmetic used by TJS, so they are one canonical mode here.
    for (unsigned mode = 0; mode == 0; ++mode) {
        for (unsigned period : {29u, 31u, 32u}) {
            std::optional<std::uint32_t> seed_xor;
            bool conflict = false;
            std::size_t seen = 0;
            for (const auto& sample : samples) {
                std::array<std::optional<std::uint8_t>, 4> bytes{};
                for (const auto& known : known_bytes(sample.filename)) {
                    if (known.offset >= sample.bytes.size() || known.offset >= 4) continue;
                    bytes[known.offset] = static_cast<std::uint8_t>(
                        sample.bytes[known.offset] ^ known.value);
                }
                if (std::any_of(bytes.begin(), bytes.end(), [](const auto& v) { return !v; }))
                    continue;
                std::uint32_t seed = std::uint32_t(*bytes[0]) |
                    (std::uint32_t(*bytes[1]) << 8) |
                    (std::uint32_t(*bytes[2]) << 16) |
                    (std::uint32_t(*bytes[3]) << 24);
                const auto candidate = (sample.hash ^ seed) & 0x7fffffffU;
                if (seed_xor && *seed_xor != candidate) conflict = true;
                seed_xor = candidate;
                ++seen;
            }
            if (conflict || !seed_xor || seen < 2) continue;
            FilterRule rule{FilterOperation::XorRotatingHash};
            rule.seed_xor = *seed_xor;
            rule.modulus = static_cast<std::uint8_t>(period);
            rule.secondary = static_cast<std::uint8_t>(mode);
            add_if_known(base, rule, samples);
        }
    }

    // Fixed XOR followed by a byte addition or nibble swap are independently
    // invertible.  Infer only exact cross-sample solutions.
    for (unsigned add = 1; add < 256; ++add) {
        const auto key = infer_constant(samples,
            [add](const auto& sample, std::size_t at, std::uint8_t plain) {
                const auto before_add = static_cast<std::uint8_t>(plain - add);
                return static_cast<std::uint8_t>(sample.bytes[at] ^ before_add);
            });
        if (!key) continue;
        FilterRule rule{FilterOperation::XorThenAdd};
        rule.constant = *key;
        rule.post_add = static_cast<std::uint8_t>(add);
        add_if_known(base, rule, samples);
    }
    if (auto key = infer_constant(samples,
        [](const auto& sample, std::size_t at, std::uint8_t plain) {
            const auto unswapped = static_cast<std::uint8_t>((plain >> 4) | (plain << 4));
            return static_cast<std::uint8_t>(sample.bytes[at] ^ unswapped);
        })) {
        FilterRule rule{FilterOperation::XorThenNibbleSwap};
        rule.constant = *key;
        add_if_known(base, rule, samples);
    }
    add_if_known(base, FilterRule{FilterOperation::RotateLeftByPopcount}, samples);

    // Several engines intentionally leave a short prefix untouched.  The
    // range start is inferred from contradictory known bytes, never assumed
    // from a particular title.
    std::vector<FilterRule> rules = base;
    for (const auto& original : base) {
        if (original.operation == FilterOperation::Identity) continue;
        for (unsigned start = 1; start <= 32; ++start) {
            auto rule = original;
            rule.start_offset = start;
            add_if_known(rules, rule, samples);
        }
    }
    for (unsigned start = 1; start <= 32; ++start) {
        for (unsigned key = 1; key < 256; ++key) {
            FilterRule constant{FilterOperation::XorConstant};
            constant.constant = static_cast<std::uint8_t>(key);
            constant.start_offset = start;
            add_if_known(rules, constant, samples);
        }
        for (unsigned shift = 0; shift < 32; ++shift) {
            FilterRule direct{shift == 0 ? FilterOperation::XorHash :
                              shift == 3 ? FilterOperation::XorHashShift3 :
                              shift == 5 ? FilterOperation::XorHashShift5 :
                                           FilterOperation::XorHashShift};
            direct.shift = static_cast<std::uint8_t>(shift);
            direct.start_offset = start;
            add_if_known(rules, direct, samples);

            FilterRule inverted{FilterOperation::XorNotHashShift};
            inverted.shift = static_cast<std::uint8_t>(shift);
            inverted.start_offset = start;
            add_if_known(rules, inverted, samples);
        }
        FilterRule reverse{FilterOperation::XorNotHashPlus1};
        reverse.start_offset = start;
        add_if_known(rules, reverse, samples);
        FilterRule lcg{FilterOperation::XorLcgHash};
        lcg.start_offset = start;
        add_if_known(rules, lcg, samples);
    }
    // Header-only filters are the inverse boundary: transform a bounded
    // prefix and leave the rest of the storage untouched. Enumerate only a
    // small, corpus-derived header window and retain exact known-byte matches;
    // an unconstrained endpoint is never guessed into a generated filter.
    for (unsigned end = 1; end <= 64; ++end) {
        for (unsigned key = 1; key < 256; ++key) {
            FilterRule constant{FilterOperation::XorConstant};
            constant.constant = static_cast<std::uint8_t>(key);
            constant.end_offset = end;
            add_if_known(rules, constant, samples);
        }
        for (unsigned shift = 0; shift < 32; ++shift) {
            FilterRule direct{shift == 0 ? FilterOperation::XorHash :
                              shift == 3 ? FilterOperation::XorHashShift3 :
                              shift == 5 ? FilterOperation::XorHashShift5 :
                                           FilterOperation::XorHashShift};
            direct.shift = static_cast<std::uint8_t>(shift);
            direct.end_offset = end;
            add_if_known(rules, direct, samples);

            FilterRule inverted{FilterOperation::XorNotHashShift};
            inverted.shift = static_cast<std::uint8_t>(shift);
            inverted.end_offset = end;
            add_if_known(rules, inverted, samples);
        }
        FilterRule reverse{FilterOperation::XorNotHashPlus1};
        reverse.end_offset = end;
        add_if_known(rules, reverse, samples);
        FilterRule lcg{FilterOperation::XorLcgHash};
        lcg.end_offset = end;
        add_if_known(rules, lcg, samples);
    }

    std::map<std::string, FilterRule> unique;
    for (auto& rule : rules) unique.emplace(rule.name(), std::move(rule));
    rules.clear();
    for (auto& [_, rule] : unique) rules.push_back(std::move(rule));
    return rules;
}

int description_cost(const FilterRule& rule) {
    int cost = 0;
    if (!rule.segments.empty()) {
        // A segmented rule is a larger hypothesis than any single transform
        // and must pay for it. What it pays for is the stride and the number
        // of regions; the individual window boundaries follow from those
        // rather than being free parameters, so they are not charged again on
        // top of each region's own transform.
        cost = 4 + 2 * static_cast<int>(rule.segments.size());
        for (const auto& segment : rule.segments) {
            if (!segment) continue;
            auto bare = *segment;
            bare.start_offset = 0;
            bare.end_offset = std::numeric_limits<std::uint64_t>::max();
            cost += description_cost(bare);
        }
        return cost;
    }
    switch (rule.operation) {
    case FilterOperation::Identity: break;
    case FilterOperation::XorHash:
    case FilterOperation::XorHashShift3:
    case FilterOperation::XorHashShift5:
    case FilterOperation::XorNotHashPlus1: cost = 1; break;
    case FilterOperation::XorConstant:
    case FilterOperation::XorHashShift:
    case FilterOperation::XorNotHashShift:
    case FilterOperation::XorOffsetAddConstant:
    case FilterOperation::XorHashOffsetParity:
    case FilterOperation::XorHashShiftByOffset:
    case FilterOperation::RotateLeftByPopcount: cost = 2; break;
    case FilterOperation::XorHashShiftConstant:
    case FilterOperation::XorHashFoldConstant:
    case FilterOperation::XorThenAdd:
    case FilterOperation::XorThenNibbleSwap: cost = 4; break;
    case FilterOperation::XorHashByteLanes:
    case FilterOperation::XorRotatingHash: cost = 5 + static_cast<int>(rule.table.size()); break;
    case FilterOperation::XorLcgHash: cost = 3; break;
    case FilterOperation::XorPeriodic: cost = 6 + static_cast<int>(rule.table.size()); break;
    case FilterOperation::SubConstant: cost = 2; break;
    case FilterOperation::XorHashMultiply:
    case FilterOperation::SubHashMultiply: cost = rule.multiplier == 1 ? 1 : 3; break;
    }
    if (rule.start_offset) cost += 3;
    if (rule.end_offset != std::numeric_limits<std::uint64_t>::max()) cost += 3;
    return cost;
}

bool same_core_transform(const FilterRule& left, const FilterRule& right) {
    const bool identical_parameters = left.operation == right.operation &&
           left.constant == right.constant &&
           left.secondary == right.secondary && left.shift == right.shift &&
           left.modulus == right.modulus && left.post_add == right.post_add &&
           left.multiplier == right.multiplier &&
           left.seed_xor == right.seed_xor && left.table == right.table;
    if (identical_parameters) return true;
    if (left.operation != FilterOperation::XorThenAdd ||
        right.operation != FilterOperation::XorThenAdd) return false;
    // XOR/add has syntactically different but byte-identical parameter pairs
    // (toggling bit 7 in the XOR and addition is the common case). Treat those
    // as one hypothesis for confidence rather than rejecting a proven map as
    // an apparent tie.
    auto left_core = left;
    auto right_core = right;
    left_core.start_offset = right_core.start_offset = 0;
    left_core.end_offset = right_core.end_offset =
        std::numeric_limits<std::uint64_t>::max();
    for (unsigned byte = 0; byte < 256; ++byte) {
        auto left_value = static_cast<std::uint8_t>(byte);
        auto right_value = left_value;
        transform_byte(left_core, 0, 0, left_value);
        transform_byte(right_core, 0, 0, right_value);
        if (left_value != right_value) return false;
    }
    return true;
}

bool is_unbounded(const FilterRule& rule) {
    return rule.start_offset == 0 &&
           rule.end_offset == std::numeric_limits<std::uint64_t>::max();
}

void discard_unproven_range_variants(std::vector<FilterRule>& rules) {
    std::vector<FilterRule> unbounded;
    for (const auto& rule : rules) {
        if (is_unbounded(rule)) unbounded.push_back(rule);
    }

    // A range boundary is evidence-backed only when the corresponding
    // unbounded transform fails a known-byte constraint.  If both candidates
    // survive build_candidates(), every directly known byte is identical
    // under both rules.  Ranking the bounded copy from a printable-byte score
    // would therefore invent an endpoint from unconstrained payload bytes.
    // Remove that non-identifiable hypothesis; real prefix/header-only rules
    // remain because their unbounded counterpart does not survive the exact
    // signature checks.
    rules.erase(std::remove_if(rules.begin(), rules.end(), [&](const auto& rule) {
        if (is_unbounded(rule)) return false;
        return std::any_of(unbounded.begin(), unbounded.end(),
                           [&](const auto& candidate) {
                               return same_core_transform(rule, candidate);
                           });
    }), rules.end());
}

// --- Offset-segmented rule discovery -------------------------------------
//
// Some Kirikiri filters do not apply one transform to a whole file. They
// switch after a fixed number of bytes: the classic case encrypts only a
// header and leaves the rest alone, and the general case walks a short
// sequence of per-region keys before settling on one for the remainder.
//
// A whole-file search cannot see this. It finds the transform that satisfies
// the format signature at offset 0, scores poorly on the rest of the sample,
// and the archive is written off as needing executable analysis.
//
// The recovery below is evidence-driven and fail-closed. It needs textual
// samples, because text is the only payload whose validity can be judged byte
// by byte at an arbitrary offset; the region key is then solved against those
// samples and the completed rule is re-validated against every sample,
// including the binary ones whose structure the text search never consulted.

std::size_t cp932_prefix_length(std::span<const std::uint8_t> bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const auto lead = bytes[i];
        if (lead == '\r' || lead == '\n' || lead == '\t' ||
            (lead >= 0x20 && lead < 0x7f)) { ++i; continue; }
        if (lead >= 0xa1 && lead <= 0xdf) { ++i; continue; } // half-width kana
        if ((lead >= 0x81 && lead <= 0x9f) || (lead >= 0xe0 && lead <= 0xef)) {
            if (i + 1 >= bytes.size()) return bytes.size();
            const auto trail = bytes[i + 1];
            if ((trail >= 0x40 && trail <= 0x7e) || (trail >= 0x80 && trail <= 0xfc)) {
                i += 2;
                continue;
            }
        }
        break;
    }
    return i;
}

std::size_t utf8_prefix_length(std::span<const std::uint8_t> bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const auto lead = bytes[i];
        if (lead == '\r' || lead == '\n' || lead == '\t' ||
            (lead >= 0x20 && lead < 0x7f)) { ++i; continue; }
        std::size_t width = 0;
        if ((lead & 0xe0) == 0xc0 && (lead & 0x1f) >= 2) width = 2;
        else if ((lead & 0xf0) == 0xe0) width = 3;
        else if ((lead & 0xf8) == 0xf0) width = 4;
        if (!width) break;
        if (i + width > bytes.size()) return bytes.size(); // truncated, not wrong
        bool valid = true;
        for (std::size_t n = 1; n < width; ++n)
            valid &= (bytes[i + n] & 0xc0) == 0x80;
        if (!valid) break;
        i += width;
    }
    return i;
}

std::size_t utf16le_prefix_length(std::span<const std::uint8_t> bytes) {
    std::size_t i = 0;
    while (i + 1 < bytes.size()) {
        const auto high = bytes[i + 1];
        const bool plausible = high == 0x00 ||
            (high >= 0x30 && high <= 0x9f) || high == 0xff;
        if (!plausible) break;
        i += 2;
    }
    return i;
}

// How far into `bytes` the content remains readable text. Retail archives mix
// CP932, UTF-8 and UTF-16LE freely, sometimes within one game, so take the
// most favourable reading rather than assuming the encoding.
std::size_t text_prefix_length(std::span<const std::uint8_t> bytes) {
    if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
        return 2 + utf16le_prefix_length(bytes.subspan(2));
    return std::max(cp932_prefix_length(bytes), utf8_prefix_length(bytes));
}

// Byte-range validity alone is a weak test. CP932 accepts 0xa1-0xdf outright
// and treats most of 0x81-0xef as lead bytes, so ASCII shifted by a constant
// lands almost entirely inside "valid" territory -- which is exactly what a
// wrong key produces from a script. Retail scripts are a mix of ASCII syntax
// and Japanese literals and always carry plenty of the former, so requiring
// some ASCII separates a real decode from a plausible-looking one.
double ascii_ratio(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return 0.0;
    std::size_t ascii = 0;
    for (const auto byte : bytes)
        if (byte == '\r' || byte == '\n' || byte == '\t' ||
            (byte >= 0x20 && byte < 0x7f)) ++ascii;
    return static_cast<double>(ascii) / static_cast<double>(bytes.size());
}

constexpr double kMinimumScriptAsciiRatio = 0.25;

bool is_text_extension(std::string_view filename) {
    const auto extension = lower_extension(filename);
    return extension == ".tjs" || extension == ".ks" || extension == ".scn" ||
           extension == ".script" || extension == ".ini" || extension == ".csv" ||
           extension == ".asd" || extension == ".sli" || extension == ".json" ||
           extension == ".xml" || extension == ".txt";
}

// PNG chunk checksum: reflected, 0xedb88320, pre/post inverted.
std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1) ? (value >> 1) ^ 0xedb88320u : value >> 1;
            values[i] = value;
        }
        return values;
    }();
    std::uint32_t value = 0xffffffffu;
    for (const auto byte : bytes) value = table[(value ^ byte) & 0xff] ^ (value >> 8);
    return value ^ 0xffffffffu;
}

// Ogg page checksum: forward, 0x04c11db7, zero seed, computed with the stored
// checksum field treated as zero.
std::uint32_t ogg_page_crc(std::span<const std::uint8_t> page) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i << 24;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 0x80000000u) ? (value << 1) ^ 0x04c11db7u : value << 1;
            values[i] = value;
        }
        return values;
    }();
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < page.size(); ++i) {
        const std::uint8_t byte = (i >= 22 && i < 26) ? 0 : page[i];
        value = (value << 8) ^ table[((value >> 24) ^ byte) & 0xff];
    }
    return value;
}

// How much of a decoded sample actually agrees with its declared format,
// beyond the signature bytes at the front.
//
// A signature is cheap to satisfy: any rule that happens to be right for the
// first few bytes reproduces it. Checking chunk and page checksums, or how far
// a script stays readable, is what separates a transform that decodes the file
// from one that only decodes its header. Returns nothing when the sample
// cannot express an opinion, so an unverifiable payload never argues against a
// correct rule.
std::optional<double> decoded_agreement(std::string_view filename,
                                        std::span<const std::uint8_t> decoded) {
    if (decoded.size() < 32) return std::nullopt;
    const auto size = static_cast<double>(decoded.size());

    if (begins(decoded, {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a})) {
        std::size_t at = 8;
        while (at + 12 <= decoded.size()) {
            // A sample is a bounded prefix of the file, so a chunk that runs
            // past its end is the usual case for image data, not a defect.
            const std::size_t length = read_be32(decoded, at);
            if (at + 12 + length > decoded.size()) break; // truncated sample
            const auto stored = read_be32(decoded, at + 8 + length);
            if (crc32(decoded.subspan(at + 4, length + 4)) != stored)
                return static_cast<double>(at) / size;
            const bool end = std::equal(decoded.begin() + static_cast<std::ptrdiff_t>(at) + 4,
                                        decoded.begin() + static_cast<std::ptrdiff_t>(at) + 8,
                                        "IEND");
            at += 12 + length;
            if (end) break;
        }
        return 1.0;
    }

    if (begins(decoded, {'O', 'g', 'g', 'S'})) {
        std::size_t at = 0;
        while (at + 27 <= decoded.size()) {
            if (!std::equal(decoded.begin() + static_cast<std::ptrdiff_t>(at),
                            decoded.begin() + static_cast<std::ptrdiff_t>(at) + 4, "OggS"))
                return static_cast<double>(at) / size;
            const std::size_t segments = decoded[at + 26];
            if (at + 27 + segments > decoded.size()) break;
            std::size_t body = 0;
            for (std::size_t i = 0; i < segments; ++i) body += decoded[at + 27 + i];
            const auto total = 27 + segments + body;
            if (at + total > decoded.size()) break; // truncated sample
            if (ogg_page_crc(decoded.subspan(at, total)) !=
                read_le32(decoded, at + 22))
                return static_cast<double>(at) / size;
            at += total;
        }
        return 1.0;
    }

    if (!is_text_extension(filename)) return std::nullopt;
    // A textual extension routinely carries compiled or compressed script
    // containers. Those are binary by design and say nothing about the rule.
    if (begins(decoded, {'T', 'J', 'S', '2', '1', '0', '0', 0}) ||
        begins(decoded, {0xfe, 0xfe, 0x00}) || begins(decoded, {0xfe, 0xfe, 0x01}) ||
        begins(decoded, {0xfe, 0xfe, 0x02})) return std::nullopt;
    const auto readable = text_prefix_length(decoded);
    // Only a payload that begins as readable text is making a claim this can
    // check. A textual extension over binary content is common enough in
    // retail archives that judging it would reject correct rules.
    if (readable < 32) return std::nullopt;
    return static_cast<double>(readable) / size;
}

// Penalty in the same units as score_plaintext. A rule that leaves most of a
// verifiable sample unexplained must not outrank one that decodes all of it.
int decode_disagreement_penalty(std::string_view filename,
                                std::span<const std::uint8_t> decoded) {
    const auto agreement = decoded_agreement(filename, decoded);
    if (!agreement) return 0;
    const auto missing = 1.0 - std::clamp(*agreement, 0.0, 1.0);
    return static_cast<int>(missing * 200.0);
}

// Every transform a segment may carry. See is_segment_primitive().
std::vector<FilterRule> segment_primitives() {
    std::vector<FilterRule> primitives;
    primitives.emplace_back(FilterOperation::Identity);
    for (unsigned value = 1; value < 256; ++value) {
        FilterRule xor_rule{FilterOperation::XorConstant};
        xor_rule.constant = static_cast<std::uint8_t>(value);
        primitives.push_back(std::move(xor_rule));
        FilterRule sub_rule{FilterOperation::SubConstant};
        sub_rule.constant = static_cast<std::uint8_t>(value);
        primitives.push_back(std::move(sub_rule));
    }
    for (unsigned multiplier = 1; multiplier < 256; ++multiplier) {
        FilterRule xor_rule{FilterOperation::XorHashMultiply};
        xor_rule.multiplier = static_cast<std::uint8_t>(multiplier);
        primitives.push_back(std::move(xor_rule));
        FilterRule sub_rule{FilterOperation::SubHashMultiply};
        sub_rule.multiplier = static_cast<std::uint8_t>(multiplier);
        primitives.push_back(std::move(sub_rule));
    }
    return primitives;
}

bool same_segment_transform(const FilterRule& left, const FilterRule& right) {
    if (left.operation != right.operation) return false;
    return left.constant == right.constant && left.multiplier == right.multiplier;
}

// Longest textual prefix produced by decoding `sample` with `rule`. The search
// below only ever asks about the region it is currently solving, so decoding
// stops at `limit` instead of walking the whole sample every time.
std::size_t decoded_text_length(const FilterRule& rule, const FilterSample& sample,
                                std::size_t limit = std::numeric_limits<std::size_t>::max()) {
    std::vector<std::uint8_t> decoded(
        sample.bytes.begin(),
        sample.bytes.begin() + static_cast<std::ptrdiff_t>(
            std::min(limit, sample.bytes.size())));
    rule.apply(sample.hash, sample.offset, decoded, sample.filename);
    return text_prefix_length(decoded);
}

constexpr std::size_t kMaxSegments = 8;
constexpr std::size_t kMinimumStride = 16;
constexpr std::size_t kStrideSearchWindow = 256;

std::optional<FilterRule> discover_segments(const std::vector<FilterSample>& samples,
                                            const FilterRule& base) {
    if (!is_segment_primitive(base.operation) || !is_unbounded(base)) return std::nullopt;

    // Textual samples that the base rule already decodes correctly at the
    // start. Anything else cannot tell us where the first region ends.
    std::vector<const FilterSample*> probes;
    for (const auto& sample : samples) {
        if (!is_text_extension(sample.filename)) continue;
        if (sample.offset != 0) continue;
        const auto readable = decoded_text_length(base, sample);
        if (readable >= 32 && readable < sample.bytes.size()) probes.push_back(&sample);
    }
    if (probes.size() < 2) return std::nullopt;

    std::size_t stride_hint = std::numeric_limits<std::size_t>::max();
    for (const auto* probe : probes)
        stride_hint = std::min(stride_hint, decoded_text_length(base, *probe));
    if (stride_hint < kMinimumStride) return std::nullopt;

    const auto primitives = segment_primitives();
    const std::size_t floor_stride =
        stride_hint > kStrideSearchWindow + kMinimumStride
            ? stride_hint - kStrideSearchWindow : kMinimumStride;

    for (std::size_t stride = stride_hint; stride >= floor_stride; --stride) {
        std::vector<std::shared_ptr<FilterRule>> segments;
        auto head = std::make_shared<FilterRule>(base);
        head->end_offset = stride;
        segments.push_back(head);

        bool converged = false;
        bool failed = false;
        for (std::size_t index = 1; index < kMaxSegments && !converged; ++index) {
            const auto window_start = stride * index;
            const auto window_end = window_start + stride;
            // Stop extending once no probe still has bytes to constrain the
            // window; an unterminated sequence is rejected below.
            const bool constrained = std::any_of(
                probes.begin(), probes.end(), [&](const auto* probe) {
                    return probe->bytes.size() >= window_end;
                });
            if (!constrained) { failed = true; break; }

            // Rank surviving transforms by how much like script the region
            // reads, not merely by whether its bytes fall in valid ranges.
            // ASCII shifted by a wrong key stays "valid" CP932 while carrying
            // almost no ASCII, so the true transform stands well clear.
            const FilterRule* solved = nullptr;
            int solved_cost = std::numeric_limits<int>::max();
            double best_ratio = -1.0;
            for (const auto& candidate : primitives) {
                auto trial = candidate;
                trial.start_offset = window_start;
                trial.end_offset = window_end;
                FilterRule composed;
                composed.segments = segments;
                composed.segments.push_back(std::make_shared<FilterRule>(trial));
                bool all_valid = true;
                double ratio_total = 0.0;
                std::size_t rated = 0;
                for (const auto* probe : probes) {
                    const auto required = std::min(window_end, probe->bytes.size());
                    if (required <= window_start) continue;
                    if (decoded_text_length(composed, *probe, required) < required) {
                        all_valid = false;
                        break;
                    }
                    std::vector<std::uint8_t> window(
                        probe->bytes.begin() +
                            static_cast<std::ptrdiff_t>(window_start),
                        probe->bytes.begin() +
                            static_cast<std::ptrdiff_t>(required));
                    composed.apply(probe->hash, probe->offset + window_start,
                                   window, probe->filename);
                    ratio_total += ascii_ratio(window);
                    ++rated;
                }
                if (!all_valid || !rated) continue;
                const auto ratio = ratio_total / static_cast<double>(rated);
                const auto cost = description_cost(trial);
                // Treat near-equal readability as a tie and let the simpler
                // transform win, so an accidental fractional edge cannot
                // select a more complicated rule.
                if (ratio > best_ratio + 0.02 ||
                    (ratio > best_ratio - 0.02 && cost < solved_cost)) {
                    best_ratio = std::max(best_ratio, ratio);
                    solved_cost = cost;
                    solved = &candidate;
                }
            }
            if (!solved || best_ratio < kMinimumScriptAsciiRatio) {
                failed = true;
                break;
            }

            // A region that repeats the previous transform means the filter
            // has settled: the previous segment covers the rest of the file.
            if (same_segment_transform(*solved, *segments.back())) {
                segments.back() = std::make_shared<FilterRule>(*segments.back());
                segments.back()->end_offset = std::numeric_limits<std::uint64_t>::max();
                converged = true;
                break;
            }
            auto next = std::make_shared<FilterRule>(*solved);
            next->start_offset = window_start;
            next->end_offset = window_end;
            segments.push_back(std::move(next));
        }
        if (failed || !converged || segments.size() < 2) continue;

        FilterRule composed;
        composed.segments = std::move(segments);

        // The per-region search only ever looked as far as the region it was
        // solving, and a region boundary that is close but wrong can survive
        // that. Require the finished rule to decode each probe from the first
        // byte to the last, as readable script rather than merely as bytes in
        // valid ranges. A stride that is off by even one fails here, and the
        // loop moves on to the next one.
        bool complete = true;
        for (const auto* probe : probes) {
            const auto readable = decoded_text_length(composed, *probe);
            if (readable < probe->bytes.size()) { complete = false; break; }
            auto decoded = probe->bytes;
            composed.apply(probe->hash, probe->offset, decoded, probe->filename);
            if (ascii_ratio(decoded) < kMinimumScriptAsciiRatio) {
                complete = false;
                break;
            }
        }
        if (!complete) continue;
        return composed;
    }
    return std::nullopt;
}

} // namespace

std::string FilterRule::name() const {
    if (!branches.empty()) {
        std::ostringstream compound;
        compound << "by_extension";
        for (const auto& branch : branches) {
            compound << '_';
            for (const auto& extension : branch.extensions) compound << extension.substr(1);
            for (const auto& prefix : branch.path_prefixes) {
                compound << "path";
                for (const auto c : prefix)
                    compound << (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
            }
            compound << '_' << (branch.rule ? branch.rule->name() : "identity");
        }
        return compound.str();
    }
    if (!segments.empty()) {
        std::ostringstream composed;
        composed << "segmented";
        for (const auto& segment : segments) {
            composed << '_';
            if (!segment) { composed << "identity"; continue; }
            composed << segment->name();
        }
        return composed.str();
    }
    std::ostringstream out;
    switch (operation) {
    case FilterOperation::Identity: out << "identity"; break;
    case FilterOperation::XorHash: out << "xor_hash"; break;
    case FilterOperation::XorHashShift3: out << "xor_hash_rshift_3"; break;
    case FilterOperation::XorHashShift5: out << "xor_hash_rshift_5"; break;
    case FilterOperation::XorNotHashPlus1: out << "xor_not_hash_plus_1"; break;
    case FilterOperation::XorConstant: out << "xor_constant_" << hex_byte(constant); break;
    case FilterOperation::XorHashShift: out << "xor_hash_rshift_" << unsigned(shift); break;
    case FilterOperation::XorNotHashShift: out << "xor_not_hash_rshift_" << unsigned(shift); break;
    case FilterOperation::XorHashShiftConstant:
        out << "xor_hash_rshift_" << unsigned(shift) << "_xor_" << hex_byte(constant); break;
    case FilterOperation::XorHashFoldConstant:
        out << "xor_hash_fold_" << unsigned(modulus) << "_xor_" << hex_byte(constant); break;
    case FilterOperation::XorPeriodic:
        out << "xor_periodic_" << table.size(); break;
    case FilterOperation::XorOffsetAddConstant:
        out << "xor_offset_plus_" << hex_byte(constant); break;
    case FilterOperation::XorHashOffsetParity: out << "xor_hash_offset_parity"; break;
    case FilterOperation::XorHashShiftByOffset:
        out << "xor_hash_shift_by_offset_mod_" << unsigned(modulus); break;
    case FilterOperation::XorHashByteLanes:
        out << "xor_hash_lane_period_" << table.size(); break;
    case FilterOperation::XorRotatingHash:
        out << "xor_rotating_hash_mode_" << unsigned(secondary)
            << "_period_" << unsigned(modulus) << "_seed_" << seed_xor; break;
    case FilterOperation::XorLcgHash: out << "xor_lcg_hash_512"; break;
    case FilterOperation::XorThenAdd:
        out << "xor_" << hex_byte(constant) << "_add_" << hex_byte(post_add); break;
    case FilterOperation::XorThenNibbleSwap:
        out << "xor_" << hex_byte(constant) << "_nibble_swap"; break;
    case FilterOperation::RotateLeftByPopcount: out << "rotate_left_by_popcount"; break;
    case FilterOperation::SubConstant: out << "sub_constant_" << hex_byte(constant); break;
    case FilterOperation::XorHashMultiply:
        out << "xor_hash_times_" << unsigned(multiplier); break;
    case FilterOperation::SubHashMultiply:
        out << "sub_hash_times_" << unsigned(multiplier); break;
    }
    if (start_offset) out << "_from_" << start_offset;
    if (end_offset != std::numeric_limits<std::uint64_t>::max()) out << "_to_" << end_offset;
    if (!table.empty() && operation == FilterOperation::XorPeriodic) {
        out << '_';
        for (auto value : table) out << hex_byte(value).substr(2);
    }
    return out.str();
}

std::string FilterRule::to_tjs() const {
    if (!branches.empty()) {
        std::ostringstream compound;
        compound << "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l,f){var n=f.toLowerCase();";
        for (const auto& branch : branches) {
            if (!branch.rule || (branch.extensions.empty() && branch.path_prefixes.empty()))
                continue;
            compound << "if(";
            if (!branch.extensions.empty()) {
                compound << '(';
                for (std::size_t i = 0; i < branch.extensions.size(); ++i) {
                    if (i) compound << "||";
                    const auto& extension = branch.extensions[i];
                    compound << "n.substr(n.length-" << extension.size() << ")=='"
                             << extension << "'";
                }
                compound << ')';
            }
            if (!branch.extensions.empty() && !branch.path_prefixes.empty()) compound << "&&";
            if (!branch.path_prefixes.empty()) {
                compound << '(';
                for (std::size_t i = 0; i < branch.path_prefixes.size(); ++i) {
                    if (i) compound << "||";
                    const auto& prefix = branch.path_prefixes[i];
                    compound << "n.substr(0," << prefix.size() << ")=='" << prefix << "'";
                }
                compound << ')';
            }
            compound << "){";
            const auto script = branch.rule->to_tjs();
            const auto body_begin = script.find('{');
            const auto body_end = script.rfind("});");
            if (body_begin != std::string::npos && body_end != std::string::npos &&
                body_end > body_begin) {
                compound << script.substr(body_begin + 1, body_end - body_begin - 1);
            }
            compound << "return;}";
        }
        compound << "});\n";
        return compound.str();
    }
    if (!segments.empty()) {
        std::ostringstream composed;
        composed << "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){"
                 << "for(var i=0;i<l;++i){var p=o+i;";
        bool first = true;
        for (const auto& segment : segments) {
            if (!segment) continue;
            const auto statement = segment_byte_statement(*segment);
            const bool bounded =
                segment->end_offset != std::numeric_limits<std::uint64_t>::max();
            if (bounded) {
                composed << (first ? "if(" : "else if(") << "p<" << segment->end_offset
                         << "){" << statement << '}';
            } else {
                composed << (first ? "{" : "else{") << statement << '}';
            }
            first = false;
        }
        composed << "}});\n";
        return composed.str();
    }
    if (operation == FilterOperation::Identity) {
        return "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){});\n";
    }
    if (!start_offset && end_offset == std::numeric_limits<std::uint64_t>::max()) {
        if (operation == FilterOperation::XorHash)
            return "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,h);});\n";
        if (operation == FilterOperation::XorHashShift3)
            return "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,h>>>3);});\n";
        if (operation == FilterOperation::XorHashShift5)
            return "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,h>>>5);});\n";
        if (operation == FilterOperation::XorNotHashPlus1)
            return "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,~(h+1));});\n";
        if (operation == FilterOperation::XorConstant) {
            std::ostringstream simple;
            simple << "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,"
                   << unsigned(constant) << ");});\n";
            return simple.str();
        }
    }
    std::ostringstream out;
    out << "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){";
    if (operation == FilterOperation::XorRotatingHash) {
        out << "var q=(h^" << seed_xor << ")&0x7fffffff;q|=q<<31;var t=[];"
            << "for(var j=0;j<" << unsigned(modulus)
            << ";++j){t[j]=q;q=";
        if (secondary == 1) out << "((q&0x1ff)<<23)|(q>>>8);}";
        else out << "(q<<23)|(q>>>8);}";
    }
    if (operation == FilterOperation::XorLcgHash) {
        out << "var q=0x15a4e35*h+1;q&=0xffffffff;var t=[];"
            << "for(var j=0;j<512;++j){q=0x15a4e35*q+1;q&=0xffffffff;t[j]=q>>>16;}";
    }
    if (operation == FilterOperation::XorPeriodic ||
        operation == FilterOperation::XorHashByteLanes) {
        out << "var t=[";
        for (std::size_t i = 0; i < table.size(); ++i) {
            if (i) out << ',';
            out << unsigned(table[i]);
        }
        out << "];";
    }
    out << "for(var i=0;i<l;++i){var p=o+i;";
    if (start_offset) out << "if(p<" << start_offset << ")continue;";
    if (end_offset != std::numeric_limits<std::uint64_t>::max())
        out << "if(p>=" << end_offset << ")continue;";
    switch (operation) {
    case FilterOperation::XorHash: out << "b[i]^=h;"; break;
    case FilterOperation::XorHashShift3: out << "b[i]^=h>>>3;"; break;
    case FilterOperation::XorHashShift5: out << "b[i]^=h>>>5;"; break;
    case FilterOperation::XorNotHashPlus1: out << "b[i]^=~(h+1);"; break;
    case FilterOperation::XorConstant: out << "b[i]^=" << unsigned(constant) << ';'; break;
    case FilterOperation::XorHashShift: out << "b[i]^=h>>>" << unsigned(shift) << ';'; break;
    case FilterOperation::XorNotHashShift: out << "b[i]^=~(h>>>" << unsigned(shift) << ");"; break;
    case FilterOperation::XorHashShiftConstant:
        out << "b[i]^=(h>>>" << unsigned(shift) << ")^" << unsigned(constant) << ';'; break;
    case FilterOperation::XorHashFoldConstant: {
        out << "var k=" << unsigned(constant);
        for (unsigned lane = 0; lane < 4; ++lane)
            if (modulus & (1u << lane)) out << "^(h>>>" << lane * 8 << ')';
        out << ";b[i]^=k;";
        break;
    }
    case FilterOperation::XorPeriodic: out << "b[i]^=t[p%" << table.size() << "];"; break;
    case FilterOperation::XorOffsetAddConstant:
        out << "b[i]^=p+" << unsigned(constant) << ';'; break;
    case FilterOperation::XorHashOffsetParity: out << "b[i]^=(p&1)?p:h;"; break;
    case FilterOperation::XorHashShiftByOffset:
        out << "b[i]^=h>>>(p%" << unsigned(modulus) << ");"; break;
    case FilterOperation::XorHashByteLanes:
        out << "b[i]^=h>>>t[p%" << table.size() << "];"; break;
    case FilterOperation::XorRotatingHash:
        out << "b[i]^=t[p%" << unsigned(modulus) << "];"; break;
    case FilterOperation::XorLcgHash: out << "b[i]^=t[p&511];"; break;
    case FilterOperation::XorThenAdd:
        out << "b[i]^=" << unsigned(constant) << ";b[i]+=" << unsigned(post_add) << ';'; break;
    case FilterOperation::XorThenNibbleSwap:
        out << "b[i]^=" << unsigned(constant) << ";b[i]=(b[i]>>>4)|(b[i]<<4);"; break;
    case FilterOperation::RotateLeftByPopcount:
        out << "var c=b[i],n=c,r=0;while(n){r+=n&1;n>>>=1;}r&=7;if(r)b[i]=(c<<r)|(c>>>(8-r));"; break;
    case FilterOperation::SubConstant:
    case FilterOperation::XorHashMultiply:
    case FilterOperation::SubHashMultiply:
        out << segment_byte_statement(*this); break;
    case FilterOperation::Identity: break;
    }
    out << "}});\n";
    return out.str();
}

void FilterRule::apply(std::uint32_t hash, std::uint64_t offset,
                       std::span<std::uint8_t> bytes,
                       std::string_view filename) const {
    if (!branches.empty()) {
        const auto extension = lower_extension(filename);
        const auto normalized = normalized_filename(filename);
        for (const auto& branch : branches) {
            const bool extension_match = branch.extensions.empty() ||
                std::find(branch.extensions.begin(), branch.extensions.end(), extension) !=
                    branch.extensions.end();
            const bool prefix_match = branch.path_prefixes.empty() ||
                std::any_of(branch.path_prefixes.begin(), branch.path_prefixes.end(),
                            [&](const auto& prefix) { return normalized.starts_with(prefix); });
            if (extension_match && prefix_match) {
                if (branch.rule) branch.rule->apply(hash, offset, bytes, filename);
                return;
            }
        }
        return;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        transform_byte(*this, hash, offset + i, bytes[i]);
    }
}

int FilterHeuristic::score_plaintext(std::string_view filename,
                                     std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return 0;
    int score = 0;
    std::string detected;
    bool binary_format = false;
    if (begins(bytes, {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a})) {
        score += 140 + png_structure_score(bytes); detected = ".png"; binary_format = true;
    } else if (begins(bytes, {0xff, 0xd8, 0xff})) {
        score += 120;
        if (bytes.size() >= 4 && bytes[3] >= 0xc0) score += 15;
        detected = ".jpg"; binary_format = true;
    } else if (begins(bytes, {'O', 'g', 'g', 'S'})) {
        score += 140;
        if (bytes.size() >= 27 && bytes[4] == 0 && bytes[26] != 0) score += 30;
        detected = ".ogg"; binary_format = true;
    } else if (begins(bytes, {'R', 'I', 'F', 'F'})) {
        score += 110;
        if (bytes.size() >= 16 && (begins(bytes.subspan(8), {'W','A','V','E'}) ||
                                  begins(bytes.subspan(8), {'W','E','B','P'}))) score += 45;
        detected = begins(bytes.subspan(8), {'W','E','B','P'}) ? ".webp" : ".wav";
        binary_format = true;
    } else if (begins(bytes, {'B', 'M'})) {
        score += 100; detected = ".bmp"; binary_format = true;
    } else if (begins(bytes, {'I', 'D', '3'})) {
        score += 90; detected = ".mp3"; binary_format = true;
    } else if (bytes.size() >= 2 && bytes[0] == 0xff &&
               (bytes[1] & 0xe0) == 0xe0) {
        score += 90; detected = ".mp3"; binary_format = true;
    } else if (begins(bytes, {0x00, 0x00, 0x01, 0xba}) ||
               begins(bytes, {0x00, 0x00, 0x01, 0xb3})) {
        score += 120; detected = ".mpg"; binary_format = true;
    } else if (begins(bytes, {0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
                              0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c})) {
        score += 150; detected = ".wmv"; binary_format = true;
    } else if (begins(bytes, {'P', 'S', 'B', 0x00})) {
        score += 130; detected = ".psb"; binary_format = true;
    } else if (begins(bytes, {'T', 'L', 'G', '5', '.', '0'}) ||
               begins(bytes, {'T', 'L', 'G', '6', '.', '0'}) ||
               begins(bytes, {'T', 'L', 'G', '0', '.', '0'})) {
        score += 140; detected = ".tlg"; binary_format = true;
    } else if (bytes.size() >= 20 &&
               begins(bytes, {'T', 'J', 'S', '2', '1', '0', '0', 0}) &&
               read_le32(bytes, 8) >= 20 && read_le32(bytes, 8) <= bytes.size() &&
               begins(bytes.subspan(12), {'D', 'A', 'T', 'A'})) {
        // KiriKiri 2 compiled-script container.  Its bytecode is deliberately
        // non-textual; judging it by printable-byte density makes a valid
        // plaintext bootstrap look encrypted and can select a bogus rule.
        score += 150; detected = ".tjs"; binary_format = true;
    } else if (begins(bytes, {0xfe, 0xfe, 0x00}) || begins(bytes, {0xfe, 0xfe, 0x01}) ||
               begins(bytes, {0xfe, 0xfe, 0x02})) {
        score += 120; detected = ".tjs"; binary_format = true;
    } else if (begins(bytes, {0xff, 0xfe}) || begins(bytes, {0xef, 0xbb, 0xbf})) {
        score += 65;
    }

    const auto expected = lower_extension(filename);
    if (!detected.empty()) {
        if (expected == detected || (expected == ".jpeg" && detected == ".jpg") ||
            (expected == ".ks" && detected == ".tjs") ||
            (expected == ".mpeg" && detected == ".mpg")) score += 35;
        else if (!expected.empty()) score -= 30;
    }

    const auto inspected = std::min<std::size_t>(bytes.size(), 2048);
    int printable = 0;
    int control = 0;
    int utf16_pairs = 0;
    int shift_jis_bytes = 0;
    for (std::size_t i = 0; i < inspected; ++i) {
        const auto c = bytes[i];
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7f)) ++printable;
        else if (c < 0x20 && c != 0) ++control;
        if (i + 1 < inspected && bytes[i + 1] == 0 && c >= 0x20 && c < 0x7f) ++utf16_pairs;
        if (c >= 0xa1 && c <= 0xdf) {
            ++shift_jis_bytes;
        } else if ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xef)) {
            if (i + 1 < inspected) {
                const auto trail = bytes[i + 1];
                if ((trail >= 0x40 && trail <= 0x7e) ||
                    (trail >= 0x80 && trail <= 0xfc)) {
                    shift_jis_bytes += 2;
                    ++i;
                }
            }
        }
    }
    if (inspected >= 16 && printable * 100 / static_cast<int>(inspected) > 80) score += 35;
    if (inspected >= 16 && utf16_pairs * 2 * 100 / static_cast<int>(inspected) > 60) score += 40;
    const bool text_extension = expected == ".tjs" || expected == ".ks" ||
        expected == ".asd" || expected == ".sli" || expected == ".json" ||
        expected == ".xml" ||
        expected == ".scn" || expected == ".script" || expected == ".ini" ||
        expected == ".csv";
    if (text_extension && inspected >= 16 &&
        (printable + shift_jis_bytes) * 100 / static_cast<int>(inspected) > 82) score += 90;
    // Entropy/control-byte penalties distinguish plaintext scripts from a
    // wrong transform, but are meaningless once a binary format's structural
    // signature has already validated. In particular, compiled TJS bytecode
    // naturally contains enough control bytes to erase its otherwise strong
    // FE FE 00/01/02 evidence.
    if (!binary_format) score -= std::min(control * 2, 80);
    return score;
}

FilterInferenceResult FilterHeuristic::analyze(const std::vector<FilterSample>& samples) {
    const auto analyze_single = [](const std::vector<FilterSample>& subset) {
        FilterInferenceResult result;
        for (const auto& sample : subset) if (has_format_constraints(sample.filename))
            ++result.constrained_samples;
        if (subset.empty()) {
            result.reason = "archive contains no candidate samples";
            return result;
        }

        int raw_score = 0;
        std::size_t raw_recognized = 0;
        for (const auto& sample : subset) {
            const auto score = FilterHeuristic::score_plaintext(sample.filename, sample.bytes) -
                decode_disagreement_penalty(sample.filename, sample.bytes);
            raw_score += score;
            if (score >= 80) ++raw_recognized;
        }
        const auto required_recognized = std::max<std::size_t>(
            std::min<std::size_t>(2, subset.size()), (subset.size() * 3 + 3) / 4);
        if (raw_recognized >= required_recognized &&
            raw_score >= static_cast<int>(subset.size()) * 40) {
            FilterRule identity{FilterOperation::Identity};
            identity.score = raw_score;
            identity.confidence = raw_score;
            result.disposition = FilterInferenceDisposition::Detected;
            result.reason = "archive samples already pass plaintext validation";
            result.rule = std::move(identity);
            return result;
        }

        auto rules = build_candidates(subset);
        discard_unproven_range_variants(rules);
        // The disagreement penalty settles which *complete* rule wins, but it
        // deliberately punishes a transform that only explains a file's head --
        // which is exactly the transform a segmented filter's first region is.
        // Keep the unpenalised ranking too, so segment discovery below starts
        // from the head transforms rather than from whatever scored best once
        // they were pushed down.
        std::vector<std::pair<int, const FilterRule*>> by_signature_score;
        by_signature_score.reserve(rules.size());
        for (auto& rule : rules) {
            int recognized = 0;
            int unpenalised = 0;
            for (const auto& sample : subset) {
                auto decoded = sample.bytes;
                rule.apply(sample.hash, sample.offset, decoded, sample.filename);
                const auto score = FilterHeuristic::score_plaintext(sample.filename, decoded);
                unpenalised += score;
                rule.score += score - decode_disagreement_penalty(sample.filename, decoded);
                if (score >= 80) ++recognized;
            }
            if (subset.size() >= 2 && recognized < 2) rule.score -= 160;
            rule.score -= description_cost(rule) * 10;
            by_signature_score.emplace_back(unpenalised - description_cost(rule) * 10,
                                            &rule);
        }
        std::stable_sort(by_signature_score.begin(), by_signature_score.end(),
                         [](const auto& a, const auto& b) {
                             return a.first > b.first;
                         });
        std::vector<FilterRule> segment_seeds;
        for (const auto& [score, rule] : by_signature_score) {
            if (segment_seeds.size() >= 6) break;
            if (!is_unbounded(*rule) || !is_segment_primitive(rule->operation))
                continue;
            segment_seeds.push_back(*rule);
        }
        std::sort(rules.begin(), rules.end(), [](const auto& a, const auto& b) {
            if (a.score != b.score) return a.score > b.score;
            return description_cost(a) < description_cost(b);
        });
        if (!rules.empty()) {
            int runner_up = 0;
            const bool preferred_is_unbounded = rules[0].start_offset == 0 &&
                rules[0].end_offset == std::numeric_limits<std::uint64_t>::max();
            const auto competitor = std::find_if(
                rules.begin() + 1, rules.end(), [&](const auto& candidate) {
                    // A bounded copy of the same transform cannot invalidate a
                    // simpler unbounded winner when all observed constraints
                    // lie before its artificial boundary. If the winner is
                    // itself ranged, however, competing endpoints remain real
                    // ambiguity and must count against confidence.
                    return !preferred_is_unbounded ||
                           !same_core_transform(rules[0], candidate);
                });
            if (competitor != rules.end()) runner_up = competitor->score;
            rules[0].confidence = rules[0].score - runner_up;
            const int minimum = static_cast<int>(subset.size()) * 65;
            const int margin = subset.size() == 1 ? 30 : static_cast<int>(subset.size()) * 10;
            if (rules[0].score >= minimum && rules[0].confidence >= margin) {
                result.disposition = FilterInferenceDisposition::Detected;
                result.reason = "archive-known plaintext uniquely validates a synthesized rule";
                result.rule = std::move(rules[0]);
                return result;
            }
        }

        // No single whole-file transform explains the archive. Before writing
        // that off as needing the executable, test whether the filter simply
        // changes key partway through the file. The head transform is one of
        // the candidates already proven against every known signature byte.
        {
            const int minimum = static_cast<int>(subset.size()) * 65;
            const auto required_margin =
                subset.size() == 1 ? 30 : static_cast<int>(subset.size()) * 10;
            for (const auto& seed : segment_seeds) {
                auto segmented = discover_segments(subset, seed);
                if (!segmented) continue;
                int score = 0;
                std::size_t recognized = 0;
                for (const auto& sample : subset) {
                    auto decoded = sample.bytes;
                    segmented->apply(sample.hash, sample.offset, decoded, sample.filename);
                    const auto sample_score =
                        FilterHeuristic::score_plaintext(sample.filename, decoded);
                    score += sample_score -
                        decode_disagreement_penalty(sample.filename, decoded);
                    if (sample_score >= 80) ++recognized;
                }
                segmented->score = score - description_cost(*segmented) * 10;
                // The segmented hypothesis is larger than everything it beat,
                // so it has to win outright rather than merely tie.
                segmented->confidence =
                    segmented->score - (rules.empty() ? 0 : rules[0].score);
                if (recognized < std::max<std::size_t>(2, (subset.size() * 3 + 3) / 4))
                    continue;
                if (segmented->score < minimum || segmented->confidence < required_margin)
                    continue;
                result.disposition = FilterInferenceDisposition::Detected;
                result.reason = "archive samples validate a rule whose key changes at a "
                                "fixed offset stride";
                result.rule = std::move(*segmented);
                return result;
            }
        }

        if (result.constrained_samples >= 2) {
            result.disposition = FilterInferenceDisposition::RequiresExecutableAnalysis;
            std::ostringstream reason;
            std::set<std::uint8_t> file_keys;
            std::size_t keyed_samples = 0;
            for (const auto& sample : subset) {
                if (const auto key = sample_uniform_xor_key(sample)) {
                    file_keys.insert(*key);
                    ++keyed_samples;
                }
            }
            if (keyed_samples >= 2 && file_keys.size() >= 2) {
                reason << "files use internally uniform but mutually different keys; "
                          "the per-hash mapping or key function is not identifiable from archive data";
            } else {
                reason << "recognizable archive types are present, but no complete archive-only rule validates";
            }
            if (!rules.empty()) {
                reason << " (best=" << rules[0].name() << ", score=" << rules[0].score
                       << ", confidence=" << rules[0].confidence << ')';
            }
            result.reason = reason.str();
        } else if (subset.size() >= 4 &&
                   std::count_if(subset.begin(), subset.end(), [](const auto& sample) {
                       return lower_extension(sample.filename).empty();
                   }) * 4 >= static_cast<std::ptrdiff_t>(subset.size()) * 3) {
            result.disposition = FilterInferenceDisposition::RequiresExecutableAnalysis;
            result.reason = "archive filenames are obfuscated, so file-type constraints must be recovered from the executable";
        } else {
            result.disposition = FilterInferenceDisposition::InsufficientArchiveEvidence;
            result.reason = "too few independently constrained archive samples";
        }
        return result;
    };

    auto result = analyze_single(samples);
    if (result.rule || samples.empty()) return result;

    const auto try_partition = [&](const auto& groups, bool paths)
        -> std::optional<FilterInferenceResult> {
        if (groups.size() < 2) return std::nullopt;
        FilterRule compound;
        compound.score = 0;
        compound.confidence = std::numeric_limits<int>::max();
        std::size_t covered_constrained = 0;
        for (const auto& [selector, group] : groups) {
            if (selector.empty()) return std::nullopt;
            auto branch_result = analyze_single(group);
            if (!branch_result.rule) {
                bool all_plain = true;
                for (const auto& sample : group)
                    all_plain &= score_plaintext(sample.filename, sample.bytes) >= 80;
                if (!all_plain) return std::nullopt;
                branch_result.rule = FilterRule{FilterOperation::Identity};
            }
            covered_constrained += branch_result.constrained_samples;
            compound.score += branch_result.rule->score;
            compound.confidence = std::min(compound.confidence,
                                           std::max(1, branch_result.rule->confidence));
            FilterBranch branch;
            if (paths) branch.path_prefixes.push_back(selector);
            else branch.extensions.push_back(selector);
            branch.rule = std::make_shared<FilterRule>(std::move(*branch_result.rule));
            compound.branches.push_back(std::move(branch));
        }
        if (covered_constrained < 2) return std::nullopt;
        compound.score -= static_cast<int>(compound.branches.size()) * 20;
        FilterInferenceResult partition_result;
        partition_result.disposition = FilterInferenceDisposition::Detected;
        partition_result.constrained_samples = covered_constrained;
        partition_result.reason = paths
            ? "independent top-level archive paths validate distinct extraction rules"
            : "independent archive extensions validate distinct extraction rules";
        partition_result.rule = std::move(compound);
        return partition_result;
    };

    std::map<std::string, std::vector<FilterSample>> extension_groups;
    for (const auto& sample : samples)
        extension_groups[lower_extension(sample.filename)].push_back(sample);
    if (auto compound = try_partition(extension_groups, false)) return *compound;

    std::map<std::string, std::vector<FilterSample>> path_groups;
    for (const auto& sample : samples)
        path_groups[top_directory(sample.filename)].push_back(sample);
    if (auto compound = try_partition(path_groups, true)) return *compound;
    return result;
}

std::optional<double> FilterHeuristic::format_agreement(
    std::string_view filename, std::span<const std::uint8_t> bytes) {
    return decoded_agreement(filename, bytes);
}

std::optional<FilterRule>
FilterHeuristic::detect(const std::vector<FilterSample>& samples) {
    return analyze(samples).rule;
}

} // namespace krkrvita

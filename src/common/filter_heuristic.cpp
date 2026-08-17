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
    case FilterOperation::XorThenAdd:
    case FilterOperation::XorThenNibbleSwap:
        return rule.constant;
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
    if (rule.operation == FilterOperation::RotateLeftByPopcount) {
        const unsigned amount = std::popcount(byte) & 7u;
        if (amount) byte = static_cast<std::uint8_t>((byte << amount) | (byte >> (8 - amount)));
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
            const auto score = FilterHeuristic::score_plaintext(sample.filename, sample.bytes);
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
        for (auto& rule : rules) {
            int recognized = 0;
            for (const auto& sample : subset) {
                auto decoded = sample.bytes;
                rule.apply(sample.hash, sample.offset, decoded, sample.filename);
                const auto score = FilterHeuristic::score_plaintext(sample.filename, decoded);
                rule.score += score;
                if (score >= 80) ++recognized;
            }
            if (subset.size() >= 2 && recognized < 2) rule.score -= 160;
            rule.score -= description_cost(rule) * 10;
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

std::optional<FilterRule>
FilterHeuristic::detect(const std::vector<FilterSample>& samples) {
    return analyze(samples).rule;
}

} // namespace krkrvita

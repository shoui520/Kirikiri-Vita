#include "krkrvita/filter_heuristic.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace krkrvita {
namespace {

std::uint8_t key_for(const FilterRule& rule, std::uint32_t hash) {
    switch (rule.operation) {
    case FilterOperation::Identity: return 0;
    case FilterOperation::XorHash: return static_cast<std::uint8_t>(hash);
    case FilterOperation::XorHashShift3: return static_cast<std::uint8_t>(hash >> 3);
    case FilterOperation::XorHashShift5: return static_cast<std::uint8_t>(hash >> 5);
    case FilterOperation::XorNotHashPlus1:
        return static_cast<std::uint8_t>(~(hash + 1));
    case FilterOperation::XorConstant: return rule.constant;
    }
    return 0;
}

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

} // namespace

std::string FilterRule::name() const {
    switch (operation) {
    case FilterOperation::Identity: return "identity";
    case FilterOperation::XorHash: return "xor_hash";
    case FilterOperation::XorHashShift3: return "xor_hash_rshift_3";
    case FilterOperation::XorHashShift5: return "xor_hash_rshift_5";
    case FilterOperation::XorNotHashPlus1: return "xor_not_hash_plus_1";
    case FilterOperation::XorConstant: {
        char value[32]{};
        std::snprintf(value, sizeof(value), "xor_constant_0x%02x", constant);
        return value;
    }
    }
    return "unknown";
}

std::string FilterRule::to_tjs() const {
    switch (operation) {
    case FilterOperation::Identity:
        return "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){});\n";
    case FilterOperation::XorHash:
        return "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,h);});\n";
    case FilterOperation::XorHashShift3:
        return "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,h>>>3);});\n";
    case FilterOperation::XorHashShift5:
        return "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,h>>>5);});\n";
    case FilterOperation::XorNotHashPlus1:
        return "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,~(h+1));});\n";
    case FilterOperation::XorConstant: {
        char script[128]{};
        std::snprintf(script, sizeof(script),
            "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,0x%02x);});\n",
            constant);
        return script;
    }
    }
    return {};
}

void FilterRule::apply(std::uint32_t hash, std::uint64_t,
                       std::span<std::uint8_t> bytes) const {
    const auto key = key_for(*this, hash);
    for (auto& byte : bytes) {
        byte ^= key;
    }
}

int FilterHeuristic::score_plaintext(std::string_view filename,
                                     std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return 0;
    int score = 0;
    std::string detected;
    if (begins(bytes, {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a})) {
        score += 140; detected = ".png";
    } else if (begins(bytes, {0xff, 0xd8, 0xff})) {
        score += 120; detected = ".jpg";
    } else if (begins(bytes, {'O', 'g', 'g', 'S'})) {
        score += 140; detected = ".ogg";
    } else if (begins(bytes, {'R', 'I', 'F', 'F'})) {
        score += 110; detected = ".wav";
    } else if (begins(bytes, {'T', 'L', 'G', '5', '.', '0'}) ||
               begins(bytes, {'T', 'L', 'G', '6', '.', '0'}) ||
               begins(bytes, {'T', 'L', 'G', '0', '.', '0'})) {
        score += 140; detected = ".tlg";
    } else if (begins(bytes, {0xfe, 0xfe, 0x00}) || begins(bytes, {0xfe, 0xfe, 0x01}) ||
               begins(bytes, {0xfe, 0xfe, 0x02})) {
        score += 120; detected = ".tjs";
    } else if (begins(bytes, {0xff, 0xfe}) || begins(bytes, {0xef, 0xbb, 0xbf})) {
        score += 65;
    }

    const auto expected = lower_extension(filename);
    if (!detected.empty()) {
        if (expected == detected || (expected == ".jpeg" && detected == ".jpg") ||
            (expected == ".ks" && detected == ".tjs")) {
            score += 35;
        } else if (!expected.empty()) {
            score -= 20;
        }
    }

    const auto inspected = std::min<std::size_t>(bytes.size(), 384);
    int printable = 0;
    int control = 0;
    int utf16_pairs = 0;
    for (std::size_t i = 0; i < inspected; ++i) {
        const auto c = bytes[i];
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7f)) ++printable;
        else if (c < 0x20 && c != 0) ++control;
        if (i + 1 < inspected && bytes[i + 1] == 0 && c >= 0x20 && c < 0x7f) {
            ++utf16_pairs;
        }
    }
    if (inspected >= 16 && printable * 100 / static_cast<int>(inspected) > 80) score += 35;
    if (inspected >= 16 && utf16_pairs * 2 * 100 / static_cast<int>(inspected) > 60) score += 40;
    score -= std::min(control * 2, 60);
    return score;
}

std::optional<FilterRule>
FilterHeuristic::detect(const std::vector<FilterSample>& samples) {
    if (samples.empty()) return std::nullopt;
    std::vector<FilterRule> rules = {
        {FilterOperation::Identity},
        {FilterOperation::XorHash},
        {FilterOperation::XorHashShift3},
        {FilterOperation::XorHashShift5},
        {FilterOperation::XorNotHashPlus1},
    };
    for (unsigned key = 1; key < 256; ++key) {
        rules.push_back({FilterOperation::XorConstant, static_cast<std::uint8_t>(key)});
    }

    for (auto& rule : rules) {
        int recognized = 0;
        for (const auto& sample : samples) {
            auto decoded = sample.bytes;
            rule.apply(sample.hash, sample.offset, decoded);
            const auto score = score_plaintext(sample.filename, decoded);
            rule.score += score;
            if (score >= 80) ++recognized;
        }
        if (samples.size() >= 2 && recognized < 2) {
            rule.score -= 100;
        }
    }
    std::sort(rules.begin(), rules.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });
    const int runner_up = rules.size() > 1 ? rules[1].score : 0;
    rules[0].confidence = rules[0].score - runner_up;
    const int minimum = static_cast<int>(samples.size()) * 65;
    const int margin = samples.size() == 1 ? 30 : static_cast<int>(samples.size()) * 18;
    if (rules[0].score < minimum || rules[0].confidence < margin) {
        return std::nullopt;
    }
    return rules[0];
}

} // namespace krkrvita


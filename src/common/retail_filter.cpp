#include "krkrvita/retail_filter.hpp"

#include "krkrvita/filter_heuristic.hpp"
#include "krkrvita/xp3_archive.hpp"
#include "krkrvita/xp3_filter_vm.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace krkrvita {
namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open filter: " + path.string());
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0 || length > 4 * 1024 * 1024) {
        throw std::runtime_error("xp3filter.tjs has an invalid size");
    }
    stream.seekg(0);
    std::string text(static_cast<std::size_t>(length), '\0');
    stream.read(text.data(), length);
    if (!stream) throw std::runtime_error("cannot read filter: " + path.string());
    return text;
}

void write_text_atomic(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot create generated filter");
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    if (!stream) throw std::runtime_error("cannot write generated filter");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::rename(temporary, path);
}

} // namespace

bool verify_retail_filter(const GameDescriptor& game,
                          const std::filesystem::path& filter_path,
                          FilterVerification* verification,
                          std::string* error) {
    try {
        Xp3FilterVm filter;
        auto script = read_text(filter_path);
        if (!filter.load(std::move(script), error)) return false;

        FilterVerification result;
        for (const auto& archive_file : game.archives) {
            auto archive = Xp3Archive::open(archive_file.path, error);
            if (!archive) continue;
            auto samples = collect_xp3_filter_samples(*archive, 12, &filter, error);
            if (samples.empty()) continue;
            for (const auto& sample : samples) {
                const auto item_score = FilterHeuristic::score_plaintext(
                    sample.filename, sample.bytes);
                result.score += item_score;
                if (item_score >= 80) ++result.recognized;
            }
            result.samples += samples.size();
        }
        if (!result.samples) throw std::runtime_error("game archives contain no filter samples");
        if (verification) *verification = result;
        const auto required = std::max<std::size_t>(
            std::min<std::size_t>(2, result.samples), (result.samples + 2) / 3);
        if (result.recognized < required) {
            throw std::runtime_error("xp3filter.tjs did not reveal recognizable archive data");
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

std::optional<PreparedFilter> prepare_filter_fallback(
    const GameDescriptor& game, const std::filesystem::path& generated_root,
    std::string* error, bool allow_game_local) {
    try {
        const auto local = game.root / "xp3filter.tjs";
        if (allow_game_local && std::filesystem::is_regular_file(local)) {
            return PreparedFilter{local, "game-local"};
        }

        std::string inference_reason;
        bool requires_executable_analysis = false;
        std::vector<FilterSample> aggregate_samples;
        for (const auto& archive_file : game.archives) {
            auto archive = Xp3Archive::open(archive_file.path, error);
            if (!archive) continue;
            auto samples = collect_xp3_filter_samples(*archive, 12, nullptr, error);
            if (samples.empty()) continue;
            aggregate_samples.insert(aggregate_samples.end(),
                                     std::make_move_iterator(samples.begin()),
                                     std::make_move_iterator(samples.end()));
            if (aggregate_samples.size() >= 96) break;
        }
        if (!aggregate_samples.empty()) {
            if (aggregate_samples.size() > 96) aggregate_samples.resize(96);
            auto analysis = FilterHeuristic::analyze(aggregate_samples);
            inference_reason = analysis.reason;
            requires_executable_analysis = analysis.disposition ==
                FilterInferenceDisposition::RequiresExecutableAnalysis;
            if (analysis.rule) {
                const auto& rule = *analysis.rule;
                const auto destination = generated_root /
                    game.fingerprint.substr(0, 16) / "xp3filter.tjs";
                write_text_atomic(destination,
                    "// Generated from archive known-plaintext analysis.\n" + rule.to_tjs());
                return PreparedFilter{destination, "heuristic:" + rule.name()};
            }
        }
        if (requires_executable_analysis)
            throw std::runtime_error("phase 2 executable analysis required: " +
                                     inference_reason);
        throw std::runtime_error(inference_reason.empty()
            ? "no local or confidently inferred xp3filter.tjs is available"
            : inference_reason);
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return std::nullopt;
    }
}

} // namespace krkrvita

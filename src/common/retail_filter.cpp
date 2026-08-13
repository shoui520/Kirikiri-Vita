#include "krkrvita/retail_filter.hpp"

#include "krkrvita/filter_heuristic.hpp"
#include "krkrvita/xp3_archive.hpp"
#include "krkrvita/xp3_filter_vm.hpp"

#include <algorithm>
#include <fstream>
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
            auto samples = collect_xp3_filter_samples(*archive, 32, &filter, error);
            if (samples.empty()) continue;
            for (const auto& sample : samples) {
                const auto item_score = FilterHeuristic::score_plaintext(
                    sample.filename, sample.bytes);
                result.score += item_score;
                if (item_score >= 80) ++result.recognized;
            }
            result.samples += samples.size();
            break;
        }
        if (!result.samples) throw std::runtime_error("game archives contain no filter samples");
        if (verification) *verification = result;
        if (result.recognized < std::min<std::size_t>(2, result.samples)) {
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
    std::string* error) {
    try {
        const auto local = game.root / "xp3filter.tjs";
        if (std::filesystem::is_regular_file(local)) {
            return PreparedFilter{local, "game-local"};
        }

        for (const auto& archive_file : game.archives) {
            auto archive = Xp3Archive::open(archive_file.path, error);
            if (!archive) continue;
            auto samples = collect_xp3_filter_samples(*archive, 32, nullptr, error);
            if (samples.empty()) continue;
            const auto rule = FilterHeuristic::detect(samples);
            if (!rule) continue;
            const auto destination = generated_root /
                game.fingerprint.substr(0, 16) / "xp3filter.tjs";
            write_text_atomic(destination,
                "// Generated from archive known-plaintext analysis.\n" + rule->to_tjs());
            return PreparedFilter{destination, "heuristic:" + rule->name()};
        }
        throw std::runtime_error("no local or confidently inferred xp3filter.tjs is available");
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return std::nullopt;
    }
}

} // namespace krkrvita

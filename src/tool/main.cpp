#include "krkrvita/bubble.hpp"
#include "krkrvita/filter_heuristic.hpp"
#include "krkrvita/game.hpp"
#include "krkrvita/patch_manifest.hpp"
#include "krkrvita/patch_repository.hpp"
#include "krkrvita/pe_resources.hpp"
#include "krkrvita/png.hpp"
#include "krkrvita/profile.hpp"
#include "krkrvita/retail_filter.hpp"
#include "krkrvita/sfo.hpp"
#include "krkrvita/storage.hpp"
#include "krkrvita/text_codec.hpp"
#include "krkrvita/xp3_archive.hpp"
#include "krkrvita/xp3_filter_vm.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

using namespace krkrvita;

void usage() {
    std::cerr
        << "Usage:\n"
        << "  krkrvita-tool scan GAME_DIR\n"
        << "  krkrvita-tool resolve GAME_DIR ALLDATA_JS\n"
        << "  krkrvita-tool prepare GAME_DIR CACHE_DIR\n"
        << "  krkrvita-tool prepare-heuristic GAME_DIR CACHE_DIR\n"
        << "  krkrvita-tool vita-stage GAME_DIR CACHE_DIR OUTPUT_DIR [VITA_GAME_PATH]\n"
        << "  krkrvita-tool bubble-assets GAME_DIR OUTPUT_DIR [TITLE_ID]\n"
        << "  krkrvita-tool bubble-stage GAME_DIR TEMPLATE_DIR OUTPUT_DIR [TITLE_ID]\n"
        << "  krkrvita-tool detect-filter HASH_HEX FILE_NAME SAMPLE [..]\n"
        << "  krkrvita-tool xp3-list ARCHIVE [LIMIT]\n"
        << "  krkrvita-tool xp3-detect ARCHIVE\n"
        << "  krkrvita-tool xp3-diagnose ARCHIVE\n"
        << "  krkrvita-tool xp3-verify ARCHIVE XP3FILTER_TJS\n"
        << "  krkrvita-tool xp3-extract ARCHIVE ENTRY OUTPUT [XP3FILTER_TJS]\n"
        << "  krkrvita-tool storage-extract PROFILE STORAGE OUTPUT [--text]\n"
        << "  krkrvita-tool decode-text INPUT OUTPUT\n";
}

void print_game(const GameDescriptor& game) {
    std::cout << "root: " << game.root << '\n'
              << "title: " << game.display_name << '\n'
              << "directory: " << game.directory_name << '\n'
              << "executable: " << game.executable << '\n'
              << "executable_stem: " << game.executable_stem << '\n'
              << "product_name: " << game.pe.product_name << '\n'
              << "file_description: " << game.pe.file_description << '\n'
              << "company_name: " << game.pe.company_name << '\n'
              << "fingerprint: " << game.fingerprint << '\n';
    for (const auto& archive : game.archives) {
        std::cout << "archive: " << archive.name << " (" << archive.size << ")\n";
    }
    for (const auto& plugin : game.plugins) {
        std::cout << "windows_plugin: " << plugin.name << " (" << plugin.size << ")\n";
    }
}

void print_resolution(const PatchResolution& resolution) {
    std::cout << "automatic: " << (resolution.automatic ? "yes" : "no") << '\n';
    for (const auto& candidate : resolution.candidates) {
        std::cout << "candidate: score=" << candidate.score << " brand=\""
                  << candidate.entry->brand << "\" title=\""
                  << candidate.entry->canonical_title << "\"\n";
        for (const auto& reason : candidate.reasons) std::cout << "  reason: " << reason << '\n';
        for (const auto& file : candidate.entry->files) std::cout << "  file: " << file << '\n';
    }
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open " + path.string());
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    stream.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    stream.read(reinterpret_cast<char*>(bytes.data()), length);
    return bytes;
}

std::string title_id_for(const GameDescriptor& game) {
    return bubble_title_id(game);
}

int command_scan(const std::filesystem::path& game_path) {
    const auto game = GameScanner::scan(game_path);
    print_game(game);
    if (!game.executable.empty()) {
        PeResources pe(game.executable);
        const auto icon = pe.largest_icon();
        std::cout << "embedded_icon: " << (icon ? "yes" : "no") << '\n';
        if (icon) {
            std::cout << "icon_dimensions: " << icon->width << 'x' << icon->height
                      << '@' << icon->bit_depth << '\n';
        }
    }
    return 0;
}

int command_resolve(const std::filesystem::path& game_path,
                    const std::filesystem::path& manifest_path) {
    const auto game = GameScanner::scan(game_path);
    const auto manifest = PatchManifest::load(manifest_path);
    std::cout << "manifest_entries: " << manifest.entries().size() << '\n';
    print_resolution(PatchResolver::resolve(game, manifest));
    return 0;
}

struct PreparedGame {
    GameDescriptor game;
    GameProfile profile;
    std::vector<CachedPatchFile> files;
    FilterVerification verification;
};

PreparedGame prepare_game(const std::filesystem::path& game_path,
                          const std::filesystem::path& cache_path) {
    auto game = GameScanner::scan(game_path);
    PatchRepository repository(cache_path);
    CurlHttpClient http;
    std::string error;
    bool manifest_valid = false;
    if (std::filesystem::is_regular_file(repository.manifest_path())) {
        try {
            (void)repository.load_manifest();
            manifest_valid = true;
        } catch (const std::exception&) {
            manifest_valid = false;
        }
    }
    if (!manifest_valid) {
        if (!repository.update_manifest(http, &error)) throw std::runtime_error(error);
    }
    const auto manifest = repository.load_manifest();
    const auto resolution = PatchResolver::resolve(game, manifest);
    print_resolution(resolution);
    auto profile = GameProfile::defaults(game);
    std::vector<CachedPatchFile> files;
    if (resolution.automatic && resolution.best()) {
        files = repository.fetch_bundle(http, *resolution.best()->entry);
        profile.patch_title = resolution.best()->entry->canonical_title;
        profile.patch_brand = resolution.best()->entry->brand;
        profile.patch_commit = std::string(kPatchCommit);
        profile.filter_origin = "patch-repository";
        for (const auto& file : files) {
            const auto filename = std::filesystem::path(file.relative_path).filename();
            if (filename == "xp3filter.tjs") profile.xp3_filter_path = file.cache_path;
            if (filename == "patch.tjs") profile.patch_root = file.cache_path.parent_path();
        }
        if (profile.patch_root.empty() && !profile.xp3_filter_path.empty()) {
            profile.patch_root = profile.xp3_filter_path.parent_path();
        }
    }
    if (profile.xp3_filter_path.empty()) {
        const auto fallback = prepare_filter_fallback(game, cache_path / "generated", &error);
        if (!fallback) throw std::runtime_error(error);
        profile.xp3_filter_path = fallback->path;
        profile.patch_root = fallback->path.parent_path();
        profile.filter_origin = fallback->origin;
        if (profile.patch_commit.empty()) profile.patch_commit = "local";
    }
    FilterVerification verification;
    if (!verify_retail_filter(game, profile.xp3_filter_path, &verification, &error)) {
        throw std::runtime_error(error);
    }
    return {std::move(game), std::move(profile), std::move(files), verification};
}

PreparedGame prepare_game_heuristic(const std::filesystem::path& game_path,
                                    const std::filesystem::path& cache_path) {
    // Keep the inference boundary pure. A failure/phase-2 diagnosis below is
    // reached without opening or parsing any executable. Only after a filter
    // has been synthesized and archive-verified do we perform the ordinary
    // full scan needed for the stable runtime profile identity.
    auto archive_game = GameScanner::scan(game_path, GameScanMode::ArchivesOnly);
    std::string error;
    const auto fallback = prepare_filter_fallback(
        archive_game, cache_path / "generated", &error, false);
    if (!fallback) throw std::runtime_error(error);
    FilterVerification verification;
    if (!verify_retail_filter(archive_game, fallback->path, &verification, &error))
        throw std::runtime_error(error);

    auto game = GameScanner::scan(game_path);
    auto profile = GameProfile::defaults(game);
    profile.xp3_filter_path = fallback->path;
    profile.patch_root = fallback->path.parent_path();
    profile.filter_origin = fallback->origin;
    profile.patch_commit = "archive-only";
    return {std::move(game), std::move(profile), {}, verification};
}

int print_prepared(PreparedGame prepared, const std::filesystem::path& cache_path) {
    auto& profile = prepared.profile;
    std::string error;
    const auto profile_path = cache_path / "profiles" / (profile.game_id + ".ini");
    if (!profile.save(profile_path, &error)) throw std::runtime_error(error);
    for (const auto& file : prepared.files)
        std::cout << "cached: " << file.cache_path << " sha256=" << file.sha256 << '\n';
    std::cout << "xp3_filter: " << profile.xp3_filter_path << '\n'
              << "filter_origin: " << profile.filter_origin << '\n'
              << "filter_verified: " << prepared.verification.recognized << '/'
              << prepared.verification.samples << '\n'
              << "profile: " << profile_path << '\n';
    return 0;
}

int command_prepare(const std::filesystem::path& game_path,
                    const std::filesystem::path& cache_path) {
    return print_prepared(prepare_game(game_path, cache_path), cache_path);
}

int command_prepare_heuristic(const std::filesystem::path& game_path,
                              const std::filesystem::path& cache_path) {
    return print_prepared(prepare_game_heuristic(game_path, cache_path), cache_path);
}

int command_vita_stage(const std::filesystem::path& game_path,
                       const std::filesystem::path& cache_path,
                       const std::filesystem::path& output_path,
                       std::string vita_game_path) {
    auto prepared = prepare_game(game_path, cache_path);
    if (vita_game_path.empty()) vita_game_path = "ux0:data/krkrvita/game";

    const auto local_patch_root = output_path / "patches" / prepared.profile.game_id;
    const std::string vita_patch_root =
        "ux0:data/krkrvita/patches/" + prepared.profile.game_id;
    std::filesystem::create_directories(local_patch_root);

    std::map<std::string, std::filesystem::path> staged;
    for (const auto& file : prepared.files) {
        const auto leaf = std::filesystem::path(file.relative_path).filename().string();
        const auto destination = local_patch_root / leaf;
        std::filesystem::copy_file(file.cache_path, destination,
                                   std::filesystem::copy_options::overwrite_existing);
        staged[leaf] = destination;
    }
    if (!prepared.profile.xp3_filter_path.empty() && !staged.count("xp3filter.tjs")) {
        const auto destination = local_patch_root / "xp3filter.tjs";
        std::filesystem::copy_file(prepared.profile.xp3_filter_path, destination,
                                   std::filesystem::copy_options::overwrite_existing);
        staged["xp3filter.tjs"] = destination;
    }

    prepared.profile.game_path = vita_game_path;
    prepared.profile.patch_root = vita_patch_root;
    prepared.profile.xp3_filter_path = vita_patch_root + "/xp3filter.tjs";
    std::string error;
    const auto active_profile = output_path / "active.ini";
    if (!prepared.profile.save(active_profile, &error)) throw std::runtime_error(error);
    const auto stored_profile =
        output_path / "profiles" / (prepared.profile.game_id + ".ini");
    if (!prepared.profile.save(stored_profile, &error)) throw std::runtime_error(error);

    std::cout << "stage_root: " << output_path << '\n'
              << "copy_to: ux0:data/krkrvita\n"
              << "game_copy_to: " << vita_game_path << '\n'
              << "active_profile: " << active_profile << '\n'
              << "stored_profile: " << stored_profile << '\n';
    for (const auto& item : staged) std::cout << "staged: " << item.second << '\n';
    return 0;
}

int command_bubble_assets(const std::filesystem::path& game_path,
                          const std::filesystem::path& output_path,
                          std::string title_id) {
    const auto game = GameScanner::scan(game_path);
    if (title_id.empty()) title_id = title_id_for(game);
    if (!is_vita_title_id(title_id))
        throw std::runtime_error("Vita title ID must match ABCD12345");
    PeResources pe(game.executable);
    const auto icon = pe.largest_icon();
    if (!icon) throw std::runtime_error("game executable has no extractable icon");
    std::string error;
    const auto decoded = decode_icon(*icon, &error);
    if (!decoded) throw std::runtime_error(error);
    const auto fitted = fit_icon(*decoded, 128, 128);
    if (!write_vita_indexed_png(output_path / "sce_sys/icon0.png", fitted, &error)) {
        throw std::runtime_error(error);
    }
    if (!ParamSfo::bubble(game.display_name, title_id)
             .write(output_path / "sce_sys/param.sfo", &error)) {
        throw std::runtime_error(error);
    }
    std::filesystem::create_directories(output_path);
    std::ofstream game_id(output_path / "game.id", std::ios::trunc);
    game_id << game.fingerprint.substr(0, 16) << '\n';
    std::cout << "title_id: " << title_id << '\n'
              << "title: " << game.display_name << '\n'
              << "icon0: " << output_path / "sce_sys/icon0.png" << '\n';
    return 0;
}

int command_bubble_stage(const std::filesystem::path& game_path,
                         const std::filesystem::path& template_path,
                         const std::filesystem::path& output_path,
                         std::string title_id) {
    const auto game = GameScanner::scan(game_path);
    if (title_id.empty()) title_id = bubble_title_id(game);
    BubbleSpec spec;
    spec.title = game.display_name;
    spec.title_id = title_id;
    spec.game_id = game.fingerprint.substr(0, 16);
    spec.executable = game.executable;
    std::string error;
    if (!stage_bubble(spec, template_path, output_path, &error)) {
        throw std::runtime_error(error);
    }
    std::cout << "title_id: " << title_id << '\n'
              << "title: " << game.display_name << '\n'
              << "game_id: " << spec.game_id << '\n'
              << "stage_root: " << output_path << '\n';
    return 0;
}

int command_detect_filter(int argc, char** argv) {
    if (argc < 6 || (argc - 3) % 3 != 0) {
        throw std::runtime_error("detect-filter expects HASH FILE SAMPLE triples");
    }
    std::vector<FilterSample> samples;
    for (int i = 3; i < argc; i += 3) {
        FilterSample sample;
        sample.hash = static_cast<std::uint32_t>(std::stoul(argv[i], nullptr, 16));
        sample.filename = argv[i + 1];
        sample.bytes = read_file(argv[i + 2]);
        samples.push_back(std::move(sample));
    }
    const auto analysis = FilterHeuristic::analyze(samples);
    if (!analysis.rule) {
        std::cout << "filter: unknown\n"
                  << "phase: " << (analysis.disposition ==
                        FilterInferenceDisposition::RequiresExecutableAnalysis ? 2 : 1) << '\n'
                  << "reason: " << analysis.reason << '\n';
        return 2;
    }
    const auto& rule = *analysis.rule;
    std::cout << "filter: " << rule.name() << '\n'
              << "phase: 1\n"
              << "score: " << rule.score << '\n'
              << "confidence: " << rule.confidence << '\n'
              << "tjs: " << rule.to_tjs();
    return 0;
}

Xp3Archive open_xp3(const std::filesystem::path& path) {
    std::string error;
    auto archive = Xp3Archive::open(path, &error);
    if (!archive) throw std::runtime_error(error);
    return std::move(*archive);
}

std::vector<FilterSample> archive_samples(const Xp3Archive& archive,
                                          Xp3FilterVm* filter = nullptr) {
    std::string error;
    auto samples = collect_xp3_filter_samples(archive, 32, filter, &error);
    if (samples.empty()) throw std::runtime_error("archive has no useful filter samples");
    return samples;
}

int command_xp3_list(const std::filesystem::path& path, std::size_t limit) {
    const auto archive = open_xp3(path);
    std::cout << "archive_offset: " << archive.archive_offset() << '\n'
              << "entries: " << archive.entries().size() << '\n';
    limit = std::min(limit, archive.entries().size());
    for (std::size_t i = 0; i < limit; ++i) {
        const auto& entry = archive.entries()[i];
        std::cout << "file: hash=" << std::hex << std::setw(8) << std::setfill('0')
                  << entry.hash << std::dec << std::setfill(' ') << " size="
                  << entry.original_size << " segments=" << entry.segments.size()
                  << " name=\"" << entry.name << "\"\n";
    }
    return 0;
}

int command_xp3_detect(const std::filesystem::path& path) {
    const auto archive = open_xp3(path);
    const auto samples = archive_samples(archive);
    const auto analysis = FilterHeuristic::analyze(samples);
    std::cout << "samples: " << samples.size() << '\n';
    if (!analysis.rule) {
        std::cout << "filter: unknown\n"
                  << "phase: " << (analysis.disposition ==
                        FilterInferenceDisposition::RequiresExecutableAnalysis ? 2 : 1) << '\n'
                  << "reason: " << analysis.reason << '\n';
        return 2;
    }
    const auto& rule = *analysis.rule;
    std::cout << "filter: " << rule.name() << '\n'
              << "phase: 1\n"
              << "score: " << rule.score << '\n'
              << "confidence: " << rule.confidence << '\n'
              << "tjs: " << rule.to_tjs();
    return 0;
}

std::string diagnostic_extension(std::string_view filename) {
    const auto dot = filename.rfind('.');
    if (dot == std::string_view::npos) return "<none>";
    std::string extension(filename.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension;
}

int command_xp3_diagnose(const std::filesystem::path& path) {
    const auto archive = open_xp3(path);
    const auto samples = archive_samples(archive);
    std::map<std::string, std::vector<FilterSample>> groups;
    for (const auto& sample : samples)
        groups[diagnostic_extension(sample.filename)].push_back(sample);
    std::cout << "samples: " << samples.size() << '\n';
    for (const auto& [extension, group] : groups) {
        const auto analysis = FilterHeuristic::analyze(group);
        std::cout << "group: extension=" << extension << " samples=" << group.size()
                  << " constrained=" << analysis.constrained_samples << " filter="
                  << (analysis.rule ? analysis.rule->name() : "unknown") << " phase="
                  << (analysis.disposition ==
                        FilterInferenceDisposition::RequiresExecutableAnalysis ? 2 : 1)
                  << " reason=\"" << analysis.reason << "\"\n";
        for (const auto& sample : group) {
            std::cout << "  sample: hash=" << std::hex << std::setw(8)
                      << std::setfill('0') << sample.hash << std::dec
                      << std::setfill(' ') << " bytes=" << sample.bytes.size()
                      << " prefix=";
            for (std::size_t i = 0; i < std::min<std::size_t>(8, sample.bytes.size()); ++i) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << unsigned(sample.bytes[i]);
            }
            std::cout << std::dec << std::setfill(' ') << " name=\""
                      << sample.filename << "\"\n";
        }
    }
    return 0;
}

int command_xp3_verify(const std::filesystem::path& path,
                       const std::filesystem::path& script_path) {
    const auto script_bytes = read_file(script_path);
    Xp3FilterVm filter;
    std::string error;
    if (!filter.load(std::string(script_bytes.begin(), script_bytes.end()), &error)) {
        throw std::runtime_error(error);
    }
    const auto archive = open_xp3(path);
    const auto samples = archive_samples(archive, &filter);
    int recognized = 0;
    int score = 0;
    for (const auto& sample : samples) {
        const auto item_score = FilterHeuristic::score_plaintext(sample.filename, sample.bytes);
        score += item_score;
        if (item_score >= 80) ++recognized;
    }
    std::cout << "samples: " << samples.size() << '\n'
              << "recognized: " << recognized << '\n'
              << "score: " << score << '\n'
              << "filter_vm: " << (recognized >= std::min<std::size_t>(2, samples.size())
                                      ? "verified" : "unverified") << '\n';
    return recognized >= std::min<std::size_t>(2, samples.size()) ? 0 : 2;
}

int command_xp3_extract(const std::filesystem::path& path, std::string_view name,
                        const std::filesystem::path& output,
                        const std::filesystem::path& filter_path) {
    const auto archive = open_xp3(path);
    const auto* entry = archive.find(name);
    if (!entry) throw std::runtime_error("entry not found in XP3 archive");
    std::optional<Xp3FilterVm> filter;
    std::string error;
    if (!filter_path.empty()) {
        const auto script = read_file(filter_path);
        filter.emplace();
        if (!filter->load(std::string(script.begin(), script.end()), &error)) {
            throw std::runtime_error(error);
        }
    }
    // Retail movie entries routinely exceed 64 MiB. This is a host-side
    // explicit extraction command, so admit a bounded 512 MiB payload while
    // retaining the archive reader's normal overflow and truncation checks.
    auto bytes = archive.read(*entry, 512u * 1024u * 1024u,
                              filter ? &*filter : nullptr, &error);
    if (!bytes) throw std::runtime_error(error);
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes->data()),
                 static_cast<std::streamsize>(bytes->size()));
    if (!stream) throw std::runtime_error("cannot write extracted entry");
    std::cout << "extracted: " << entry->name << '\n'
              << "bytes: " << bytes->size() << '\n'
              << "output: " << output << '\n';
    return 0;
}

int command_decode_text(const std::filesystem::path& input,
                        const std::filesystem::path& output) {
    const auto encoded = read_file(input);
    std::string decoded;
    std::string error;
    if (!decode_kirikiri_text(encoded, decoded, &error)) throw std::runtime_error(error);
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    stream.write(decoded.data(), static_cast<std::streamsize>(decoded.size()));
    if (!stream) throw std::runtime_error("cannot write decoded text");
    std::cout << "decoded_bytes: " << decoded.size() << '\n'
              << "output: " << output << '\n';
    return 0;
}

int command_storage_extract(const std::filesystem::path& profile_path,
                            std::string_view name,
                            const std::filesystem::path& output,
                            bool text) {
    std::string error;
    const auto profile = GameProfile::load(profile_path, &error);
    if (!profile) throw std::runtime_error(error);
    auto storage = GameStorage::mount(*profile, &error);
    if (!storage) throw std::runtime_error(error);
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    std::size_t size = 0;
    if (text) {
        const auto decoded = storage->read_script(name, &error);
        if (!decoded) throw std::runtime_error(error);
        stream.write(decoded->data(), static_cast<std::streamsize>(decoded->size()));
        size = decoded->size();
    } else {
        const auto bytes = storage->read(name, 256u * 1024u * 1024u, &error);
        if (!bytes) throw std::runtime_error(error);
        stream.write(reinterpret_cast<const char*>(bytes->data()),
                     static_cast<std::streamsize>(bytes->size()));
        size = bytes->size();
    }
    if (!stream) throw std::runtime_error("cannot write extracted storage");
    std::cout << "storage: " << name << '\n'
              << "bytes: " << size << '\n'
              << "output: " << output << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            usage();
            return 1;
        }
        const std::string command = argv[1];
        if (command == "scan" && argc == 3) return command_scan(argv[2]);
        if (command == "resolve" && argc == 4) return command_resolve(argv[2], argv[3]);
        if (command == "prepare" && argc == 4) return command_prepare(argv[2], argv[3]);
        if (command == "prepare-heuristic" && argc == 4)
            return command_prepare_heuristic(argv[2], argv[3]);
        if (command == "vita-stage" && (argc == 5 || argc == 6)) {
            return command_vita_stage(argv[2], argv[3], argv[4],
                                      argc == 6 ? argv[5] : "");
        }
        if (command == "bubble-assets" && (argc == 4 || argc == 5)) {
            return command_bubble_assets(argv[2], argv[3], argc == 5 ? argv[4] : "");
        }
        if (command == "bubble-stage" && (argc == 5 || argc == 6)) {
            return command_bubble_stage(argv[2], argv[3], argv[4],
                                        argc == 6 ? argv[5] : "");
        }
        if (command == "detect-filter") return command_detect_filter(argc, argv);
        if (command == "xp3-list" && (argc == 3 || argc == 4)) {
            return command_xp3_list(argv[2], argc == 4 ? std::stoul(argv[3]) : 20);
        }
        if (command == "xp3-detect" && argc == 3) return command_xp3_detect(argv[2]);
        if (command == "xp3-diagnose" && argc == 3) return command_xp3_diagnose(argv[2]);
        if (command == "xp3-verify" && argc == 4) return command_xp3_verify(argv[2], argv[3]);
        if (command == "xp3-extract" && (argc == 5 || argc == 6)) {
            return command_xp3_extract(argv[2], argv[3], argv[4],
                                       argc == 6 ? argv[5] : "");
        }
        if (command == "storage-extract" && (argc == 5 || argc == 6)) {
            if (argc == 6 && std::string_view(argv[5]) != "--text") {
                throw std::runtime_error("storage-extract only accepts --text");
            }
            return command_storage_extract(argv[2], argv[3], argv[4], argc == 6);
        }
        if (command == "decode-text" && argc == 4) return command_decode_text(argv[2], argv[3]);
        usage();
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << '\n';
        return 1;
    }
}

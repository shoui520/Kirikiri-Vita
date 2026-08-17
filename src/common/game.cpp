#include "krkrvita/game.hpp"

#include "krkrvita/pe_resources.hpp"
#include "krkrvita/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>

#ifdef __vita__
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#endif

namespace krkrvita {
namespace {

std::vector<std::uint32_t> decode_utf8(std::string_view input) {
    std::vector<std::uint32_t> output;
    for (std::size_t i = 0; i < input.size();) {
        const auto first = static_cast<unsigned char>(input[i]);
        std::uint32_t cp = 0;
        std::size_t length = 1;
        if ((first & 0x80) == 0) {
            cp = first;
        } else if ((first & 0xe0) == 0xc0 && i + 1 < input.size()) {
            cp = first & 0x1f;
            length = 2;
        } else if ((first & 0xf0) == 0xe0 && i + 2 < input.size()) {
            cp = first & 0x0f;
            length = 3;
        } else if ((first & 0xf8) == 0xf0 && i + 3 < input.size()) {
            cp = first & 0x07;
            length = 4;
        } else {
            ++i;
            continue;
        }
        bool valid = true;
        for (std::size_t j = 1; j < length; ++j) {
            const auto next = static_cast<unsigned char>(input[i + j]);
            if ((next & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3f);
        }
        if (valid) {
            output.push_back(cp);
            i += length;
        } else {
            ++i;
        }
    }
    return output;
}

void append_utf8(std::string& output, std::uint32_t cp) {
    if (cp <= 0x7f) {
        output.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        output.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

bool generic_pe_title(std::string_view value) {
    const auto normalized = normalize_game_name(value);
    return normalized.empty() || normalized.find("kirikiri") != std::string::npos ||
           normalized.find("tvp") == 0 || normalized.find("scriptingplatform") != std::string::npos;
}

std::string lower_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

void hash_file_prefix(Sha256& hash, const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::array<char, 65536> buffer{};
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    hash.update(buffer.data(), static_cast<std::size_t>(stream.gcount()));
}

} // namespace

std::string normalize_game_name(std::string_view utf8) {
    std::string output;
    for (auto cp : decode_utf8(utf8)) {
        if (cp >= 0xff01 && cp <= 0xff5e) {
            cp -= 0xfee0;
        }
        if (cp >= 'A' && cp <= 'Z') {
            cp += 'a' - 'A';
        }
        const bool ascii_word = (cp >= 'a' && cp <= 'z') ||
                                (cp >= '0' && cp <= '9');
        const bool non_ascii_word = cp > 0x7f &&
            cp != 0x3000 && cp != 0x30fb && cp != 0x3001 && cp != 0x3002 &&
            cp != 0x3010 && cp != 0x3011 && cp != 0x300c && cp != 0x300d &&
            cp != 0xff08 && cp != 0xff09 && cp != 0xff0f;
        if (ascii_word || non_ascii_word) {
            append_utf8(output, cp);
        }
    }
    return output;
}

GameDescriptor GameScanner::scan(const std::filesystem::path& root,
                                 GameScanMode mode) {
#ifdef __vita__
    const std::string native_root = root.string();
    const SceUID directory = sceIoDopen(native_root.c_str());
    if (directory < 0) {
        throw std::runtime_error("game path is not a directory: " + native_root);
    }
#else
    if (!std::filesystem::is_directory(root)) {
        throw std::runtime_error("game path is not a directory: " + root.string());
    }
#endif

    GameDescriptor game;
#ifdef __vita__
    // realpath/canonical do not understand Vita device prefixes reliably.
    // The caller has already selected an absolute ux0: path, so retain it.
    game.root = root;
#else
    game.root = std::filesystem::canonical(root);
#endif
    game.directory_name = game.root.filename().string();

    std::vector<GameFile> executables;
    std::vector<GameFile> fingerprint_files;
#ifdef __vita__
    SceIoDirent entry{};
    while (sceIoDread(directory, &entry) > 0) {
        if (!SCE_S_ISREG(entry.d_stat.st_mode)) {
            entry = {};
            continue;
        }
        const std::filesystem::path path = game.root / entry.d_name;
        GameFile file{path, entry.d_name,
                      static_cast<std::uint64_t>(entry.d_stat.st_size)};
        const auto extension = lower_extension(path);
        if (extension == ".exe" && mode == GameScanMode::Full) {
            executables.push_back(file);
        } else if (extension == ".xp3") {
            game.archives.push_back(file);
        } else if (mode == GameScanMode::Full &&
                   (extension == ".tpm" || extension == ".dll")) {
            game.plugins.push_back(file);
        }
        if (extension == ".xp3" || (mode == GameScanMode::Full &&
            (extension == ".exe" || extension == ".tpm" || extension == ".dll"))) {
            fingerprint_files.push_back(file);
        }
        entry = {};
    }
    sceIoDclose(directory);
#else
    for (const auto& item : std::filesystem::directory_iterator(game.root)) {
        if (!item.is_regular_file()) {
            continue;
        }
        GameFile file{item.path(), item.path().filename().string(), item.file_size()};
        const auto extension = lower_extension(item.path());
        if (extension == ".exe" && mode == GameScanMode::Full) {
            executables.push_back(file);
        } else if (extension == ".xp3") {
            game.archives.push_back(file);
        } else if (mode == GameScanMode::Full &&
                   (extension == ".tpm" || extension == ".dll")) {
            game.plugins.push_back(file);
        }
        if (extension == ".xp3" || (mode == GameScanMode::Full &&
            (extension == ".exe" || extension == ".tpm" || extension == ".dll"))) {
            fingerprint_files.push_back(file);
        }
    }
#endif

    auto executable_score = [](const GameFile& file) {
        auto name = normalize_game_name(file.name);
        int score = static_cast<int>(std::min<std::uint64_t>(file.size / 65536, 100));
        if (name.find("check") != std::string::npos ||
            name.find("config") != std::string::npos ||
            name.find("uninst") != std::string::npos ||
            name.find("sigchk") != std::string::npos ||
            name.find("tool") != std::string::npos ||
            name.find("ファイル破損") != std::string::npos) {
            score -= 200;
        }
        return score;
    };
    std::sort(executables.begin(), executables.end(), [&](const auto& left, const auto& right) {
        return executable_score(left) > executable_score(right);
    });

    if (mode == GameScanMode::Full && !executables.empty()) {
        game.executable = executables.front().path;
        game.executable_stem = game.executable.stem().string();
        PeResources pe(game.executable);
        if (pe.valid()) {
            game.pe = pe.metadata();
        }
    }

    if (!generic_pe_title(game.pe.product_name)) {
        game.display_name = game.pe.product_name;
    } else if (!generic_pe_title(game.pe.file_description)) {
        game.display_name = game.pe.file_description;
    } else {
        game.display_name = game.directory_name;
    }

    std::sort(game.archives.begin(), game.archives.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    std::sort(game.plugins.begin(), game.plugins.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    std::sort(fingerprint_files.begin(), fingerprint_files.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });

    Sha256 fingerprint;
    for (const auto& file : fingerprint_files) {
        const auto normalized = normalize_game_name(file.name);
        fingerprint.update(normalized);
        fingerprint.update(&file.size, sizeof(file.size));
    }
    if (mode == GameScanMode::Full && !game.executable.empty()) {
        hash_file_prefix(fingerprint, game.executable);
    }
    game.fingerprint = Sha256::hex(fingerprint.finish());
    return game;
}

} // namespace krkrvita

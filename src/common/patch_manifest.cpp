#include "krkrvita/patch_manifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace krkrvita {
namespace {

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

class JsReader {
public:
    explicit JsReader(std::string_view input) : input_(input) {}

    std::vector<PatchEntry> read() {
        const auto equals = input_.find('=');
        position_ = input_.find('[', equals == std::string_view::npos ? 0 : equals);
        if (position_ == std::string_view::npos) {
            throw std::runtime_error("patch manifest has no array");
        }
        expect('[');
        std::vector<PatchEntry> entries;
        skip();
        while (!take(']')) {
            entries.push_back(entry());
            skip();
            if (!take(',')) {
                expect(']');
                break;
            }
            skip();
        }
        return entries;
    }

private:
    void skip() {
        for (;;) {
            while (position_ < input_.size() &&
                   std::isspace(static_cast<unsigned char>(input_[position_]))) {
                ++position_;
            }
            if (input_.substr(position_, 2) == "//") {
                const auto end = input_.find('\n', position_ + 2);
                position_ = end == std::string_view::npos ? input_.size() : end + 1;
                continue;
            }
            if (input_.substr(position_, 2) == "/*") {
                const auto end = input_.find("*/", position_ + 2);
                if (end == std::string_view::npos) {
                    throw std::runtime_error("unterminated manifest comment");
                }
                position_ = end + 2;
                continue;
            }
            break;
        }
    }

    bool take(char wanted) {
        skip();
        if (position_ < input_.size() && input_[position_] == wanted) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char wanted) {
        if (!take(wanted)) {
            throw std::runtime_error(std::string("expected '") + wanted +
                                     "' at manifest offset " +
                                     std::to_string(position_));
        }
    }

    std::uint64_t number() {
        skip();
        std::uint64_t value = 0;
        const auto begin = position_;
        while (position_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[position_]))) {
            value = value * 10 + static_cast<unsigned>(input_[position_] - '0');
            ++position_;
        }
        if (begin == position_) {
            throw std::runtime_error("expected manifest number");
        }
        return value;
    }

    std::string string() {
        skip();
        if (position_ >= input_.size() ||
            (input_[position_] != '"' && input_[position_] != '\'')) {
            throw std::runtime_error("expected manifest string");
        }
        const char quote = input_[position_++];
        std::string output;
        while (position_ < input_.size()) {
            const char c = input_[position_++];
            if (c == quote) {
                return output;
            }
            if (c != '\\') {
                output.push_back(c);
                continue;
            }
            if (position_ >= input_.size()) {
                break;
            }
            const char escaped = input_[position_++];
            switch (escaped) {
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'u': {
                if (position_ + 4 > input_.size()) {
                    throw std::runtime_error("truncated Unicode escape");
                }
                std::uint32_t cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const char digit = input_[position_++];
                    cp <<= 4;
                    if (digit >= '0' && digit <= '9') cp |= digit - '0';
                    else if (digit >= 'a' && digit <= 'f') cp |= digit - 'a' + 10;
                    else if (digit >= 'A' && digit <= 'F') cp |= digit - 'A' + 10;
                    else throw std::runtime_error("invalid Unicode escape");
                }
                append_utf8(output, cp);
                break;
            }
            default: output.push_back(escaped); break;
            }
        }
        throw std::runtime_error("unterminated manifest string");
    }

    PatchEntry entry() {
        expect('[');
        PatchEntry result;
        result.timestamp = number();
        expect(',');
        result.brand = string();
        expect(',');
        result.canonical_title = string();
        expect(',');
        result.display_title = string();
        expect(',');
        expect('[');
        skip();
        while (!take(']')) {
            result.files.push_back(string());
            if (!take(',')) {
                expect(']');
                break;
            }
        }
        expect(']');
        return result;
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

int similarity(std::string_view signal, std::string_view title, std::string_view label,
               std::vector<std::string>& reasons) {
    if (signal.empty() || title.empty()) {
        return 0;
    }
    if (signal == title) {
        reasons.push_back(std::string(label) + " exact match");
        return 120;
    }
    if (signal.size() >= 4 && title.size() >= 4 &&
        (signal.find(title) != std::string_view::npos ||
         title.find(signal) != std::string_view::npos)) {
        reasons.push_back(std::string(label) + " partial match");
        return 60 + static_cast<int>(std::min(signal.size(), title.size()) / 4);
    }
    return 0;
}

std::string percent_encode(std::string_view input) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    for (const auto value : input) {
        const auto c = static_cast<unsigned char>(value);
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            output.push_back(static_cast<char>(c));
        } else {
            output.push_back('%');
            output.push_back(hex[c >> 4]);
            output.push_back(hex[c & 0xf]);
        }
    }
    return output;
}

constexpr std::string_view trusted_release_prefix =
    "https://github.com/zeas2/Kirikiroid2_patch/releases/download/";

bool is_trusted_release_url(std::string_view reference) {
    if (reference.substr(0, trusted_release_prefix.size()) != trusted_release_prefix) {
        return false;
    }
    const auto tail = reference.substr(trusted_release_prefix.size());
    if (tail.empty() || tail.find("..") != std::string_view::npos ||
        tail.find('\\') != std::string_view::npos || tail.find('?') != std::string_view::npos ||
        tail.find('#') != std::string_view::npos) {
        return false;
    }
    const auto slash = tail.rfind('/');
    if (slash == std::string_view::npos || slash + 1 == tail.size()) return false;
    std::string filename(tail.substr(slash + 1));
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return filename.size() >= 4 && filename.substr(filename.size() - 4) == ".xp3";
}

std::string_view normalized_repository_path(std::string_view reference) {
    constexpr std::string_view shared_prefix = "../patch/";
    if (reference.substr(0, shared_prefix.size()) == shared_prefix) {
        return reference.substr(shared_prefix.size());
    }
    return reference;
}

} // namespace

PatchManifest PatchManifest::parse(std::string_view javascript) {
    PatchManifest manifest;
    manifest.entries_ = JsReader(javascript).read();
    return manifest;
}

PatchManifest PatchManifest::load(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open patch manifest: " + path.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return parse(contents.str());
}

PatchResolution PatchResolver::resolve(const GameDescriptor& game,
                                       const PatchManifest& manifest) {
    const std::vector<std::pair<std::string, std::string>> signals = {
        {normalize_game_name(game.directory_name), "directory name"},
        {normalize_game_name(game.display_name), "display name"},
        {normalize_game_name(game.executable_stem), "executable name"},
        {normalize_game_name(game.pe.product_name), "PE product name"},
        {normalize_game_name(game.pe.file_description), "PE description"},
    };

    PatchResolution resolution;
    for (const auto& entry : manifest.entries()) {
        const auto canonical = normalize_game_name(entry.canonical_title);
        const auto display = normalize_game_name(entry.display_title);
        const auto brand = normalize_game_name(entry.brand);
        PatchCandidate candidate{&entry, 0, {}};
        for (const auto& [signal, label] : signals) {
            candidate.score = std::max(candidate.score,
                similarity(signal, canonical, label, candidate.reasons));
            candidate.score = std::max(candidate.score,
                similarity(signal, display, label, candidate.reasons));
            if (!brand.empty() && signal.find(brand) != std::string::npos) {
                candidate.score += 10;
                candidate.reasons.push_back(label + " contains brand");
            }
        }
        if (candidate.score > 0) {
            resolution.candidates.push_back(std::move(candidate));
        }
    }
    std::sort(resolution.candidates.begin(), resolution.candidates.end(),
              [](const auto& a, const auto& b) {
                  if (a.score != b.score) return a.score > b.score;
                  if (a.entry->timestamp != b.entry->timestamp)
                      return a.entry->timestamp > b.entry->timestamp;
                  return a.entry->canonical_title < b.entry->canonical_title;
              });
    if (resolution.candidates.size() > 12) {
        resolution.candidates.resize(12);
    }
    if (!resolution.candidates.empty()) {
        const int runner_up = resolution.candidates.size() > 1
            ? resolution.candidates[1].score : 0;
        resolution.automatic = resolution.candidates[0].score >= 120 &&
                               resolution.candidates[0].score - runner_up >= 15;
    }
    return resolution;
}

bool is_safe_patch_path(std::string_view relative) {
    if (is_trusted_release_url(relative)) return true;
    relative = normalized_repository_path(relative);
    if (relative.empty() || relative.front() == '/' || relative.find('\\') != std::string_view::npos ||
        relative.find('\0') != std::string_view::npos) {
        return false;
    }
    std::size_t begin = 0;
    while (begin <= relative.size()) {
        const auto end = relative.find('/', begin);
        const auto component = relative.substr(begin,
            end == std::string_view::npos ? relative.size() - begin : end - begin);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    auto extension_at = relative.rfind('.');
    if (extension_at == std::string_view::npos) {
        return false;
    }
    std::string extension(relative.substr(extension_at));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static constexpr std::string_view allowed[] = {
        ".tjs", ".xp3", ".txt", ".csv", ".ini", ".json", ".dat", ".key",
        ".cf", ".md",
    };
    return std::find(std::begin(allowed), std::end(allowed), extension) != std::end(allowed);
}

std::string patch_file_url(std::string_view relative) {
    if (!is_safe_patch_path(relative)) {
        throw std::invalid_argument("unsafe or unsupported patch path");
    }
    if (is_trusted_release_url(relative)) return std::string(relative);
    relative = normalized_repository_path(relative);
    return std::string(kPatchRawBase) + std::string(kPatchCommit) + "/patch/" +
           percent_encode(relative);
}

} // namespace krkrvita

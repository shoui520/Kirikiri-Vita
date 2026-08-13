#include "krkrvita/storage.hpp"

#include "krkrvita/game.hpp"
#include "krkrvita/text_codec.hpp"
#include "krkrvita/xp3_archive.hpp"
#include "krkrvita/xp3_filter_vm.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace krkrvita {
namespace {

std::string storage_key(std::string_view input) {
    std::string value;
    value.reserve(input.size());
    for (const auto raw : input) {
        auto character = static_cast<unsigned char>(raw);
        if (character == '\\') character = '/';
        if (character < 0x80) character = static_cast<unsigned char>(std::tolower(character));
        value.push_back(static_cast<char>(character));
    }
    while (value.rfind("./", 0) == 0) value.erase(0, 2);
    while (!value.empty() && value.front() == '/') value.erase(value.begin());
    while (value.find("//") != std::string::npos) {
        value.erase(value.find("//"), 1);
    }
    return value;
}

bool safe_relative_name(std::string_view value) {
    if (value.empty() || value.find('\0') != std::string_view::npos) return false;
    const std::filesystem::path path{std::string(value)};
    if (path.is_absolute() || path.has_root_name()) return false;
    for (const auto& part : path) {
        if (part == "..") return false;
    }
    return true;
}

std::string archive_leaf(std::string_view value) {
    const auto separator = value.find('>');
    return separator == std::string_view::npos
        ? std::string(value) : std::string(value.substr(separator + 1));
}

int archive_priority(const std::filesystem::path& path) {
    auto stem = storage_key(path.stem().string());
    if (stem.rfind("patch", 0) == 0) {
        int number = 1;
        if (stem.size() > 5) {
            try { number = std::stoi(stem.substr(5)); } catch (...) { number = 1; }
        }
        return 10000 + number;
    }
    if (stem == "data") return 9000;
    return 1000;
}

std::optional<std::vector<std::uint8_t>> read_file(
    const std::filesystem::path& path, std::size_t safety_limit, std::string* error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        if (error) *error = "cannot determine storage size: " + path.string();
        return std::nullopt;
    }
    if (size > safety_limit || size > std::numeric_limits<std::size_t>::max()) {
        if (error) *error = "storage exceeds the caller's memory safety limit";
        return std::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        if (error) *error = "cannot open storage: " + path.string();
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!stream) {
        if (error) *error = "cannot read storage: " + path.string();
        return std::nullopt;
    }
    return bytes;
}

} // namespace

struct GameStorage::Impl {
    struct MountedArchive {
        int priority = 0;
        Xp3Archive archive;
    };

    GameProfile profile;
    std::unique_ptr<Xp3FilterVm> filter;
    std::vector<MountedArchive> archives;
    std::unordered_map<std::string, std::filesystem::path> loose_files;
    std::vector<std::string> search_paths;

    void index_loose_root(const std::filesystem::path& root, bool overwrite) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) return;
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; !ec && iterator != end; iterator.increment(ec)) {
            if (!iterator->is_regular_file(ec)) continue;
            const auto relative = iterator->path().lexically_relative(root).generic_string();
            const auto key = storage_key(relative);
            if (overwrite) loose_files[key] = iterator->path();
            else loose_files.emplace(key, iterator->path());
        }
    }

    std::vector<std::string> candidates(std::string_view requested) const {
        auto name = storage_key(archive_leaf(requested));
        std::vector<std::string> result;
        result.push_back(name);
        if (name.find('/') == std::string::npos) {
            for (const auto& prefix : search_paths) result.push_back(prefix + name);
        }
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
};

GameStorage::GameStorage() = default;
GameStorage::~GameStorage() = default;
GameStorage::GameStorage(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GameStorage::GameStorage(GameStorage&&) noexcept = default;
GameStorage& GameStorage::operator=(GameStorage&&) noexcept = default;

std::optional<GameStorage> GameStorage::mount(const GameProfile& profile,
                                               std::string* error) {
    try {
        if (!std::filesystem::is_directory(profile.game_path)) {
            throw std::runtime_error("game directory is unavailable");
        }
        auto impl = std::make_unique<Impl>();
        impl->profile = profile;

        auto filter_path = profile.xp3_filter_path;
        if (filter_path.empty()) filter_path = profile.game_path / "xp3filter.tjs";
        if (std::filesystem::is_regular_file(filter_path)) {
            std::ifstream stream(filter_path, std::ios::binary);
            std::ostringstream content;
            content << stream.rdbuf();
            impl->filter = std::make_unique<Xp3FilterVm>();
            if (!stream || !impl->filter->load(content.str(), error)) return std::nullopt;
        }

        const auto game = GameScanner::scan(profile.game_path);
        if (!game.archives.empty() && !impl->filter) {
            throw std::runtime_error("xp3filter.tjs must be staged before mounting retail archives");
        }
        for (const auto& file : game.archives) {
            auto archive = Xp3Archive::open(file.path, error);
            if (!archive) return std::nullopt;
            impl->archives.push_back({archive_priority(file.path), std::move(*archive)});
        }
        std::sort(impl->archives.begin(), impl->archives.end(),
                  [](const auto& left, const auto& right) {
            return left.priority > right.priority;
        });

        // Game files override archives. Compatibility patch files override both.
        impl->index_loose_root(profile.game_path, false);
        if (!profile.patch_root.empty()) impl->index_loose_root(profile.patch_root, true);
        return GameStorage(std::move(impl));
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return std::nullopt;
    }
}

void GameStorage::add_auto_path(std::string_view path) {
    if (!impl_) return;
    auto normalized = storage_key(archive_leaf(path));
    if (normalized.empty()) return;
    if (normalized.back() != '/') normalized.push_back('/');
    if (!safe_relative_name(normalized)) return;
    if (std::find(impl_->search_paths.begin(), impl_->search_paths.end(), normalized) ==
        impl_->search_paths.end()) {
        impl_->search_paths.push_back(std::move(normalized));
    }
}

bool GameStorage::exists(std::string_view storage_name) const {
    if (!impl_) return false;
    const auto requested = archive_leaf(storage_name);
    if (!safe_relative_name(requested)) return false;
    for (const auto& candidate : impl_->candidates(requested)) {
        if (impl_->loose_files.contains(candidate)) return true;
        for (const auto& mounted : impl_->archives) {
            if (mounted.archive.find(candidate)) return true;
        }
    }
    return false;
}

std::optional<std::vector<std::uint8_t>> GameStorage::read(
    std::string_view storage_name, std::size_t safety_limit, std::string* error) const {
    if (!impl_) {
        if (error) *error = "storage is not mounted";
        return std::nullopt;
    }
    const auto requested = archive_leaf(storage_name);
    if (!safe_relative_name(requested)) {
        if (error) *error = "unsafe storage name";
        return std::nullopt;
    }
    for (const auto& candidate : impl_->candidates(requested)) {
        if (const auto loose = impl_->loose_files.find(candidate);
            loose != impl_->loose_files.end()) {
            return read_file(loose->second, safety_limit, error);
        }
        for (const auto& mounted : impl_->archives) {
            if (const auto* entry = mounted.archive.find(candidate)) {
                return mounted.archive.read(*entry, safety_limit, impl_->filter.get(), error);
            }
        }
    }
    if (error) *error = "storage not found: " + std::string(storage_name);
    return std::nullopt;
}

std::optional<std::string> GameStorage::read_script(std::string_view storage_name,
                                                     std::string* error) const {
    auto bytes = read(storage_name, 32u * 1024u * 1024u, error);
    if (!bytes) return std::nullopt;
    std::string decoded;
    if (!decode_kirikiri_text(*bytes, decoded, error)) return std::nullopt;
    return decoded;
}

const std::vector<std::string>& GameStorage::auto_paths() const {
    static const std::vector<std::string> empty;
    return impl_ ? impl_->search_paths : empty;
}

} // namespace krkrvita

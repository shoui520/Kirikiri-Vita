#include "krkrvita/patch_repository.hpp"

#include "krkrvita/sha256.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace krkrvita {
namespace {

std::size_t append_body(char* data, std::size_t size, std::size_t count, void* context) {
    const auto bytes = size * count;
    auto& output = *static_cast<std::vector<std::uint8_t>*>(context);
    output.insert(output.end(), reinterpret_cast<std::uint8_t*>(data),
                  reinterpret_cast<std::uint8_t*>(data) + bytes);
    return bytes;
}

void write_atomic(const std::filesystem::path& path,
                  const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot create " + temporary.string());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) throw std::runtime_error("cannot write " + temporary.string());
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::rename(temporary, path);
}

std::string digest(const std::vector<std::uint8_t>& bytes) {
    Sha256 hash;
    hash.update(bytes.data(), bytes.size());
    return Sha256::hex(hash.finish());
}

std::optional<std::string> digest_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    Sha256 hash;
    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0) hash.update(buffer.data(), static_cast<std::size_t>(count));
    }
    if (!stream.eof()) return std::nullopt;
    return Sha256::hex(hash.finish());
}

std::optional<std::string> recorded_digest(const std::filesystem::path& sidecar) {
    std::ifstream stream(sidecar);
    std::string value;
    stream >> value;
    if (!stream || value.size() != 64 ||
        !std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isxdigit(character) != 0;
        })) {
        return std::nullopt;
    }
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

bool verified_cache_file(const std::filesystem::path& path,
                         const std::filesystem::path& sidecar,
                         std::string* sum = nullptr) {
    if (!std::filesystem::is_regular_file(path) ||
        !std::filesystem::is_regular_file(sidecar)) return false;
    const auto recorded = recorded_digest(sidecar);
    const auto actual = digest_file(path);
    if (!recorded || !actual || *recorded != *actual) return false;
    if (sum) *sum = *actual;
    return true;
}

bool is_remote_reference(std::string_view reference) {
    return reference.substr(0, 8) == "https://";
}

std::filesystem::path cache_destination(const std::filesystem::path& cache_root,
                                        std::string_view reference) {
    const auto revision_root = cache_root / std::string(kPatchCommit);
    if (is_remote_reference(reference)) {
        Sha256 hash;
        hash.update(reference);
        const auto key = Sha256::hex(hash.finish()).substr(0, 24);
        const auto slash = reference.rfind('/');
        const std::string filename(reference.substr(slash + 1));
        return revision_root / "external" / key / filename;
    }
    constexpr std::string_view shared_prefix = "../patch/";
    if (reference.substr(0, shared_prefix.size()) == shared_prefix) {
        reference.remove_prefix(shared_prefix.size());
    }
    return revision_root / "patch" / std::filesystem::path(reference);
}

} // namespace

CurlHttpClient::CurlHttpClient() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

CurlHttpClient::~CurlHttpClient() {
    curl_global_cleanup();
}

HttpResult CurlHttpClient::get(std::string_view url) {
    HttpResult result;
    CURL* handle = curl_easy_init();
    if (!handle) {
        result.error = "curl initialization failed";
        return result;
    }
    const std::string owned_url(url);
    char error[CURL_ERROR_SIZE]{};
    curl_easy_setopt(handle, CURLOPT_URL, owned_url.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "KirikiriVita/0.1");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    const auto code = curl_easy_perform(handle);
    if (code != CURLE_OK) {
        result.error = error[0] ? error : curl_easy_strerror(code);
    }
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &result.status);
    curl_easy_cleanup(handle);
    return result;
}

PatchRepository::PatchRepository(std::filesystem::path cache_root)
    : cache_root_(std::move(cache_root)) {}

std::filesystem::path PatchRepository::manifest_path() const {
    return cache_root_ / std::string(kPatchCommit) / "alldata.js";
}

bool PatchRepository::update_manifest(HttpClient& http, std::string* error) const {
    try {
        const auto url = std::string(kPatchRawBase) + std::string(kPatchCommit) +
                         "/patch/alldata.js";
        auto response = http.get(url);
        if (!response.ok()) {
            throw std::runtime_error("manifest download failed (HTTP " +
                std::to_string(response.status) + "): " + response.error);
        }
        (void)PatchManifest::parse(std::string_view(
            reinterpret_cast<const char*>(response.body.data()), response.body.size()));
        write_atomic(manifest_path(), response.body);
        const auto sum = digest(response.body);
        std::vector<std::uint8_t> sum_bytes(sum.begin(), sum.end());
        sum_bytes.push_back('\n');
        write_atomic(manifest_path().string() + ".sha256", sum_bytes);
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

PatchManifest PatchRepository::load_manifest() const {
    const auto sidecar = manifest_path().string() + ".sha256";
    if (!verified_cache_file(manifest_path(), sidecar)) {
        throw std::runtime_error("cached patch manifest failed SHA-256 verification");
    }
    return PatchManifest::load(manifest_path());
}

std::vector<CachedPatchFile>
PatchRepository::cached_bundle(const PatchEntry& entry) const {
    std::vector<CachedPatchFile> files;
    for (const auto& relative : entry.files) {
        if (!is_safe_patch_path(relative)) continue;
        const auto destination = cache_destination(cache_root_, relative);
        const auto sidecar = destination.string() + ".sha256";
        std::string sum;
        if (!verified_cache_file(destination, sidecar, &sum)) return {};
        files.push_back({relative, destination, sum});
    }
    return files;
}

CachedPatchFile PatchRepository::fetch_one(HttpClient& http,
                                            std::string_view relative) const {
    if (!is_safe_patch_path(relative)) {
        throw std::runtime_error("patch bundle contains unsafe path: " + std::string(relative));
    }
    const auto destination = cache_destination(cache_root_, relative);
    const auto sidecar = destination.string() + ".sha256";
    std::string sum;
    if (verified_cache_file(destination, sidecar, &sum)) {
        return {std::string(relative), destination, sum};
    }
    auto response = http.get(patch_file_url(relative));
    if (!response.ok()) {
        throw std::runtime_error("patch download failed for " + std::string(relative) +
                                 " (HTTP " + std::to_string(response.status) + "): " +
                                 response.error);
    }
    const auto downloaded_sum = digest(response.body);
    write_atomic(destination, response.body);
    std::vector<std::uint8_t> sum_bytes(downloaded_sum.begin(), downloaded_sum.end());
    sum_bytes.push_back('\n');
    write_atomic(sidecar, sum_bytes);
    return {std::string(relative), destination, downloaded_sum};
}

std::vector<CachedPatchFile>
PatchRepository::fetch_bundle(HttpClient& http, const PatchEntry& entry) const {
    std::vector<CachedPatchFile> files;
    for (const auto& relative : entry.files) {
        if (!is_safe_patch_path(relative)) {
            continue;
        }
        files.push_back(fetch_one(http, relative));
    }
    if (files.empty()) {
        throw std::runtime_error("patch entry has no supported files");
    }
    return files;
}

} // namespace krkrvita

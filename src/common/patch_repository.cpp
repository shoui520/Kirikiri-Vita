#include "krkrvita/patch_repository.hpp"

#include "krkrvita/sha256.hpp"

#include <curl/curl.h>

#include <fstream>
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
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
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
    return PatchManifest::load(manifest_path());
}

std::vector<CachedPatchFile>
PatchRepository::cached_bundle(const PatchEntry& entry) const {
    std::vector<CachedPatchFile> files;
    for (const auto& relative : entry.files) {
        if (!is_safe_patch_path(relative)) continue;
        const auto destination = cache_root_ / std::string(kPatchCommit) / "patch" /
                                 std::filesystem::path(relative);
        const auto sidecar = destination.string() + ".sha256";
        if (!std::filesystem::is_regular_file(destination) ||
            !std::filesystem::is_regular_file(sidecar)) {
            return {};
        }
        std::ifstream digest_stream(sidecar);
        std::string sum;
        digest_stream >> sum;
        if (sum.size() != 64) return {};
        files.push_back({relative, destination, sum});
    }
    return files;
}

CachedPatchFile PatchRepository::fetch_one(HttpClient& http,
                                            std::string_view relative) const {
    if (!is_safe_patch_path(relative)) {
        throw std::runtime_error("patch bundle contains unsafe path: " + std::string(relative));
    }
    const auto destination = cache_root_ / std::string(kPatchCommit) / "patch" /
                             std::filesystem::path(relative);
    const auto sidecar = destination.string() + ".sha256";
    if (std::filesystem::is_regular_file(destination) &&
        std::filesystem::is_regular_file(sidecar)) {
        std::ifstream digest_stream(sidecar);
        std::string sum;
        digest_stream >> sum;
        return {std::string(relative), destination, sum};
    }
    auto response = http.get(patch_file_url(relative));
    if (!response.ok()) {
        throw std::runtime_error("patch download failed for " + std::string(relative) +
                                 " (HTTP " + std::to_string(response.status) + "): " +
                                 response.error);
    }
    const auto sum = digest(response.body);
    write_atomic(destination, response.body);
    std::vector<std::uint8_t> sum_bytes(sum.begin(), sum.end());
    sum_bytes.push_back('\n');
    write_atomic(sidecar, sum_bytes);
    return {std::string(relative), destination, sum};
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

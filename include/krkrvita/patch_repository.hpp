#pragma once

#include "krkrvita/patch_manifest.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace krkrvita {

struct HttpResult {
    long status = 0;
    std::vector<std::uint8_t> body;
    std::string error;

    bool ok() const { return status >= 200 && status < 300 && error.empty(); }
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResult get(std::string_view url) = 0;
};

class CurlHttpClient final : public HttpClient {
public:
    CurlHttpClient();
    ~CurlHttpClient() override;
    HttpResult get(std::string_view url) override;
};

struct CachedPatchFile {
    std::string relative_path;
    std::filesystem::path cache_path;
    std::string sha256;
};

class PatchRepository {
public:
    explicit PatchRepository(std::filesystem::path cache_root);

    std::filesystem::path manifest_path() const;
    bool update_manifest(HttpClient& http, std::string* error = nullptr) const;
    PatchManifest load_manifest() const;
    std::vector<CachedPatchFile> cached_bundle(const PatchEntry& entry) const;
    std::vector<CachedPatchFile> fetch_bundle(HttpClient& http,
                                               const PatchEntry& entry) const;

private:
    CachedPatchFile fetch_one(HttpClient& http, std::string_view relative) const;
    std::filesystem::path cache_root_;
};

} // namespace krkrvita

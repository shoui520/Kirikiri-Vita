#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace krkrvita {

// Parser for the signature envelope emitted by Kirikiri's sigcheck tools:
//   -- SIGNATURE - SHA256/PSS/RSA --\r\n
//   <base64 RSA signature>
// Keeping this separate from the OpenSSL verifier lets the file-format and
// failure contracts be tested without a retail executable or a Vita frontend.
bool decode_sha256_pss_rsa_signature(
    const std::uint8_t* envelope,
    std::size_t envelope_size,
    std::vector<std::uint8_t>& signature,
    std::string* error = nullptr);

class RsaPssSha256Verifier {
public:
    RsaPssSha256Verifier();
    ~RsaPssSha256Verifier();

    RsaPssSha256Verifier(const RsaPssSha256Verifier&) = delete;
    RsaPssSha256Verifier& operator=(const RsaPssSha256Verifier&) = delete;

    bool initialize(std::string_view public_key_pem, std::string* error = nullptr);
    bool update(const std::uint8_t* bytes, std::size_t size,
                std::string* error = nullptr);
    bool finish(const std::uint8_t* signature, std::size_t size,
                std::string* error = nullptr);

private:
    void clear();
    void* context_ = nullptr;
    void* key_ = nullptr;
    bool initialized_ = false;
    bool finished_ = false;
};

} // namespace krkrvita

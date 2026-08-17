#include "krkrvita/rsa_pss_signature.hpp"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <utility>

namespace krkrvita {
namespace {

constexpr std::string_view kSignatureHeader =
    "-- SIGNATURE - SHA256/PSS/RSA --";

void set_error(std::string* output, std::string message) {
    if (output) *output = std::move(message);
}

std::string openssl_error(std::string_view operation) {
    const unsigned long code = ERR_get_error();
    if (!code) return std::string(operation) + " failed";
    std::array<char, 256> text{};
    ERR_error_string_n(code, text.data(), text.size());
    return std::string(operation) + ": " + text.data();
}

EVP_PKEY* read_public_key(std::string_view pem) {
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) return nullptr;
    EVP_PKEY* key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (key) return key;

    // Some Kirikiri sigcheck releases emitted a PKCS#1 RSAPublicKey DER body
    // while retaining the generic "BEGIN PUBLIC KEY" PEM label.  OpenSSL 1.0
    // accepted this more readily than its current decoder.  Decode that exact
    // historical envelope explicitly, without weakening signature validation.
    ERR_clear_error();
    bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) return nullptr;
    char* name = nullptr;
    char* header = nullptr;
    unsigned char* der = nullptr;
    long der_size = 0;
    const int read = PEM_read_bio(bio, &name, &header, &der, &der_size);
    BIO_free(bio);
    if (read != 1 || !name || !der || der_size <= 0 ||
        (std::string_view(name) != "PUBLIC KEY" &&
         std::string_view(name) != "RSA PUBLIC KEY")) {
        OPENSSL_free(name);
        OPENSSL_free(header);
        OPENSSL_free(der);
        return nullptr;
    }
    const unsigned char* cursor = der;
    RSA* rsa = d2i_RSAPublicKey(nullptr, &cursor, der_size);
    const bool consumed = rsa && cursor == der + der_size;
    OPENSSL_free(name);
    OPENSSL_free(header);
    OPENSSL_free(der);
    if (!consumed) {
        RSA_free(rsa);
        return nullptr;
    }
    key = EVP_PKEY_new();
    if (!key || EVP_PKEY_assign_RSA(key, rsa) != 1) {
        EVP_PKEY_free(key);
        RSA_free(rsa);
        return nullptr;
    }
    return key;
}

int base64_value(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

} // namespace

bool decode_sha256_pss_rsa_signature(
    const std::uint8_t* envelope,
    std::size_t envelope_size,
    std::vector<std::uint8_t>& signature,
    std::string* error) {
    signature.clear();
    if (!envelope || envelope_size < kSignatureHeader.size() ||
        !std::equal(kSignatureHeader.begin(), kSignatureHeader.end(),
                    envelope)) {
        set_error(error, "Invalid signature file format");
        return false;
    }

    std::vector<unsigned char> encoded;
    encoded.reserve(envelope_size - kSignatureHeader.size());
    for (std::size_t index = kSignatureHeader.size(); index < envelope_size;
         ++index) {
        const auto ch = static_cast<unsigned char>(envelope[index]);
        if (std::isspace(ch)) continue;
        if (base64_value(ch) < 0 && ch != '=') {
            set_error(error, "Invalid base64 in signature file");
            return false;
        }
        encoded.push_back(ch);
    }
    if (encoded.empty() || (encoded.size() & 3u) != 0) {
        set_error(error, "Invalid base64 length in signature file");
        return false;
    }

    signature.reserve(encoded.size() / 4 * 3);
    for (std::size_t index = 0; index < encoded.size(); index += 4) {
        const bool final_group = index + 4 == encoded.size();
        const int a = base64_value(encoded[index]);
        const int b = base64_value(encoded[index + 1]);
        const int c = encoded[index + 2] == '='
            ? -2 : base64_value(encoded[index + 2]);
        const int d = encoded[index + 3] == '='
            ? -2 : base64_value(encoded[index + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1 ||
            (c == -2 && d != -2) ||
            (!final_group && (c == -2 || d == -2))) {
            set_error(error, "Invalid base64 padding in signature file");
            signature.clear();
            return false;
        }
        const std::uint32_t bits =
            (static_cast<std::uint32_t>(a) << 18) |
            (static_cast<std::uint32_t>(b) << 12) |
            (static_cast<std::uint32_t>(c < 0 ? 0 : c) << 6) |
            static_cast<std::uint32_t>(d < 0 ? 0 : d);
        signature.push_back(static_cast<std::uint8_t>(bits >> 16));
        if (c != -2) signature.push_back(static_cast<std::uint8_t>(bits >> 8));
        if (d != -2) signature.push_back(static_cast<std::uint8_t>(bits));
    }
    if (signature.empty()) {
        set_error(error, "Signature is empty");
        return false;
    }
    return true;
}

RsaPssSha256Verifier::RsaPssSha256Verifier() = default;

RsaPssSha256Verifier::~RsaPssSha256Verifier() { clear(); }

void RsaPssSha256Verifier::clear() {
    if (context_) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        EVP_MD_CTX_destroy(static_cast<EVP_MD_CTX*>(context_));
#else
        EVP_MD_CTX_free(static_cast<EVP_MD_CTX*>(context_));
#endif
    }
    if (key_) EVP_PKEY_free(static_cast<EVP_PKEY*>(key_));
    context_ = nullptr;
    key_ = nullptr;
    initialized_ = false;
    finished_ = false;
}

bool RsaPssSha256Verifier::initialize(std::string_view public_key_pem,
                                     std::string* error) {
    clear();
    if (public_key_pem.empty() ||
        public_key_pem.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        set_error(error, "Invalid public key length");
        return false;
    }
    ERR_clear_error();
    EVP_PKEY* key = read_public_key(public_key_pem);
    if (!key) {
        set_error(error, openssl_error("PEM_read_bio_PUBKEY"));
        return false;
    }
    EVP_MD_CTX* context =
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        EVP_MD_CTX_create();
#else
        EVP_MD_CTX_new();
#endif
    if (!context) {
        EVP_PKEY_free(key);
        set_error(error, openssl_error("EVP_MD_CTX_new"));
        return false;
    }
    EVP_PKEY_CTX* key_context = nullptr;
    if (EVP_DigestVerifyInit(context, &key_context, EVP_sha256(), nullptr,
                             key) != 1 ||
        !key_context ||
        EVP_PKEY_CTX_set_rsa_padding(key_context,
                                     RSA_PKCS1_PSS_PADDING) <= 0 ||
        EVP_PKEY_CTX_set_rsa_pss_saltlen(
            key_context, -1) <= 0) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        EVP_MD_CTX_destroy(context);
#else
        EVP_MD_CTX_free(context);
#endif
        EVP_PKEY_free(key);
        set_error(error, openssl_error("RSA-PSS verifier initialization"));
        return false;
    }
    context_ = context;
    key_ = key;
    initialized_ = true;
    return true;
}

bool RsaPssSha256Verifier::update(const std::uint8_t* bytes,
                                  std::size_t size,
                                  std::string* error) {
    if (!initialized_ || finished_) {
        set_error(error, "RSA-PSS verifier is not active");
        return false;
    }
    if (size != 0 && (!bytes ||
        EVP_DigestVerifyUpdate(static_cast<EVP_MD_CTX*>(context_),
                               bytes, size) != 1)) {
        set_error(error, openssl_error("EVP_DigestVerifyUpdate"));
        return false;
    }
    return true;
}

bool RsaPssSha256Verifier::finish(
    const std::uint8_t* signature, std::size_t size, std::string* error) {
    if (!initialized_ || finished_ || !signature || size == 0) {
        set_error(error, "RSA-PSS verifier is not ready");
        return false;
    }
    finished_ = true;
    const int result = EVP_DigestVerifyFinal(
        static_cast<EVP_MD_CTX*>(context_), signature, size);
    if (result != 1) {
        set_error(error, result == 0 ? "Signature verification failed"
                                     : openssl_error("EVP_DigestVerifyFinal"));
        return false;
    }
    return true;
}

} // namespace krkrvita

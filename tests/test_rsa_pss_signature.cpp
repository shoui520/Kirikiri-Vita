#include "krkrvita/rsa_pss_signature.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot open " + path.string());
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    require(end >= 0, "cannot size " + path.string());
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    require(input.good() || input.eof(), "cannot read " + path.string());
    require(static_cast<std::size_t>(input.gcount()) == bytes.size(),
            "short read from " + path.string());
    return bytes;
}

void verify_stream(const std::filesystem::path& path,
                   std::string_view public_key,
    const std::vector<std::uint8_t>& signature) {
    krkrvita::RsaPssSha256Verifier verifier;
    std::string error;
    require(verifier.initialize(public_key, &error), error);
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot open signed payload");
    std::array<std::uint8_t, 65536> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const auto amount = input.gcount();
        if (amount > 0) {
            require(verifier.update(buffer.data(),
                                    static_cast<std::size_t>(amount), &error),
                    error);
        }
    }
    require(input.eof(), "signed payload read failed");
    require(verifier.finish(signature.data(), signature.size(), &error), error);
}

} // namespace

int main() {
    try {
        constexpr std::string_view public_key =
            "-----BEGIN PUBLIC KEY-----\n"
            "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQC+1kdOYr+n64opY2jyAW/s0UZj\n"
            "pSnn6KVe9759Gv/dqkspu2OWOn/kQBzzgONwL6nhU+k66BUS74jbJXER/iqlStXE\n"
            "wRzFmPO7j2tW3nHLYuB1T7xpQ7tNXiTH6ZNLe5aU4Mwx5QrdRsUf9ONeJM1u10Ig\n"
            "PdzmWtHwBf4X4Sh9NQIDAQAB\n"
            "-----END PUBLIC KEY-----\n";
        constexpr std::string_view envelope =
            "-- SIGNATURE - SHA256/PSS/RSA --\r\n"
            "FrKV7oJAs7296Xf/b9PO1ob16JvPgQxOR7sTb5xHsupB+jNzG7jIDYDx6dJbD8O+UPXdiY4lHJbF\r\n"
            "ynF6VDcioDPlMUACGZ/KtEbZ3nCYQWaP24dyppvJSRYi6zgKSBqK0krbuPEE3J8dzMKEgqmPtZTV\r\n"
            "Uarde7x2W0Ows/MlJso=\r\n";
        constexpr std::string_view message =
            "krkrvita rsa-pss streaming compatibility fixture\n";

        std::vector<std::uint8_t> signature;
        std::string error;
        require(krkrvita::decode_sha256_pss_rsa_signature(
                    reinterpret_cast<const std::uint8_t*>(envelope.data()),
                    envelope.size(), signature, &error),
                error);
        require(signature.size() == 128, "RSA-1024 signature size changed");

        krkrvita::RsaPssSha256Verifier verifier;
        require(verifier.initialize(public_key, &error), error);
        const auto* message_bytes =
            reinterpret_cast<const std::uint8_t*>(message.data());
        require(verifier.update(message_bytes, 7, &error), error);
        require(verifier.update(message_bytes + 7, message.size() - 7, &error),
                error);
        require(verifier.finish(signature.data(), signature.size(), &error),
                error);

        krkrvita::RsaPssSha256Verifier tampered;
        require(tampered.initialize(public_key, &error), error);
        std::string changed(message);
        changed[0] ^= 1;
        require(tampered.update(
                    reinterpret_cast<const std::uint8_t*>(changed.data()),
                    changed.size(), &error),
                error);
        require(!tampered.finish(signature.data(), signature.size(), &error),
                "tampered payload passed RSA-PSS verification");

        std::vector<std::uint8_t> rejected;
        const std::array<std::uint8_t, 4> malformed = {'b', 'a', 'd', '!'};
        require(!krkrvita::decode_sha256_pss_rsa_signature(
                    malformed.data(), malformed.size(), rejected, &error),
                "malformed signature envelope was accepted");

        const char* corpus =
            std::getenv("KRKRVITA_TEST_SIGNED_ARCHIVE_DIR");
        if (corpus && *corpus) {
            const std::filesystem::path retail = corpus;
            require(std::filesystem::exists(retail / "data.xp3") &&
                        std::filesystem::exists(retail / "data.xp3.sig"),
                    "signed archive corpus is incomplete");
            constexpr std::string_view retail_key =
                "-----BEGIN PUBLIC KEY-----\n"
                "MIGJAoGBAM9SZJzFoJNvGMjW7Ag2fHpHHZnZwmoc0LIzl5sCenvp+sShikO22lQs\n"
                "lOguG8vPzqoQkjPIJIw+HiZRRtZR7mlEYHupgh1FKWcqAn+S15NHWHKvLkFRyyGc\n"
                "mms/pQHGvSeRV/pZrGdbfY0icSOOhm2VwIU3Ba5vTZJjzQJleZYfAgMBAAE=\n"
                "-----END PUBLIC KEY-----\n";
            const auto retail_envelope = read_file(retail / "data.xp3.sig");
            require(krkrvita::decode_sha256_pss_rsa_signature(
                        retail_envelope.data(), retail_envelope.size(),
                        signature, &error),
                    error);
            verify_stream(retail / "data.xp3", retail_key, signature);
        }
        std::cout << "RSA-PSS signature contracts passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

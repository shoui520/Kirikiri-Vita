#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace krkrvita {

// Decodes Kirikiri text streams: UTF-8, UTF-16LE, the two FE FE ciphers, and
// the zlib-compressed FE FE stream used by compiled retail script sets.
bool decode_kirikiri_text(std::span<const std::uint8_t> bytes,
                          std::string& utf8,
                          std::string* error = nullptr);

} // namespace krkrvita

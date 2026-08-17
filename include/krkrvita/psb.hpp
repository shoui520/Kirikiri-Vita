#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace krkrvita {

struct PsbValue;
using PsbValuePtr = std::shared_ptr<PsbValue>;

struct PsbValue {
    enum class Type {
        null_value,
        boolean,
        integer,
        real,
        string,
        binary,
        array,
        object,
    };

    Type type = Type::null_value;
    bool boolean = false;
    std::int64_t integer = 0;
    double real = 0.0;
    std::string string;
    std::shared_ptr<const std::vector<std::uint8_t>> binary;
    std::vector<PsbValuePtr> array;
    std::vector<std::pair<std::string, PsbValuePtr>> object;
};

struct PsbDocument {
    std::uint16_t version = 0;
    bool mdf_compressed = false;
    PsbValuePtr root;
};

// Parses PSB v2-v4 values and the common MDF/zlib envelope produced by the
// original psbfile.dll toolchain. All table offsets, array widths, recursion,
// and decompressed sizes are fail-closed before any allocation or dereference.
bool parse_psb(const std::uint8_t* bytes, std::size_t size,
               PsbDocument& document, std::string* error = nullptr);

} // namespace krkrvita

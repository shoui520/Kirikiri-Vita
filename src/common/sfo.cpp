#include "krkrvita/sfo.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace krkrvita {
namespace {

void append16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
}

void append32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) output.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

std::size_t align4(std::size_t value) { return (value + 3) & ~std::size_t(3); }

} // namespace

void ParamSfo::set(std::string key, std::string value) {
    values_[std::move(key)] = std::move(value);
}

void ParamSfo::set(std::string key, std::uint32_t value) {
    values_[std::move(key)] = value;
}

std::vector<std::uint8_t> ParamSfo::encode() const {
    struct Index {
        std::uint16_t key_offset;
        std::uint16_t format;
        std::uint32_t length;
        std::uint32_t maximum;
        std::uint32_t data_offset;
    };
    std::vector<std::uint8_t> keys;
    std::vector<std::uint8_t> data;
    std::vector<Index> indices;
    for (const auto& [key, value] : values_) {
        const auto key_offset = static_cast<std::uint16_t>(keys.size());
        keys.insert(keys.end(), key.begin(), key.end());
        keys.push_back(0);
        data.resize(align4(data.size()), 0);
        const auto data_offset = static_cast<std::uint32_t>(data.size());
        if (const auto* text = std::get_if<std::string>(&value)) {
            const auto length = static_cast<std::uint32_t>(text->size() + 1);
            auto maximum = static_cast<std::uint32_t>(align4(std::max<std::size_t>(length, 4)));
            if (key == "TITLE") maximum = std::max<std::uint32_t>(maximum, 128);
            if (key == "STITLE") maximum = std::max<std::uint32_t>(maximum, 52);
            data.insert(data.end(), text->begin(), text->end());
            data.push_back(0);
            data.resize(data_offset + maximum, 0);
            indices.push_back({key_offset, 0x0204, length, maximum, data_offset});
        } else {
            const auto integer = std::get<std::uint32_t>(value);
            append32(data, integer);
            indices.push_back({key_offset, 0x0404, 4, 4, data_offset});
        }
    }
    keys.resize(align4(keys.size()), 0);
    const auto key_table = static_cast<std::uint32_t>(20 + indices.size() * 16);
    const auto data_table = key_table + static_cast<std::uint32_t>(keys.size());

    std::vector<std::uint8_t> output;
    append32(output, 0x46535000); // \0PSF
    append32(output, 0x00000101);
    append32(output, key_table);
    append32(output, data_table);
    append32(output, static_cast<std::uint32_t>(indices.size()));
    for (const auto& index : indices) {
        append16(output, index.key_offset);
        append16(output, index.format);
        append32(output, index.length);
        append32(output, index.maximum);
        append32(output, index.data_offset);
    }
    output.insert(output.end(), keys.begin(), keys.end());
    output.insert(output.end(), data.begin(), data.end());
    return output;
}

bool ParamSfo::write(const std::filesystem::path& path, std::string* error) const {
    try {
        std::filesystem::create_directories(path.parent_path());
        const auto bytes = encode();
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("cannot create PARAM.SFO");
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!stream) throw std::runtime_error("cannot write PARAM.SFO");
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

ParamSfo ParamSfo::bubble(std::string title, std::string title_id,
                          std::string version) {
    ParamSfo sfo;
    sfo.set("APP_VER", version);
    sfo.set("ATTRIBUTE", std::uint32_t{0});
    sfo.set("BOOT_FILE", "eboot.bin");
    sfo.set("CATEGORY", "gd");
    sfo.set("CONTENT_ID", "KRKRVITA-" + title_id + "_00-0000000000000000");
    sfo.set("EBOOT_APP_MEMSIZE", std::uint32_t{0x10000000});
    sfo.set("FORMAT", "obs");
    sfo.set("PARENTAL_LEVEL", std::uint32_t{1});
    sfo.set("PSP2_DISP_VER", "00.000");
    sfo.set("STITLE", title);
    sfo.set("TITLE", std::move(title));
    sfo.set("TITLE_ID", std::move(title_id));
    sfo.set("VERSION", std::move(version));
    return sfo;
}

} // namespace krkrvita


#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace krkrvita {

class ParamSfo {
public:
    using Value = std::variant<std::string, std::uint32_t>;

    void set(std::string key, std::string value);
    void set(std::string key, std::uint32_t value);
    std::vector<std::uint8_t> encode() const;
    bool write(const std::filesystem::path& path, std::string* error = nullptr) const;

    static ParamSfo bubble(std::string title, std::string title_id,
                           std::string version = "01.00");

private:
    std::map<std::string, Value> values_;
};

} // namespace krkrvita


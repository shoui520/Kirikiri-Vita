#include "krkrvita/kag_inline_script.hpp"
#include "tjs.h"
#include "tjsError.h"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class MemoryBinaryStream final : public TJS::tTJSBinaryStream {
public:
    TJS::tjs_uint64 TJS_INTF_METHOD Seek(TJS::tjs_int64 offset,
                                         TJS::tjs_int whence) override {
        TJS::tjs_int64 base = 0;
        if (whence == TJS_BS_SEEK_CUR) base = position_;
        else if (whence == TJS_BS_SEEK_END) base = bytes_.size();
        const auto next = base + offset;
        if (next < 0) throw std::runtime_error("negative bytecode seek");
        position_ = static_cast<std::size_t>(next);
        return position_;
    }
    TJS::tjs_uint TJS_INTF_METHOD Read(void* output,
                                       TJS::tjs_uint size) override {
        const auto available = position_ < bytes_.size()
            ? bytes_.size() - position_ : 0;
        const auto amount = std::min<std::size_t>(available, size);
        if (amount) std::memcpy(output, bytes_.data() + position_, amount);
        position_ += amount;
        return static_cast<TJS::tjs_uint>(amount);
    }
    TJS::tjs_uint TJS_INTF_METHOD Write(const void* input,
                                        TJS::tjs_uint size) override {
        if (size > std::numeric_limits<std::size_t>::max() - position_)
            throw std::length_error("bytecode output overflow");
        const auto end = position_ + size;
        if (end > bytes_.size()) bytes_.resize(end);
        if (size) std::memcpy(bytes_.data() + position_, input, size);
        position_ = end;
        return size;
    }
    void TJS_INTF_METHOD SetEndOfStorage() override { bytes_.resize(position_); }
    TJS::tjs_uint64 TJS_INTF_METHOD GetSize() override { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open probe " + path.string());
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

std::vector<std::string> split(std::string_view line) {
    std::vector<std::string> fields;
    std::size_t cursor = 0;
    while (cursor <= line.size()) {
        const auto separator = line.find('|', cursor);
        fields.emplace_back(line.substr(cursor,
            separator == std::string_view::npos ? line.size() - cursor
                                                : separator - cursor));
        if (separator == std::string_view::npos) break;
        cursor = separator + 1;
    }
    return fields;
}

void runtime_smoke(TJS::tTJS& engine) {
    TJS::tTJSVariant result;
    engine.EvalExpression(TJS_W("(function(){var q=2221053586956454128;"
        "q=(q<<23)|(q>>>8);var d=%[a:3,b:5];return q+d.a+d.b;})()"),
        &result);
    if (result.AsInteger() != INT64_C(4332131632849724816))
        throw std::runtime_error("ARMv7 TJS runtime smoke result changed: " +
                                 std::to_string(result.AsInteger()));

    std::vector<std::u16string> lines(737, u"retail inline script line");
    const auto script = krkrvita::assemble_kag_inline_script<char16_t>(
        0, lines.size(), [&](std::size_t line) { return lines[line].c_str(); });
    if (script.size() != lines.size() * (lines.front().size() + 2))
        throw std::runtime_error("ARMv7 KAG assembly smoke result changed");
}

} // namespace

int main() {
    try {
        const char* data_root = std::getenv("DATA_ROOT");
        if (!data_root) throw std::runtime_error("DATA_ROOT is not set");
        const std::filesystem::path root(data_root);
        const auto manifest_path = root / "manifest.txt";
        std::ifstream manifest(manifest_path);
        if (!manifest) throw std::runtime_error("probe manifest is missing");

        const auto release = [](TJS::tTJS* engine) {
            if (engine) engine->Release();
        };
        std::unique_ptr<TJS::tTJS, decltype(release)>
            engine(new TJS::tTJS(), release);
        runtime_smoke(*engine);

        std::size_t probes = 0;
        std::string line;
        while (std::getline(manifest, line)) {
            if (line.empty() || line.front() == '#') continue;
            const auto fields = split(line);
            if (fields.size() != 4 || fields[0] != "compile")
                throw std::runtime_error("invalid probe manifest row");
            const std::filesystem::path relative(fields[1]);
            if (relative.is_absolute() || fields[1].find("..") != std::string::npos ||
                relative.extension() != ".tjs")
                throw std::runtime_error("unsafe probe path");
            const auto expected_bytes = std::stoull(fields[2]);
            const auto maximum_us = std::stoull(fields[3]);
            const auto source = read_file(root / relative);
            if (source.size() != expected_bytes)
                throw std::runtime_error("probe byte count changed: " + fields[1]);

            MemoryBinaryStream bytecode;
            const auto begin = std::chrono::steady_clock::now();
            engine->CompileScript(TJS::ttstr(source).c_str(), &bytecode, false,
                                  false, false, TJS::ttstr(fields[1]).c_str(), 0);
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - begin).count();
            if (maximum_us && static_cast<std::uint64_t>(elapsed) > maximum_us)
                throw std::runtime_error("probe exceeded ARMv7 budget: " + fields[1]);
            std::cout << "probe " << fields[1] << " bytes=" << source.size()
                      << " compile_us=" << elapsed << '\n';
            ++probes;
        }
        if (!probes) throw std::runtime_error("probe manifest is empty");
        std::cout << "ARMv7 retail runtime probes passed: " << probes << '\n';
        return 0;
    } catch (const TJS::eTJS& exception) {
        std::cerr << TJS::ttstr(exception.GetMessage()).AsStdString() << '\n';
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

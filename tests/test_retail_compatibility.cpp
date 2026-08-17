#include "krkrvita/game.hpp"
#include "krkrvita/native_plugin_inventory.hpp"
#include "krkrvita/phase1_filter.hpp"
#include "krkrvita/psb.hpp"
#include "krkrvita/text_codec.hpp"
#include "krkrvita/xp3_archive.hpp"
#include "krkrvita/xp3_filter_vm.hpp"
#include "krkrvita/yuri_plugin_capabilities.hpp"
#include "krkrvita/layerexdraw_surface.hpp"
#include "krkrvita/motionplayer_surface.hpp"
#include "krkrvita/scriptsex_surface.hpp"
#include "tjs.h"
#include "tjsError.h"

#include <iconv.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace krkrvita;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string ascii_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string extension_of(std::string_view name) {
    const auto slash = name.find_last_of("/\\");
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos ||
        (slash != std::string_view::npos && dot < slash)) return {};
    return ascii_lower(std::string(name.substr(dot)));
}

bool valid_utf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) continue;
        unsigned continuation = 0;
        std::uint32_t codepoint = 0;
        if ((lead & 0xe0) == 0xc0) {
            continuation = 1;
            codepoint = lead & 0x1f;
            if (codepoint < 2) return false;
        } else if ((lead & 0xf0) == 0xe0) {
            continuation = 2;
            codepoint = lead & 0x0f;
        } else if ((lead & 0xf8) == 0xf0) {
            continuation = 3;
            codepoint = lead & 0x07;
        } else {
            return false;
        }
        if (text.size() - i < continuation) return false;
        for (unsigned n = 0; n < continuation; ++n) {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if ((continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}

std::string cp932_to_utf8(std::string_view input) {
    iconv_t converter = iconv_open("UTF-8", "CP932");
    require(converter != reinterpret_cast<iconv_t>(-1),
            "host iconv does not support CP932");
    const auto close_converter = [](iconv_t* value) {
        if (*value != reinterpret_cast<iconv_t>(-1)) iconv_close(*value);
    };
    std::unique_ptr<iconv_t, decltype(close_converter)>
        guard(&converter, close_converter);

    std::string output(std::max<std::size_t>(64, input.size() * 3), '\0');
    char* source = const_cast<char*>(input.data());
    std::size_t source_left = input.size();
    char* destination = output.data();
    std::size_t destination_left = output.size();
    while (source_left) {
        if (iconv(converter, &source, &source_left,
                  &destination, &destination_left) != static_cast<std::size_t>(-1)) {
            continue;
        }
        if (errno != E2BIG) {
            throw std::runtime_error("invalid CP932 retail script at byte " +
                                     std::to_string(input.size() - source_left));
        }
        const auto written = static_cast<std::size_t>(destination - output.data());
        output.resize(output.size() * 2);
        destination = output.data() + written;
        destination_left = output.size() - written;
    }
    output.resize(static_cast<std::size_t>(destination - output.data()));
    return output;
}

std::string decode_retail_text(std::span<const std::uint8_t> bytes,
                               std::string_view name) {
    std::string source;
    std::string error;
    require(decode_kirikiri_text(bytes, source, &error),
            "cannot decode " + std::string(name) + ": " + error);
    if (!valid_utf8(source)) source = cp932_to_utf8(source);
    require(valid_utf8(source), "decoded text is not UTF-8: " + std::string(name));
    return source;
}

class MemoryBinaryStream final : public TJS::tTJSBinaryStream {
public:
    tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) override {
        tjs_int64 base = 0;
        if (whence == TJS_BS_SEEK_CUR) base = static_cast<tjs_int64>(position_);
        else if (whence == TJS_BS_SEEK_END)
            base = static_cast<tjs_int64>(bytes_.size());
        const auto next = base + offset;
        if (next < 0) throw std::runtime_error("negative TJS bytecode seek");
        position_ = static_cast<std::size_t>(next);
        return position_;
    }

    tjs_uint TJS_INTF_METHOD Read(void* output, tjs_uint size) override {
        const auto available = position_ < bytes_.size()
            ? bytes_.size() - position_ : 0;
        const auto amount = std::min<std::size_t>(size, available);
        if (amount) std::memcpy(output, bytes_.data() + position_, amount);
        position_ += amount;
        return static_cast<tjs_uint>(amount);
    }

    tjs_uint TJS_INTF_METHOD Write(const void* input, tjs_uint size) override {
        require(size <= std::numeric_limits<std::size_t>::max() - position_,
                "TJS bytecode output overflow");
        const auto end = position_ + size;
        if (end > bytes_.size()) bytes_.resize(end);
        if (size) std::memcpy(bytes_.data() + position_, input, size);
        position_ = end;
        return size;
    }

    void TJS_INTF_METHOD SetEndOfStorage() override { bytes_.resize(position_); }
    tjs_uint64 TJS_INTF_METHOD GetSize() override { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

std::string compile_tjs_error(TJS::tTJS& engine, std::string_view source,
                              std::string_view name,
                              bool expression = false) {
    MemoryBinaryStream bytecode;
    const TJS::ttstr script{std::string(source)};
    const TJS::ttstr script_name{std::string(name)};
    try {
        engine.CompileScript(script.c_str(), &bytecode, false, false, expression,
                             script_name.c_str(), 0);
    } catch (const TJS::eTJS& exception) {
        return TJS::ttstr(exception.GetMessage()).AsStdString();
    }
    // A comment-only or otherwise empty TJS unit legitimately emits no
    // bytecode. Successful compilation, rather than output size, is the
    // compatibility contract.
    return {};
}

std::string_view trim(std::string_view line) {
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
        line.remove_prefix(1);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
        line.remove_suffix(1);
    return line;
}

bool is_tag(std::string_view line, std::string_view bracketed,
            std::string_view command) {
    line = trim(line);
    const auto lower = ascii_lower(std::string(line));
    return lower == bracketed || lower == command ||
           lower == std::string(bracketed) + "\\";
}

struct InlineCompileResult {
    std::size_t blocks = 0;
    std::vector<std::string> failures;
    std::vector<std::string> sources;
};

InlineCompileResult compile_kag_iscripts(TJS::tTJS& engine,
                                         std::string_view scenario,
                                         std::string_view name,
                                         bool retain_sources = false) {
    std::size_t cursor = 0;
    std::size_t line_number = 1;
    InlineCompileResult result;
    while (cursor <= scenario.size()) {
        const auto end = scenario.find('\n', cursor);
        const auto line_end = end == std::string_view::npos ? scenario.size() : end;
        auto line = scenario.substr(cursor, line_end - cursor);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (is_tag(line, "[iscript]", "@iscript")) {
            const auto script_line = line_number + 1;
            std::string script;
            bool closed = false;
            cursor = end == std::string_view::npos ? scenario.size() + 1 : end + 1;
            ++line_number;
            while (cursor <= scenario.size()) {
                const auto script_end = scenario.find('\n', cursor);
                const auto script_line_end = script_end == std::string_view::npos
                    ? scenario.size() : script_end;
                auto script_part = scenario.substr(cursor, script_line_end - cursor);
                if (!script_part.empty() && script_part.back() == '\r')
                    script_part.remove_suffix(1);
                if (is_tag(script_part, "[endscript]", "@endscript")) {
                    closed = true;
                    cursor = script_end == std::string_view::npos
                        ? scenario.size() + 1 : script_end + 1;
                    ++line_number;
                    break;
                }
                script.append(script_part);
                script.append("\r\n");
                cursor = script_end == std::string_view::npos
                    ? scenario.size() + 1 : script_end + 1;
                ++line_number;
            }
            require(closed, "unterminated [iscript] in " + std::string(name) +
                            ":" + std::to_string(script_line - 1));
            const auto unit_name = std::string(name) + ":" +
                                   std::to_string(script_line);
            if (auto failure = compile_tjs_error(engine, script, unit_name);
                !failure.empty()) {
                result.failures.push_back(unit_name + ": " + failure);
            }
            if (retain_sources) result.sources.push_back(script);
            ++result.blocks;
            continue;
        }
        if (is_tag(line, "[endscript]", "@endscript")) {
            throw std::runtime_error("orphan [endscript] in " +
                                     std::string(name) + ":" +
                                     std::to_string(line_number));
        }
        if (end == std::string_view::npos) break;
        cursor = end + 1;
        ++line_number;
    }
    return result;
}

std::string normalize_storage_name(std::string_view value) {
    auto result = ascii_lower(std::string(trim(value)));
    std::replace(result.begin(), result.end(), '\\', '/');
    while (result.starts_with("./")) result.erase(0, 2);
    while (!result.empty() && result.front() == '/') result.erase(0, 1);
    return result;
}

// Preserve offsets and quoted arguments while removing TJS comments.  Raw
// substring searches otherwise turn disabled compatibility code and examples
// into fake boot dependencies/plugin requirements.
std::string mask_tjs_comments(std::string_view source) {
    std::string masked(source);
    char quote = 0;
    bool escape = false;
    bool line_comment = false;
    bool block_comment = false;
    for (std::size_t index = 0; index < source.size(); ++index) {
        const char ch = source[index];
        const char next = index + 1 < source.size() ? source[index + 1] : 0;
        if (line_comment) {
            if (ch == '\n') {
                line_comment = false;
            } else {
                masked[index] = ' ';
            }
            continue;
        }
        if (block_comment) {
            if (ch == '*' && next == '/') {
                masked[index] = masked[index + 1] = ' ';
                ++index;
                block_comment = false;
            } else if (ch != '\n' && ch != '\r') {
                masked[index] = ' ';
            }
            continue;
        }
        if (quote) {
            if (escape) escape = false;
            else if (ch == '\\') escape = true;
            else if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }
        if (ch == '/' && next == '/') {
            masked[index] = masked[index + 1] = ' ';
            ++index;
            line_comment = true;
        } else if (ch == '/' && next == '*') {
            masked[index] = masked[index + 1] = ' ';
            ++index;
            block_comment = true;
        }
    }
    return masked;
}

std::vector<std::string> collect_scenario_dependencies(std::string_view source) {
    std::vector<std::string> dependencies;
    std::size_t cursor = 0;
    while (cursor <= source.size()) {
        const auto end = source.find('\n', cursor);
        const auto line_end = end == std::string_view::npos ? source.size() : end;
        auto line = trim(source.substr(cursor, line_end - cursor));
        const auto lower = ascii_lower(std::string(line));
        const bool call_or_jump = lower.starts_with("@call") ||
            lower.starts_with("@jump") || lower.starts_with("[call") ||
            lower.starts_with("[jump");
        if (call_or_jump) {
            const auto storage = lower.find("storage");
            if (storage != std::string::npos) {
                auto value = lower.find('=', storage + 7);
                if (value != std::string::npos) {
                    ++value;
                    while (value < line.size() &&
                           std::isspace(static_cast<unsigned char>(line[value]))) {
                        ++value;
                    }
                    std::size_t finish = value;
                    if (value < line.size() &&
                        (line[value] == '\'' || line[value] == '"')) {
                        const auto quote = line[value++];
                        finish = line.find(quote, value);
                    } else {
                        finish = line.find_first_of(" \t\r]", value);
                    }
                    if (finish == std::string_view::npos) finish = line.size();
                    auto dependency = normalize_storage_name(
                        line.substr(value, finish - value));
                    if (!dependency.empty() && dependency.find('%') == std::string::npos &&
                        dependency.find('&') == std::string::npos) {
                        if (extension_of(dependency).empty()) dependency += ".ks";
                        dependencies.push_back(std::move(dependency));
                    }
                }
            }
        }
        if (end == std::string_view::npos) break;
        cursor = end + 1;
    }
    return dependencies;
}

std::vector<std::string> collect_tjs_dependencies(std::string_view source) {
    static constexpr std::array<std::string_view, 6> loaders = {
        "kagloadscript(", "kagloadscriptonce(", "scripts.execstorage(",
        "scripts.evalstorage(", "scripts.compilestorage(", "evalstorage("};
    const auto lower = ascii_lower(mask_tjs_comments(source));
    std::vector<std::string> dependencies;
    for (const auto loader : loaders) {
        std::size_t cursor = 0;
        while ((cursor = lower.find(loader, cursor)) != std::string::npos) {
            cursor += loader.size();
            while (cursor < source.size() &&
                   std::isspace(static_cast<unsigned char>(source[cursor]))) {
                ++cursor;
            }
            if (cursor >= source.size() ||
                (source[cursor] != '\'' && source[cursor] != '"')) continue;
            const auto quote = source[cursor++];
            const auto finish = source.find(quote, cursor);
            if (finish == std::string_view::npos) break;
            auto dependency = normalize_storage_name(
                source.substr(cursor, finish - cursor));
            if (!dependency.empty()) dependencies.push_back(std::move(dependency));
            cursor = finish + 1;
        }
    }
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                       dependencies.end());
    return dependencies;
}

struct PluginRequest {
    std::string module;
    std::string source;
    std::size_t line = 0;
    bool optional = false;
    bool scenario_tag = false;
};

std::size_t source_line_at(std::string_view source, std::size_t offset) {
    return 1 + static_cast<std::size_t>(
        std::count(source.begin(), source.begin() + offset, '\n'));
}

std::string normalize_plugin_module(std::string module) {
    std::replace(module.begin(), module.end(), '\\', '/');
    if (const auto slash = module.find_last_of('/'); slash != std::string::npos)
        module.erase(0, slash + 1);
    module = ascii_lower(std::move(module));
    if (!module.empty() && extension_of(module).empty()) module += ".dll";
    return module;
}

std::size_t matching_brace(std::string_view source, std::size_t open) {
    std::size_t depth = 0;
    char quote = 0;
    bool escape = false;
    bool line_comment = false;
    bool block_comment = false;
    for (std::size_t index = open; index < source.size(); ++index) {
        const char ch = source[index];
        const char next = index + 1 < source.size() ? source[index + 1] : 0;
        if (line_comment) {
            if (ch == '\n') line_comment = false;
            continue;
        }
        if (block_comment) {
            if (ch == '*' && next == '/') {
                block_comment = false;
                ++index;
            }
            continue;
        }
        if (quote) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == quote) {
                quote = 0;
            }
            continue;
        }
        if (ch == '/' && next == '/') {
            line_comment = true;
            ++index;
            continue;
        }
        if (ch == '/' && next == '*') {
            block_comment = true;
            ++index;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }
        if (ch == '{') ++depth;
        if (ch == '}' && --depth == 0) return index;
    }
    return std::string_view::npos;
}

bool request_is_caught(std::string_view source, std::string_view lower,
                       std::size_t request_offset) {
    auto search = request_offset;
    while (search) {
        const auto try_pos = lower.rfind("try", search - 1);
        if (try_pos == std::string_view::npos) return false;
        const bool word_start = try_pos == 0 ||
            !std::isalnum(static_cast<unsigned char>(lower[try_pos - 1]));
        const auto after_try = try_pos + 3;
        const bool word_end = after_try == lower.size() ||
            !std::isalnum(static_cast<unsigned char>(lower[after_try]));
        const auto open = lower.find('{', after_try);
        if (word_start && word_end && open != std::string_view::npos &&
            open < request_offset) {
            const auto close = matching_brace(source, open);
            if (close != std::string_view::npos && close > request_offset) {
                auto cursor = close + 1;
                while (cursor < lower.size() &&
                       std::isspace(static_cast<unsigned char>(lower[cursor]))) {
                    ++cursor;
                }
                if (lower.substr(cursor, 5) == "catch") return true;
            }
        }
        search = try_pos;
    }
    return false;
}

void collect_quoted_plugin(std::string_view source, std::string_view source_name,
                           std::string_view needle,
                           std::set<std::string>& plugins,
                           std::vector<PluginRequest>& requests) {
    std::size_t cursor = 0;
    const auto lower = ascii_lower(mask_tjs_comments(source));
    while ((cursor = lower.find(needle, cursor)) != std::string::npos) {
        const auto request_offset = cursor;
        cursor += needle.size();
        while (cursor < source.size() &&
               std::isspace(static_cast<unsigned char>(source[cursor]))) ++cursor;
        if (cursor >= source.size() ||
            (source[cursor] != '\'' && source[cursor] != '"')) continue;
        const char quote = source[cursor++];
        const auto finish = source.find(quote, cursor);
        if (finish == std::string_view::npos) continue;
        auto plugin = normalize_plugin_module(
            std::string(source.substr(cursor, finish - cursor)));
        plugins.insert(plugin);
        const auto line_begin_pos = source.rfind('\n', request_offset);
        const auto line_begin = line_begin_pos == std::string_view::npos
            ? 0 : line_begin_pos + 1;
        const auto line_end_pos = source.find('\n', finish);
        const auto line_end = line_end_pos == std::string_view::npos
            ? source.size() : line_end_pos;
        const auto line = ascii_lower(
            std::string(source.substr(line_begin, line_end - line_begin)));
        const auto within_line = request_offset - line_begin;
        bool optional = line.find("try") < within_line &&
            line.find("catch", within_line) != std::string::npos;
        if (!optional)
            optional = request_is_caught(source, lower, request_offset);
        requests.push_back(PluginRequest{std::move(plugin),
                                         std::string(source_name),
                                         source_line_at(source, request_offset),
                                         optional, false});
        cursor = finish + 1;
    }
}

void collect_plugin_requests(std::string_view source,
                             std::string_view source_name,
                             std::set<std::string>& plugins,
                             std::vector<PluginRequest>& requests) {
    collect_quoted_plugin(source, source_name, "plugins.link(", plugins,
                          requests);
    std::size_t cursor = 0;
    std::size_t line_number = 1;
    while (cursor <= source.size()) {
        const auto newline = source.find('\n', cursor);
        const auto line_end = newline == std::string_view::npos
            ? source.size() : newline;
        const auto raw_line = source.substr(cursor, line_end - cursor);
        const auto line = trim(raw_line);
        const auto lower = ascii_lower(std::string(line));
        const bool plugin_tag = lower.starts_with("@loadplugin") ||
                                lower.starts_with("[loadplugin");
        if (!plugin_tag) {
            if (newline == std::string_view::npos) break;
            cursor = newline + 1;
            ++line_number;
            continue;
        }
        const auto module = lower.find("module");
        if (module == std::string::npos) {
            if (newline == std::string_view::npos) break;
            cursor = newline + 1;
            ++line_number;
            continue;
        }
        auto value = lower.find('=', module + 6);
        if (value == std::string::npos) {
            if (newline == std::string_view::npos) break;
            cursor = newline + 1;
            ++line_number;
            continue;
        }
        ++value;
        while (value < line.size() &&
               std::isspace(static_cast<unsigned char>(line[value]))) ++value;
        std::size_t finish = value;
        if (value < line.size() &&
            (line[value] == '\'' || line[value] == '"')) {
            const char quote = line[value++];
            finish = line.find(quote, value);
        } else {
            finish = line.find_first_of(" \t\r]", value);
        }
        if (finish == std::string_view::npos) finish = line.size();
        auto raw_module = std::string(trim(line.substr(value, finish - value)));
        const bool dynamic = raw_module.empty() ||
            raw_module.find_first_of("%&") != std::string::npos;
        auto plugin = dynamic ? std::string("<dynamic>")
                              : normalize_plugin_module(std::move(raw_module));
        if (!dynamic) plugins.insert(plugin);
        const bool conditional = lower.find("cond", finish) != std::string::npos;
        requests.push_back(PluginRequest{std::move(plugin),
                                         std::string(source_name),
                                         line_number,
                                         conditional, true});
        if (newline == std::string_view::npos) break;
        cursor = newline + 1;
        ++line_number;
    }
}

bool known_binary_magic(std::string_view extension,
                        std::span<const std::uint8_t> bytes) {
    if (extension == ".png")
        return bytes.size() >= 8 &&
            std::equal(bytes.begin(), bytes.begin() + 8,
                       std::array<std::uint8_t, 8>{0x89, 'P', 'N', 'G',
                                                   0x0d, 0x0a, 0x1a, 0x0a}.begin());
    if (extension == ".jpg" || extension == ".jpeg")
        return bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xd8;
    if (extension == ".bmp")
        return bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M';
    if (extension == ".ogg")
        return bytes.size() >= 4 && std::memcmp(bytes.data(), "OggS", 4) == 0;
    if (extension == ".wav")
        return bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
               std::memcmp(bytes.data() + 8, "WAVE", 4) == 0;
    return true;
}

bool looks_like_text_payload(std::span<const std::uint8_t> bytes,
                             std::string_view name) {
    if (bytes.empty() ||
        std::find(bytes.begin(), bytes.end(), std::uint8_t{0}) != bytes.end()) {
        return false;
    }
    try {
        const auto text = decode_retail_text(bytes, name);
        if (text.find('\n') == std::string::npos) return false;
        const auto first = std::find_if_not(text.begin(), text.end(), [](char ch) {
            return std::isspace(static_cast<unsigned char>(ch));
        });
        if (first == text.end()) return false;
        return *first == ';' || *first == '#' || *first == '@' ||
               *first == '[' || *first == '(' || *first == '/';
    } catch (const std::exception&) {
        return false;
    }
}

bool unsupported_runtime_extension(std::string_view extension) {
    // MPEG-PS/MPEG-1+MP2 and ASF/WMV3+WMA2 are decoded by the pinned Vita
    // FFmpeg backend and separately byte-golden tested against the actual
    // retail payloads. Keep unproven containers and Flash fail-closed.
    static constexpr std::array<std::string_view, 6> unsupported = {
        ".avi", ".flv", ".mkv", ".mov", ".mp4", ".swf"};
    return std::find(unsupported.begin(), unsupported.end(), extension) !=
           unsupported.end();
}

struct CompatibilityTotals {
    std::size_t archives = 0;
    std::size_t tjs_scripts = 0;
    std::size_t kag_scenarios = 0;
    std::size_t custom_scenarios = 0;
    std::size_t inline_scripts = 0;
    std::size_t expression_storages = 0;
    std::size_t boot_scenarios = 0;
    std::size_t boot_sources = 0;
    std::size_t dormant_compile_findings = 0;
    std::size_t binary_probes = 0;
    std::size_t psb_documents = 0;
    std::size_t zero_length_assets = 0;
    std::size_t text_named_assets = 0;
    std::set<std::string> plugins;
    std::vector<NativePluginBinary> native_plugins;
    std::set<std::string> unimplemented_bundled_plugins;
    std::size_t plugin_requests = 0;
    std::size_t boot_plugin_requests = 0;
    std::size_t optional_unsupported_plugins = 0;
    std::size_t dormant_unsupported_plugins = 0;
    std::size_t unresolved_scenario_storages = 0;
    std::size_t optional_unresolved_tjs = 0;
    std::map<std::string, std::size_t> unsupported_assets;
    std::map<std::string, std::size_t> archive_extensions;
    std::string first_unsupported_asset;
};

struct SourceUnit {
    std::string name;
    std::string normalized_name;
    std::string extension;
    std::string source;
    std::vector<std::string> inline_failures;
    std::vector<std::string> inline_sources;
    std::vector<PluginRequest> plugin_requests;
};

struct BootReachability {
    std::set<std::size_t> sources;
    std::vector<std::string> unresolved;
    std::vector<std::string> unresolved_tjs;
};

bool source_uses_plugin_surface(std::string_view module,
                                std::string_view source) {
    const auto* contract = yuri_plugin_surface_contract(module);
    if (!contract) return false;
    auto masked_source = mask_tjs_comments(source);
    char quote = 0;
    bool escape = false;
    for (char& ch : masked_source) {
        if (quote) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == quote) {
                quote = 0;
            }
            if (ch != '\n' && ch != '\r') ch = ' ';
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            ch = ' ';
        }
    }
    const auto masked = ascii_lower(std::move(masked_source));
    for (const auto marker : contract->markers) {
        if (!marker.empty() && masked.find(marker) != std::string::npos)
            return true;
    }
    return false;
}

bool reachable_uses_plugin_surface(std::string_view module,
                                   const std::vector<SourceUnit>& sources,
                                   const BootReachability& reachable) {
    for (const auto index : reachable.sources) {
        const auto& unit = sources[index];
        if (source_uses_plugin_surface(module, unit.source)) return true;
        for (const auto& inline_source : unit.inline_sources) {
            if (source_uses_plugin_surface(module, inline_source)) return true;
        }
    }
    return false;
}

BootReachability boot_reachable_sources(
    const std::vector<SourceUnit>& sources) {
    std::map<std::string, std::size_t> by_name;
    std::map<std::string, std::vector<std::size_t>> by_basename;
    std::deque<std::size_t> pending;
    BootReachability result;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        by_name[sources[index].normalized_name] = index;
        const auto slash = sources[index].normalized_name.find_last_of('/');
        const auto basename = slash == std::string::npos
            ? sources[index].normalized_name
            : sources[index].normalized_name.substr(slash + 1);
        by_basename[basename].push_back(index);
        if (basename == "startup.tjs" || basename == "first.ks" ||
            basename == "start.scn")
            pending.push_back(index);
    }

    const auto resolve = [&](std::string dependency)
        -> std::optional<std::size_t> {
        dependency = normalize_storage_name(dependency);
        if (const auto exact = by_name.find(dependency); exact != by_name.end())
            return exact->second;
        const auto slash = dependency.find_last_of('/');
        const auto basename = slash == std::string::npos
            ? dependency : dependency.substr(slash + 1);
        if (const auto match = by_basename.find(basename);
            match != by_basename.end() && !match->second.empty()) {
            // Yuri's auto-path table resolves a duplicate basename to the last
            // mounted active entry. Archive iteration follows mount priority.
            return match->second.back();
        }
        return std::nullopt;
    };

    while (!pending.empty()) {
        const auto index = pending.front();
        pending.pop_front();
        if (!result.sources.insert(index).second) continue;
        const auto dependencies = sources[index].extension == ".tjs"
            ? collect_tjs_dependencies(sources[index].source)
            : collect_scenario_dependencies(sources[index].source);
        for (const auto& dependency : dependencies) {
            if (const auto resolved = resolve(dependency)) {
                pending.push_back(*resolved);
            } else {
                auto& unresolved = sources[index].extension == ".tjs"
                    ? result.unresolved_tjs : result.unresolved;
                unresolved.push_back(sources[index].name + " -> " + dependency);
            }
        }
    }
    std::sort(result.unresolved.begin(), result.unresolved.end());
    result.unresolved.erase(
        std::unique(result.unresolved.begin(), result.unresolved.end()),
        result.unresolved.end());
    std::sort(result.unresolved_tjs.begin(), result.unresolved_tjs.end());
    result.unresolved_tjs.erase(
        std::unique(result.unresolved_tjs.begin(), result.unresolved_tjs.end()),
        result.unresolved_tjs.end());
    return result;
}

CompatibilityTotals audit_game(const std::filesystem::path& path,
                               std::string_view expected_fingerprint,
                               std::string_view expected_rule,
                               std::size_t expected_recognized,
                               std::size_t expected_samples,
                               const std::filesystem::path& arm_bundle = {},
                               std::string_view arm_probe_id = {},
                               std::string_view arm_kag_probe = {}) {
    const auto game = GameScanner::scan(path);
    require(game.fingerprint == expected_fingerprint,
            "retail fingerprint changed for " + path.string() + ": " +
            game.fingerprint);
    std::string error;
    const auto phase1 = infer_phase1_filter(path.string(), &error);
    require(phase1.has_value(), path.string() + ": " + error);
    require(phase1->rule_name == expected_rule,
            path.string() + ": Phase 1 rule changed from " +
            std::string(expected_rule) + " to " + phase1->rule_name);
    require(phase1->recognized == expected_recognized &&
            phase1->samples == expected_samples,
            path.string() + ": Phase 1 evidence changed from " +
            std::to_string(expected_recognized) + "/" +
            std::to_string(expected_samples) + " to " +
            std::to_string(phase1->recognized) + "/" +
            std::to_string(phase1->samples));

    Xp3FilterVm filter;
    require(filter.load(phase1->script, &error),
            path.string() + ": generated filter does not execute in Yuri TJS: " +
            error);
    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        compiler(new TJS::tTJS(), release_tjs);

    CompatibilityTotals totals;
    totals.native_plugins = inventory_native_plugins(path);
    std::map<std::string, std::vector<const NativePluginBinary*>> bundled_plugins;
    for (const auto& plugin : totals.native_plugins) {
        bundled_plugins[plugin.module].push_back(&plugin);
        if (!yuri_plugin_link_is_supported(plugin.module))
            totals.unimplemented_bundled_plugins.insert(plugin.module);
    }
    bool has_startup = false;
    bool has_boot_scenario = false;
    std::vector<SourceUnit> sources;
    for (const auto& archive_file : game.archives) {
        auto archive = Xp3Archive::open(archive_file.path, &error);
        require(archive.has_value(), "cannot open " + archive_file.path.string() +
                                    ": " + error);
        ++totals.archives;
        for (const auto& entry : archive->entries()) {
            const auto extension = extension_of(entry.name);
            ++totals.archive_extensions[extension.empty() ? "<none>" : extension];
            if (unsupported_runtime_extension(extension)) {
                ++totals.unsupported_assets[extension];
                if (totals.first_unsupported_asset.empty())
                    totals.first_unsupported_asset = archive_file.name + ">" +
                                                     entry.name;
            }
            if (extension == ".tjs" || extension == ".ks" ||
                extension == ".scn" || extension == ".script") {
                const auto bytes = archive->read(entry, 64u * 1024u * 1024u,
                                                 &filter, &error);
                require(bytes.has_value(), "cannot read " + entry.name +
                                           " from " + archive_file.name +
                                           ": " + error);
                const auto source = decode_retail_text(*bytes, entry.name);
                std::vector<PluginRequest> plugin_requests;
                collect_plugin_requests(source, entry.name, totals.plugins,
                                        plugin_requests);
                auto normalized = ascii_lower(entry.name);
                std::replace(normalized.begin(), normalized.end(), '\\', '/');
                if (normalized == "startup.tjs" ||
                    normalized.ends_with("/startup.tjs")) has_startup = true;
                const auto slash = normalized.find_last_of('/');
                const auto basename = slash == std::string::npos
                    ? normalized : normalized.substr(slash + 1);
                if (basename == "first.ks" || basename == "start.scn")
                    has_boot_scenario = true;
                sources.push_back(SourceUnit{entry.name, normalized, extension,
                                             source, {}, {},
                                             std::move(plugin_requests)});
                continue;
            }
            if (extension == ".psb") {
                const auto bytes = archive->read(entry, 256u * 1024u * 1024u,
                                                 &filter, &error);
                require(bytes.has_value(), "cannot read PSB " + entry.name +
                                           " from " + archive_file.name +
                                           ": " + error);
                PsbDocument document;
                require(parse_psb(bytes->data(), bytes->size(), document, &error),
                        "cannot parse PSB " + archive_file.name + ">" +
                        entry.name + ": " + error);
                require(document.root != nullptr,
                        "PSB has no root value: " + archive_file.name + ">" +
                        entry.name);
                ++totals.psb_documents;
                continue;
            }
            static const std::set<std::string> probed_extensions = {
                ".png", ".jpg", ".jpeg", ".bmp", ".ogg", ".wav"};
            if (!probed_extensions.contains(extension)) continue;
            const auto bytes = archive->read_prefix(entry, 64, &filter, &error);
            require(bytes.has_value(), "cannot read binary probe " + entry.name +
                                       ": " + error);
            if (bytes->empty()) {
                ++totals.zero_length_assets;
                ++totals.binary_probes;
                continue;
            }
            if (!known_binary_magic(extension, *bytes)) {
                const auto full = archive->read(entry, 64u * 1024u * 1024u,
                                                &filter, &error);
                require(full.has_value(), "cannot read text-named asset " +
                                          entry.name + ": " + error);
                require(looks_like_text_payload(*full, entry.name),
                        "decrypted binary signature is invalid: " + entry.name);
                ++totals.text_named_assets;
            }
            ++totals.binary_probes;
        }
    }
    // XP3 coverage alone is insufficient: older titles commonly place opening
    // and ending movies next to the executable.  Inspect loose files as well;
    // this is metadata-only and never copies the retail corpus.
    std::error_code walk_error;
    std::filesystem::recursive_directory_iterator iterator(
        path, std::filesystem::directory_options::skip_permission_denied,
        walk_error), end;
    require(!walk_error, path.string() + ": cannot enumerate loose files: " +
                         walk_error.message());
    while (iterator != end) {
        if (!iterator->is_regular_file(walk_error)) {
            require(!walk_error, path.string() +
                                 ": cannot inspect loose file type: " +
                                 walk_error.message());
            iterator.increment(walk_error);
            require(!walk_error, path.string() +
                                 ": cannot enumerate loose files: " +
                                 walk_error.message());
            continue;
        }
        const auto extension = ascii_lower(iterator->path().extension().string());
        if (unsupported_runtime_extension(extension)) {
            ++totals.unsupported_assets[extension];
            if (totals.first_unsupported_asset.empty())
                totals.first_unsupported_asset = iterator->path().string();
        }
        iterator.increment(walk_error);
        require(!walk_error, path.string() + ": cannot enumerate loose files: " +
                             walk_error.message());
    }
    require(has_startup, path.string() + ": startup.tjs is not mounted in XP3");
    require(has_boot_scenario,
            path.string() + ": no recognized boot scenario was found");

    const auto reachable = boot_reachable_sources(sources);
    require(!reachable.sources.empty(),
            path.string() + ": no boot source was reachable");
    // KAG scenario files commonly contain dormant labelled handlers for
    // optional routes (after-story, DLC, debug menus) after an unconditional
    // stop.  A file-level graph deliberately over-approximates those labels,
    // so a missing literal storage is a resource finding rather than proof of
    // a boot failure.  Unsupported plugins and formats remain hard failures.
    totals.unresolved_scenario_storages = reachable.unresolved.size();
    totals.optional_unresolved_tjs = reachable.unresolved_tjs.size();
    for (std::size_t index = 0; index < sources.size(); ++index) {
        auto& unit = sources[index];
        if (unit.extension == ".tjs") {
            auto failure = compile_tjs_error(*compiler, unit.source, unit.name);
            if (!failure.empty()) {
                const auto expression_failure = compile_tjs_error(
                    *compiler, unit.source, unit.name, true);
                if (expression_failure.empty()) {
                    failure.clear();
                    ++totals.expression_storages;
                }
            }
            const auto basename_pos = unit.normalized_name.find_last_of('/');
            const auto basename = basename_pos == std::string::npos
                ? unit.normalized_name
                : unit.normalized_name.substr(basename_pos + 1);
            if (!failure.empty() && basename == "startup.tjs") {
                throw std::runtime_error("boot TJS cannot compile: " + unit.name +
                                         ": " + failure);
            }
            if (!failure.empty())
                unit.inline_failures.push_back(unit.name + ": " + failure);
            ++totals.tjs_scripts;
            continue;
        }

        if (unit.extension == ".ks") {
            const auto basename_pos = unit.normalized_name.find_last_of('/');
            const auto basename = basename_pos == std::string::npos
                ? unit.normalized_name
                : unit.normalized_name.substr(basename_pos + 1);
            const auto requested = normalize_storage_name(arm_kag_probe);
            const bool capture = !requested.empty() &&
                (unit.normalized_name == requested || basename == requested);
            auto result = compile_kag_iscripts(*compiler, unit.source, unit.name,
                                               capture);
            totals.inline_scripts += result.blocks;
            unit.inline_failures = std::move(result.failures);
            unit.inline_sources = std::move(result.sources);
            ++totals.kag_scenarios;
        } else {
            ++totals.custom_scenarios;
        }
    }
    for (const auto index : reachable.sources) {
        ++totals.boot_sources;
        if (sources[index].extension != ".tjs") ++totals.boot_scenarios;
        for (const auto& request : sources[index].plugin_requests) {
            ++totals.boot_plugin_requests;
        }
        if (!sources[index].inline_failures.empty()) {
            throw std::runtime_error("boot-reachable TJS cannot compile: " +
                                     sources[index].inline_failures.front());
        }
    }
    for (std::size_t index = 0; index < sources.size(); ++index) {
        if (!reachable.sources.contains(index))
            totals.dormant_compile_findings += sources[index].inline_failures.size();
        for (const auto& request : sources[index].plugin_requests) {
            ++totals.plugin_requests;
            const bool requires_surface = reachable_uses_plugin_surface(
                request.module, sources, reachable);
            if (requires_surface &&
                (!yuri_plugin_link_is_supported(request.module) ||
                 (yuri_plugin_is_link_only(request.module) &&
                  !yuri_plugin_has_script_surface(request.module))) &&
                reachable.sources.contains(index)) {
                throw std::runtime_error(
                    "boot-reachable plugin " + request.module +
                    " requires an unavailable script API surface at " +
                    request.source + ":" + std::to_string(request.line));
            }
            if (yuri_plugin_link_is_supported(request.module)) continue;
            if (request.optional) {
                ++totals.optional_unsupported_plugins;
                continue;
            }
            if (reachable.sources.contains(index)) {
                std::string binary_context = "; no matching loose plugin binary";
                if (const auto bundled = bundled_plugins.find(request.module);
                    bundled != bundled_plugins.end() && !bundled->second.empty()) {
                    const auto& binary = *bundled->second.front();
                    binary_context = "; bundled Windows plugin " +
                        binary.relative_path.generic_string() + " [" +
                        (binary.valid_pe ? binary.machine : "not-PE") +
                        ", sha256=" + binary.sha256 + "]";
                }
                throw std::runtime_error(
                    "boot-reachable unsupported plugin " + request.module +
                    " requested at " + request.source + ":" +
                    std::to_string(request.line) + binary_context);
            }
            ++totals.dormant_unsupported_plugins;
        }
    }
    require(totals.tjs_scripts != 0, path.string() + ": no TJS scripts compiled");
    require(totals.kag_scenarios + totals.custom_scenarios != 0,
            path.string() + ": no recognized scenarios decoded");
    require(totals.unsupported_assets.empty(),
            path.string() + ": Vita-unsupported runtime asset " +
            totals.first_unsupported_asset);

    if (!arm_bundle.empty()) {
        require(!arm_probe_id.empty() &&
                std::all_of(arm_probe_id.begin(), arm_probe_id.end(), [](char ch) {
                    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
                }), "unsafe ARM probe id");
        const SourceUnit* startup = nullptr;
        for (const auto& source : sources) {
            const auto slash = source.normalized_name.find_last_of('/');
            const auto basename = slash == std::string::npos
                ? source.normalized_name
                : source.normalized_name.substr(slash + 1);
            if (basename == "startup.tjs") startup = &source;
        }
        require(startup != nullptr, path.string() + ": startup probe is unavailable");
        std::filesystem::create_directories(arm_bundle);
        const auto filename = std::string(arm_probe_id) + "-startup.tjs";
        std::ofstream output(arm_bundle / filename, std::ios::binary);
        require(static_cast<bool>(output), "cannot create ARM startup probe");
        output.write(startup->source.data(),
                     static_cast<std::streamsize>(startup->source.size()));
        require(static_cast<bool>(output), "cannot write ARM startup probe");
        output.close();
        const auto manifest_path = arm_bundle / "manifest.txt";
        const bool new_manifest = !std::filesystem::exists(manifest_path);
        std::ofstream manifest(manifest_path, std::ios::app);
        require(static_cast<bool>(manifest), "cannot append ARM probe manifest");
        if (new_manifest)
            manifest << "# operation|relative_path|bytes|max_compile_us\n";
        manifest << "compile|" << filename << '|' << startup->source.size()
                 << "|5000000\n";

        if (!arm_kag_probe.empty()) {
            const SourceUnit* scenario = nullptr;
            const auto requested = normalize_storage_name(arm_kag_probe);
            for (std::size_t index = 0; index < sources.size(); ++index) {
                const auto& source = sources[index];
                const auto slash = source.normalized_name.find_last_of('/');
                const auto basename = slash == std::string::npos
                    ? source.normalized_name
                    : source.normalized_name.substr(slash + 1);
                if ((source.normalized_name == requested || basename == requested) &&
                    reachable.sources.contains(index)) {
                    scenario = &source;
                    break;
                }
            }
            require(scenario != nullptr,
                    "requested ARM KAG probe is not boot-reachable");
            require(!scenario->inline_sources.empty(),
                    "requested ARM KAG probe has no inline script");
            const auto largest = std::max_element(
                scenario->inline_sources.begin(), scenario->inline_sources.end(),
                [](const auto& left, const auto& right) {
                    return left.size() < right.size();
                });
            const auto inline_filename =
                std::string(arm_probe_id) + "-inline.tjs";
            std::ofstream inline_output(arm_bundle / inline_filename,
                                        std::ios::binary);
            require(static_cast<bool>(inline_output),
                    "cannot create ARM KAG inline probe");
            inline_output.write(largest->data(),
                                static_cast<std::streamsize>(largest->size()));
            require(static_cast<bool>(inline_output),
                    "cannot write ARM KAG inline probe");
            inline_output.close();
            manifest << "compile|" << inline_filename << '|' << largest->size()
                     << "|5000000\n";
        }
        require(static_cast<bool>(manifest), "cannot write ARM probe manifest");
    }
    return totals;
}

void run_internal_contract_tests() {
    std::set<std::string> modules;
    std::vector<PluginRequest> requests;
    const std::string plugin_source =
        "// Plugins.link(\"commented.dll\");\n"
        "try {\n  Plugins.link(\"optional.dll\");\n} catch(e) {}\n"
        "Plugins.link(\"hard.dll\");\n"
        "; @loadplugin module=\"commented-tag.dll\"\n"
        "@loadplugin module=\"tag.dll\"\n"
        "[loadplugin module=unquoted.tpm]\n"
        "@loadplugin module=conditional.dll cond=false\n";
    collect_plugin_requests(plugin_source, "contract.ks", modules, requests);
    require(requests.size() == 5, "plugin parser admitted a commented request");
    require(requests[0].module == "optional.dll" && requests[0].optional,
            "multiline caught plugin request was not optional");
    require(requests[1].module == "hard.dll" && !requests[1].optional,
            "hard Plugins.link request was misclassified");
    require(requests[2].module == "tag.dll" && requests[2].scenario_tag,
            "KAG loadplugin request was not recognized");
    require(requests[3].module == "unquoted.tpm" &&
            requests[3].scenario_tag,
            "unquoted KAG loadplugin module was not recognized");
    require(requests[4].module == "conditional.dll" && requests[4].optional,
            "conditional KAG loadplugin request was not classified as optional");

    const std::string optional_surface_source =
        "try { Plugins.link(\"motionplayer.dll\"); } catch(e) {}\n"
        "var p = new Motion.Player(null); p.play(\"normal\");\n";
    require(source_uses_plugin_surface("motionplayer.dll",
                                      optional_surface_source),
            "optional motion plug-in API use was not detected");
    require(!source_uses_plugin_surface(
                "motionplayer.dll",
                "try { Plugins.link(\"motionplayer.dll\"); } catch(e) {}\n"),
            "a caught motion plug-in load was mistaken for API use");
    require(!source_uses_plugin_surface(
                "krflash.dll",
                "try { Plugins.link(\"krflash.dll\"); } catch(e) {}\n"),
            "a caught Flash plug-in load was mistaken for API use");
    require(source_uses_plugin_surface(
                "layerexdraw.dll",
                "if (typeof global.Layer.drawImage == \"undefined\") {}\n"),
            "layerExDraw API use was not detected");
    require(source_uses_plugin_surface(
                "scriptsex.dll",
                "if (Scripts.getObjectCount(value) > 0) {}\n"),
            "scriptsEx API use was not detected");
    SourceUnit caught_plugin;
    caught_plugin.source = "try { Plugins.link(\"scriptsEx.dll\"); } catch {}\n";
    SourceUnit later_api;
    later_api.source = "if (Scripts.getObjectCount(value) > 0) {}\n";
    BootReachability synthetic_reachability;
    synthetic_reachability.sources = {0, 1};
    require(reachable_uses_plugin_surface(
                "scriptsex.dll", {caught_plugin, later_api},
                synthetic_reachability),
            "cross-file optional plugin API use was not correlated");

    const auto release_surface_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_surface_tjs)>
        surface_engine(new TJS::tTJS(), release_surface_tjs);
    const auto exec_surface = [&](std::string_view source,
                                  std::string_view label) {
        const auto compile_failure = compile_tjs_error(
            *surface_engine, source, label);
        require(compile_failure.empty(), std::string(label) +
                " does not compile: " + compile_failure);
        try {
            surface_engine->ExecScript(TJS::ttstr(std::string(source)));
        } catch (const TJS::eTJS& error) {
            throw std::runtime_error(std::string(label) + ": " +
                                     error.GetMessage().AsStdString());
        }
    };
    exec_surface(krkrvita::motionplayer_surface_script, "Motion surface");
    exec_surface(
        "var __motion_rm = new global.Motion.ResourceManager(null, 1);"
        "var __motion_p = new global.Motion.Player(__motion_rm);"
        "__motion_p.setCoord(1, 2); __motion_p.setFlip(false, false);"
        "__motion_p.play(\"normal\", global.Motion.PlayFlagForce);"
        "__motion_p.progress(16); __motion_p.draw(null);"
        "var __motion_adaptor = new global.Motion.SeparateLayerAdaptor(null);"
        "__motion_adaptor.clear();", "Motion surface execution");
    exec_surface(krkrvita::layerexdraw_surface_script, "layerExDraw surface");
    exec_surface(
        "var __gdi = new global.GdiPlus.Image();"
        "__gdi.setSize(4, 5);"
        "var __bounds = __gdi.GetBounds();"
        "var __draw_rect = global.Layer.drawImageStretch(1, 2, 3, 4, __gdi, 0, 0, 4, 5);",
        "layerExDraw surface execution");
    exec_surface(krkrvita::scriptsex_surface_script, "scriptsEx surface");
    exec_surface(
        "var __scripts_count = global.Scripts.getObjectCount(%[a:1,b:2]);",
        "scriptsEx surface execution");
    TJS::tTJSVariant surface_result;
    surface_engine->EvalExpression(TJS_W("typeof global.Motion.Player"),
                                   &surface_result);
    require(TJS::ttstr(surface_result).AsStdString() != "undefined",
            "Motion compatibility surface did not register a Player class");

    const auto dependencies = collect_tjs_dependencies(
        "// Scripts.execStorage(\"ignored.tjs\");\n"
        "Scripts.evalStorage(\"table.tjs\");\n");
    require(dependencies.size() == 1 && dependencies[0] == "table.tjs",
            "commented TJS dependency was treated as executable");

    require(yuri_plugin_link_is_supported("fstat.dll") &&
            yuri_plugin_link_is_supported("sqlite3.dll") &&
            yuri_plugin_link_is_supported("krmovie.dll") &&
            yuri_plugin_link_is_supported("extnagano.dll") &&
            yuri_plugin_link_is_supported("krflash.dll") &&
            yuri_plugin_link_is_supported("gfxeffect.dll") &&
            !yuri_plugin_link_is_supported("arbitrary.dll"),
            "host/Vita plugin capability inventory is inconsistent");
    require(yuri_plugin_is_link_only("extnagano.dll") &&
            yuri_plugin_is_link_only("krflash.dll") &&
            yuri_plugin_is_link_only("gfxeffect.dll") &&
            !yuri_plugin_is_link_only("krmovie.dll"),
            "partial plugin capability status is inconsistent");
    require(yuri_plugin_has_script_surface("motionplayer.dll") &&
            yuri_plugin_has_script_surface("gfxeffect.dll") &&
            yuri_plugin_has_script_surface("layerexdraw.dll") &&
            yuri_plugin_has_script_surface("scriptsex.dll") &&
            !yuri_plugin_has_script_surface("krflash.dll"),
            "plugin script-surface capability status is inconsistent");
    std::set<std::string_view> unique_modules;
    for (const auto module : yuri_integrated_plugin_modules)
        require(unique_modules.insert(module).second,
                "duplicate integrated plugin capability");
    for (const auto module : yuri_internal_plugin_modules)
        require(unique_modules.insert(module).second,
                "duplicate internal plugin capability");

    const std::array<std::uint8_t, 8> png = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    require(known_binary_magic(".png", png), "PNG signature contract failed");
    require(!known_binary_magic(".png", std::span<const std::uint8_t>{}),
            "empty PNG was mistaken for decoded image data");
    const std::string disguised = "; scenario text\r\n@jump storage=first.ks\r\n";
    require(looks_like_text_payload(
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t*>(disguised.data()),
                    disguised.size()),
                "disguised.png"),
            "text payload classification contract failed");
    require(!unsupported_runtime_extension(".psb") &&
            !unsupported_runtime_extension(".mpg") &&
            !unsupported_runtime_extension(".wmv") &&
            unsupported_runtime_extension(".mp4") &&
            !unsupported_runtime_extension(".png"),
            "unsupported runtime-format inventory is inconsistent");

    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        compiler(new TJS::tTJS(), release_tjs);
    require(compile_tjs_error(*compiler, "(const)%[\"value\" => 1]",
                              "expression-contract.tjs", true).empty(),
            "TJS expression-storage compiler contract failed");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 &&
            std::string_view(argv[1]) == "--plugin-inventory") {
            const auto plugins = inventory_native_plugins(argv[2]);
            for (const auto& plugin : plugins) {
                std::cout << plugin.module << '|'
                          << plugin.relative_path.generic_string() << '|'
                          << plugin.sha256 << '|'
                          << (plugin.valid_pe ? plugin.machine : "not-PE")
                          << '|';
                for (std::size_t index = 0; index < plugin.imports.size(); ++index) {
                    if (index) std::cout << ',';
                    std::cout << plugin.imports[index];
                }
                if (!plugin.error.empty())
                    std::cout << "|error=" << plugin.error;
                std::cout << '\n';
            }
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
            run_internal_contract_tests();
            std::cout << "retail compatibility contracts passed\n";
            return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "--expect-phase2") {
            const auto game = GameScanner::scan(argv[2]);
            require(game.fingerprint == argv[3],
                    "retail fingerprint changed for " + std::string(argv[2]) +
                    ": " + game.fingerprint);
            std::string error;
            const auto phase1 = infer_phase1_filter(argv[2], &error);
            if (phase1) {
                throw std::runtime_error(std::string(argv[2]) +
                    ": expected Phase 2 but Phase 1 now selects " +
                    phase1->rule_name);
            }
            require(error.starts_with("phase 2 executable analysis required:"),
                    std::string(argv[2]) +
                    ": unexpected Phase 1 failure: " + error);
            std::cout << "phase2-required: " << argv[2] << "\n"
                      << "reason: " << error << '\n';
            return 0;
        }
        if (argc == 8 &&
            std::string_view(argv[1]) == "--expect-runtime-blocker") {
            const auto recognized = static_cast<std::size_t>(std::stoull(argv[5]));
            const auto samples = static_cast<std::size_t>(std::stoull(argv[6]));
            try {
                (void)audit_game(argv[2], argv[3], argv[4], recognized, samples);
            } catch (const std::exception& exception) {
                const std::string diagnostic = exception.what();
                require(diagnostic.find(argv[7]) != std::string::npos,
                        std::string(argv[2]) +
                        ": runtime blocker changed; expected substring '" +
                        argv[7] + "', got: " + diagnostic);
                std::cout << "runtime-blocked: " << argv[2] << "\n"
                          << "reason: " << diagnostic << '\n';
                return 0;
            }
            throw std::runtime_error(std::string(argv[2]) +
                ": expected runtime blocker but the strict audit passed");
        }
        if (argc == 9 && std::string_view(argv[1]) == "--emit-arm-probe") {
            const auto recognized = static_cast<std::size_t>(std::stoull(argv[5]));
            const auto samples = static_cast<std::size_t>(std::stoull(argv[6]));
            const auto totals = audit_game(argv[2], argv[3], argv[4], recognized,
                                           samples, argv[7], argv[8]);
            std::cout << "arm-probe: " << argv[8] << " boot_scenarios="
                      << totals.boot_scenarios << '\n';
            return 0;
        }
        if (argc == 10 &&
            std::string_view(argv[1]) == "--emit-kag-arm-probe") {
            const auto recognized = static_cast<std::size_t>(std::stoull(argv[5]));
            const auto samples = static_cast<std::size_t>(std::stoull(argv[6]));
            const auto totals = audit_game(argv[2], argv[3], argv[4], recognized,
                                           samples, argv[7], argv[8], argv[9]);
            std::cout << "arm-kag-probe: " << argv[8] << " scenario="
                      << argv[9] << " boot_scenarios="
                      << totals.boot_scenarios << '\n';
            return 0;
        }
        if (argc != 7 || std::string_view(argv[1]) != "--game") {
            std::cerr << "usage: krkrvita-retail-compatibility --game "
                         "GAME FINGERPRINT PHASE1_RULE RECOGNIZED SAMPLES\n"
                         "   or: krkrvita-retail-compatibility --expect-phase2 "
                         "GAME FINGERPRINT\n"
                         "   or: krkrvita-retail-compatibility "
                         "--expect-runtime-blocker GAME FINGERPRINT PHASE1_RULE "
                         "RECOGNIZED SAMPLES DIAGNOSTIC_SUBSTRING\n"
                         "   or: krkrvita-retail-compatibility --emit-arm-probe "
                         "GAME FINGERPRINT PHASE1_RULE RECOGNIZED SAMPLES "
                         "OUTPUT_DIR ID\n"
                         "   or: krkrvita-retail-compatibility "
                         "--plugin-inventory GAME\n"
                         "   or: krkrvita-retail-compatibility "
                         "--emit-kag-arm-probe GAME FINGERPRINT PHASE1_RULE "
                         "RECOGNIZED SAMPLES OUTPUT_DIR ID SCENARIO\n";
            return 2;
        }
        const auto recognized = static_cast<std::size_t>(std::stoull(argv[5]));
        const auto samples = static_cast<std::size_t>(std::stoull(argv[6]));
        const auto totals = audit_game(argv[2], argv[3], argv[4], recognized,
                                       samples);
        std::cout << "compatible: " << argv[2] << "\n"
                  << "archives: " << totals.archives << "\n"
                  << "tjs_scripts: " << totals.tjs_scripts << "\n"
                  << "kag_scenarios: " << totals.kag_scenarios << "\n"
                  << "custom_scenarios: " << totals.custom_scenarios << "\n"
                  << "inline_scripts: " << totals.inline_scripts << "\n"
                  << "expression_storages: "
                  << totals.expression_storages << "\n"
                  << "boot_scenarios: " << totals.boot_scenarios << "\n"
                  << "boot_sources: " << totals.boot_sources << "\n"
                  << "dormant_compile_findings: "
                  << totals.dormant_compile_findings << "\n"
                  << "binary_probes: " << totals.binary_probes << "\n"
                  << "psb_documents: " << totals.psb_documents << "\n"
                  << "zero_length_assets: "
                  << totals.zero_length_assets << "\n"
                  << "text_named_assets: " << totals.text_named_assets << "\n"
                  << "boot_plugin_requests: "
                  << totals.boot_plugin_requests << "\n"
                  << "plugin_requests: " << totals.plugin_requests << "\n"
                  << "native_plugin_binaries: "
                  << totals.native_plugins.size() << "\n"
                  << "unimplemented_bundled_plugin_modules: "
                  << totals.unimplemented_bundled_plugins.size() << "\n"
                  << "optional_unsupported_plugins: "
                  << totals.optional_unsupported_plugins << "\n"
                  << "dormant_unsupported_plugins: "
                  << totals.dormant_unsupported_plugins << "\n"
                  << "unresolved_scenario_storages: "
                  << totals.unresolved_scenario_storages << "\n"
                  << "optional_unresolved_tjs: "
                  << totals.optional_unresolved_tjs << "\n"
                  << "unsupported_assets:";
        for (const auto& [extension, count] : totals.unsupported_assets)
            std::cout << ' ' << extension << '=' << count;
        std::cout << "\n"
                  << "plugins:";
        for (const auto& plugin : totals.plugins) std::cout << ' ' << plugin;
        std::cout << "\nnative_plugins:";
        for (const auto& plugin : totals.native_plugins) {
            std::cout << ' ' << plugin.module << '@'
                      << plugin.relative_path.generic_string() << '['
                      << (plugin.valid_pe ? plugin.machine : "not-PE") << ']';
        }
        std::cout << "\nunimplemented_bundled_plugins:";
        for (const auto& plugin : totals.unimplemented_bundled_plugins)
            std::cout << ' ' << plugin;
        std::cout << "\narchive_extensions:";
        for (const auto& [extension, count] : totals.archive_extensions)
            std::cout << ' ' << extension << '=' << count;
        std::cout << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "compatibility failure: " << exception.what() << '\n';
        return 1;
    }
}

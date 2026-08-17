#pragma once

// The original Kirikiri Squirrel 2.2.4 build targets Win32, where wchar_t
// and Kirikiri's tjs_char are both 16-bit.  Vita's C++ ABI uses a 32-bit
// wchar_t while Yuri deliberately keeps tjs_char as UTF-16 (char16_t).  Keep
// Squirrel's public character type UTF-16 and provide the small C-library
// surface the old VM expects instead of relying on an ABI-incompatible
// wchar_t implementation.

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <string_view>

#ifndef STGC_DEFAULT
#define STGC_DEFAULT 0
#endif

namespace krkrvita::squirrel {

using Char = char16_t;

inline std::size_t strlen16(const Char *text) {
    if (!text) return 0;
    const Char *cursor = text;
    while (*cursor) ++cursor;
    return static_cast<std::size_t>(cursor - text);
}

inline int strcmp16(const Char *left, const Char *right) {
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return static_cast<int>(*left) - static_cast<int>(*right);
}

inline int strncmp16(const Char *left, const Char *right, std::size_t count) {
    if (count == 0) return 0;
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (count-- > 0) {
        if (*left != *right || *left == 0)
            return static_cast<int>(*left) - static_cast<int>(*right);
        ++left;
        ++right;
    }
    return 0;
}

inline const Char *strstr16(const Char *text, const Char *needle) {
    if (!text || !needle) return nullptr;
    const std::size_t needle_length = strlen16(needle);
    if (needle_length == 0) return text;
    for (const Char *cursor = text; *cursor; ++cursor) {
        if (strncmp16(cursor, needle, needle_length) == 0) return cursor;
    }
    return nullptr;
}

inline const Char *strchr16(const Char *text, Char needle) {
    if (!text) return nullptr;
    for (; *text; ++text) if (*text == needle) return text;
    return needle == 0 ? text : nullptr;
}

inline bool is_delimiter16(Char value, const Char *delimiters) {
    return delimiters && strchr16(delimiters, value) != nullptr;
}

// The Squirrel standard library uses strtok's two-call convention.  Keep
// state per thread because Squirrel continuations may be driven by different
// engine workers, while preserving the destructive tokenization semantics.
inline Char *strtok16(Char *text, const Char *delimiters) {
    static thread_local Char *state = nullptr;
    Char *cursor = text ? text : state;
    if (!cursor || !delimiters) return nullptr;
    while (*cursor && is_delimiter16(*cursor, delimiters)) ++cursor;
    if (!*cursor) { state = nullptr; return nullptr; }
    Char *token = cursor;
    while (*cursor && !is_delimiter16(*cursor, delimiters)) ++cursor;
    if (*cursor) { *cursor = 0; state = cursor + 1; }
    else state = nullptr;
    return token;
}

inline bool isspace16(Char value) {
    return value == u' ' || value == u'\t' || value == u'\n' ||
           value == u'\r' || value == u'\f' || value == u'\v';
}

inline bool isdigit16(Char value) { return value >= u'0' && value <= u'9'; }
inline bool isxdigit16(Char value) {
    return isdigit16(value) || (value >= u'a' && value <= u'f') ||
           (value >= u'A' && value <= u'F');
}
inline bool isalpha16(Char value) {
    return (value >= u'a' && value <= u'z') ||
           (value >= u'A' && value <= u'Z');
}
inline bool isalnum16(Char value) { return isalpha16(value) || isdigit16(value); }
inline bool iscntrl16(Char value) { return value < 0x20 || value == 0x7f; }

inline std::string ascii_prefix(const Char *text) {
    std::string result;
    if (!text) return result;
    for (; *text; ++text) {
        const auto value = static_cast<unsigned>(static_cast<std::uint16_t>(*text));
        result.push_back(value <= 0x7f ? static_cast<char>(value) : '?');
    }
    return result;
}

inline std::string utf8_from_utf16(const Char *text) {
    std::string result;
    if (!text) return result;
    for (std::size_t i = 0; text[i]; ++i) {
        std::uint32_t code = static_cast<std::uint16_t>(text[i]);
        if (code >= 0xd800 && code <= 0xdbff && text[i + 1] >= 0xdc00 &&
            text[i + 1] <= 0xdfff) {
            code = 0x10000 + ((code - 0xd800) << 10) +
                   (static_cast<std::uint16_t>(text[++i]) - 0xdc00);
        }
        if (code <= 0x7f) result.push_back(static_cast<char>(code));
        else if (code <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (code >> 6)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (code >> 12)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xf0 | (code >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }
    return result;
}

inline std::u16string utf16_from_utf8(const char *text) {
    std::u16string result;
    if (!text) return result;
    const auto *bytes = reinterpret_cast<const unsigned char *>(text);
    while (*bytes) {
        std::uint32_t code = *bytes++;
        if (code >= 0xc0 && code < 0xe0) {
            code = ((code & 0x1f) << 6) | (*bytes++ & 0x3f);
        } else if (code >= 0xe0 && code < 0xf0) {
            code = ((code & 0x0f) << 12) | ((*bytes++ & 0x3f) << 6) |
                   (*bytes++ & 0x3f);
        } else if (code >= 0xf0) {
            code = ((code & 0x07) << 18) | ((*bytes++ & 0x3f) << 12) |
                   ((*bytes++ & 0x3f) << 6) | (*bytes++ & 0x3f);
        }
        if (code <= 0xffff) result.push_back(static_cast<Char>(code));
        else {
            code -= 0x10000;
            result.push_back(static_cast<Char>(0xd800 | (code >> 10)));
            result.push_back(static_cast<Char>(0xdc00 | (code & 0x3ff)));
        }
    }
    return result;
}

inline std::FILE *fopen16(const Char *path, const Char *mode) {
    const std::string path8 = utf8_from_utf16(path);
    const std::string mode8 = utf8_from_utf16(mode);
    return std::fopen(path8.c_str(), mode8.c_str());
}

inline const Char *getenv16(const Char *name) {
    static thread_local std::u16string value;
    const std::string name8 = utf8_from_utf16(name);
    const char *found = std::getenv(name8.c_str());
    if (!found) return nullptr;
    value = utf16_from_utf8(found);
    return value.c_str();
}

inline int system16(const Char *command) {
    const std::string command8 = utf8_from_utf16(command);
    return std::system(command8.c_str());
}
inline int remove16(const Char *path) {
    const std::string path8 = utf8_from_utf16(path);
    return std::remove(path8.c_str());
}
inline int rename16(const Char *old_path, const Char *new_path) {
    const std::string old8 = utf8_from_utf16(old_path);
    const std::string new8 = utf8_from_utf16(new_path);
    return std::rename(old8.c_str(), new8.c_str());
}
inline Char *asctime16(const std::tm *time) {
    static thread_local Char result[64];
    const char *text = std::asctime(time);
    const std::u16string converted = utf16_from_utf8(text ? text : "");
    const std::size_t result_capacity = sizeof(result) / sizeof(result[0]);
    const std::size_t count = std::min(converted.size(), result_capacity - 1);
    std::copy_n(converted.data(), count, result);
    result[count] = 0;
    return result;
}

inline long strtol16(const Char *text, Char **end, int base) {
    const std::string ascii = ascii_prefix(text);
    char *ascii_end = nullptr;
    const long result = std::strtol(ascii.c_str(), &ascii_end, base);
    if (end) *end = const_cast<Char *>(text) + (ascii_end - ascii.c_str());
    return result;
}

inline unsigned long strtoul16(const Char *text, Char **end, int base) {
    const std::string ascii = ascii_prefix(text);
    char *ascii_end = nullptr;
    const auto result = std::strtoul(ascii.c_str(), &ascii_end, base);
    if (end) *end = const_cast<Char *>(text) + (ascii_end - ascii.c_str());
    return result;
}

inline double strtod16(const Char *text, Char **end) {
    const std::string ascii = ascii_prefix(text);
    char *ascii_end = nullptr;
    const double result = std::strtod(ascii.c_str(), &ascii_end);
    if (end) *end = const_cast<Char *>(text) + (ascii_end - ascii.c_str());
    return result;
}

inline int atoi16(const Char *text) {
    return static_cast<int>(strtol16(text, nullptr, 10));
}

inline void append_ascii(std::u16string &output, std::string_view text) {
    output.reserve(output.size() + text.size());
    for (const unsigned char value : text)
        output.push_back(static_cast<Char>(value));
}

inline void append_padded(std::u16string &output, std::u16string_view value,
                          int width, bool left, Char pad) {
    const std::size_t length = value.size();
    if (width <= 0 || length >= static_cast<std::size_t>(width)) {
        output.append(value);
        return;
    }
    const std::size_t padding = static_cast<std::size_t>(width) - length;
    if (!left) output.append(padding, pad);
    output.append(value);
    if (left) output.append(padding, u' ');
}

// Squirrel's diagnostic/string conversion code only needs the conventional
// printf subset.  Keeping it here also avoids passing char16_t pointers to a
// libc wchar_t formatter, which is undefined on Vita.
inline int vsprintf16(Char *destination, std::size_t capacity,
                      const Char *format, va_list arguments) {
    std::u16string output;
    if (!format) output = u"(null)";
    for (const Char *cursor = format; cursor && *cursor; ) {
        if (*cursor != u'%') {
            output.push_back(*cursor++);
            continue;
        }
        ++cursor;
        if (*cursor == u'%') {
            output.push_back(u'%');
            ++cursor;
            continue;
        }
        bool left = false;
        bool zero = false;
        while (*cursor == u'-' || *cursor == u'0' || *cursor == u'+' ||
               *cursor == u' ') {
            left = left || *cursor == u'-';
            zero = zero || *cursor == u'0';
            ++cursor;
        }
        int width = 0;
        while (isdigit16(*cursor)) {
            width = width * 10 + static_cast<int>(*cursor - u'0');
            ++cursor;
        }
        int precision = -1;
        if (*cursor == u'.') {
            ++cursor;
            precision = 0;
            while (isdigit16(*cursor)) {
                precision = precision * 10 + static_cast<int>(*cursor - u'0');
                ++cursor;
            }
        }
        // The old VM does not use length modifiers, but accepting them keeps
        // diagnostics from consuming the wrong argument if one is present.
        if (*cursor == u'l' || *cursor == u'h' || *cursor == u'z') ++cursor;
        const Char conversion = *cursor ? *cursor++ : 0;
        std::u16string value;
        switch (conversion) {
        case u's': {
            const Char *text = va_arg(arguments, const Char *);
            if (text) value.assign(text);
            break;
        }
        case u'c':
            value.push_back(static_cast<Char>(va_arg(arguments, int)));
            break;
        case u'd':
        case u'i': {
            char buffer[64];
            const int number = va_arg(arguments, int);
            std::snprintf(buffer, sizeof(buffer), "%d", number);
            append_ascii(value, buffer);
            break;
        }
        case u'u': {
            char buffer[64];
            const unsigned number = va_arg(arguments, unsigned);
            std::snprintf(buffer, sizeof(buffer), "%u", number);
            append_ascii(value, buffer);
            break;
        }
        case u'f':
        case u'g': {
            char buffer[128];
            const double number = va_arg(arguments, double);
            const int digits = precision >= 0 ? precision : 6;
            std::snprintf(buffer, sizeof(buffer), conversion == u'f' ? "%.*f" : "%.*g",
                          digits, number);
            append_ascii(value, buffer);
            break;
        }
        case u'p': {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%p", va_arg(arguments, void *));
            append_ascii(value, buffer);
            break;
        }
        default:
            value.push_back(u'%');
            if (conversion) value.push_back(conversion);
            break;
        }
        append_padded(output, value, width, left, zero ? u'0' : u' ');
    }

    const std::size_t copied = capacity == 0 ? 0 :
        std::min(output.size(), capacity - 1);
    if (destination && capacity != 0) {
        if (copied) std::memcpy(destination, output.data(), copied * sizeof(Char));
        destination[copied] = 0;
    }
    return static_cast<int>(output.size());
}

inline int sprintf16(Char *destination, const Char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = vsprintf16(destination,
                                   std::numeric_limits<std::size_t>::max(),
                                   format, arguments);
    va_end(arguments);
    return result;
}

inline int vsprintf_unbounded16(Char *destination, const Char *format,
                                va_list arguments) {
    return vsprintf16(destination, std::numeric_limits<std::size_t>::max(),
                      format, arguments);
}

inline int vsnprintf16(Char *destination, std::size_t capacity,
                       const Char *format, va_list arguments) {
    return vsprintf16(destination, capacity, format, arguments);
}

inline int snprintf16(Char *destination, std::size_t capacity,
                      const Char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = vsnprintf16(destination, capacity, format, arguments);
    va_end(arguments);
    return result;
}

inline int printf16(const Char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    va_list measure_arguments;
    va_copy(measure_arguments, arguments);
    const int length = vsprintf16(nullptr, 0, format, measure_arguments);
    va_end(measure_arguments);

    if (length > 0) {
        std::u16string buffer(static_cast<std::size_t>(length) + 1, 0);
        va_list render_arguments;
        va_copy(render_arguments, arguments);
        vsprintf16(buffer.data(), buffer.size(), format, render_arguments);
        va_end(render_arguments);
        const std::string utf8 = utf8_from_utf16(buffer.c_str());
        std::fwrite(utf8.data(), 1, utf8.size(), stdout);
    }
    va_end(arguments);
    return length;
}

} // namespace krkrvita::squirrel

// sqstdstring.cpp keeps the original MSVC spelling instead of using
// squirrel.h's scatoi macro directly.
#ifndef _wtoi
#define _wtoi krkrvita::squirrel::atoi16
#endif

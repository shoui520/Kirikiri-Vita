#include "yuri_squirrel_compat.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using krkrvita::squirrel::Char;

namespace {

int format16(Char *destination, std::size_t capacity, const Char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = krkrvita::squirrel::vsnprintf16(
        destination, capacity, format, arguments);
    va_end(arguments);
    return result;
}

} // namespace

int main() {
    using namespace krkrvita::squirrel;

    const Char greeting[] = u"alpha-日本😀";
    assert(strlen16(greeting) == 10); // UTF-16 units, including the surrogate pair.
    assert(strcmp16(greeting, greeting) == 0);
    assert(strncmp16(greeting, u"alpha", 5) == 0);
    assert(strstr16(greeting, u"日本") == greeting + 6);
    assert(strchr16(greeting, u'日') == greeting + 6);
    assert(strchr16(greeting, 0) == greeting + strlen16(greeting));

    const std::string utf8 = utf8_from_utf16(greeting);
    assert(utf8 == u8"alpha-日本😀");
    const std::u16string round_trip = utf16_from_utf8(utf8.c_str());
    assert(round_trip == greeting);
    assert(ascii_prefix(u"123.5") == "123.5");

    Char *end = nullptr;
    assert(strtol16(u"-123tail", &end, 10) == -123);
    assert(end && *end == u't');
    assert(strtoul16(u"ff!", &end, 16) == 255);
    assert(end && *end == u'!');
    assert(strtod16(u"1.25x", &end) == 1.25);
    assert(end && *end == u'x');
    assert(atoi16(u"42") == 42);

    Char tokens[] = u" one,,two ";
    Char *token = strtok16(tokens, u" ,");
    assert(token && std::u16string(token) == u"one");
    token = strtok16(nullptr, u" ,");
    assert(token && std::u16string(token) == u"two");
    assert(strtok16(nullptr, u" ,") == nullptr);

    Char formatted[64]{};
    const int formatted_length = format16(
        formatted, sizeof(formatted) / sizeof(formatted[0]),
        u"%s %d %u %.2f %%", u"ok", -7, 9u, 1.5);
    assert(formatted_length == 14);
    assert(std::u16string(formatted) == u"ok -7 9 1.50 %");
    Char truncated[5]{};
    assert(format16(truncated, 5, u"abcdef") == 6);
    assert(std::u16string(truncated) == u"abcd");

    std::tm time{};
    time.tm_year = 125;
    time.tm_mon = 0;
    time.tm_mday = 2;
    Char *date = asctime16(&time);
    assert(date && strlen16(date) > 0 && strlen16(date) < 64);

    assert(isspace16(u'\n'));
    assert(isdigit16(u'7'));
    assert(isxdigit16(u'F'));
    assert(isalpha16(u'z'));
    assert(isalnum16(u'9'));
    assert(iscntrl16(u'\x1f'));

    std::puts("squirrel compat tests passed");
    return 0;
}

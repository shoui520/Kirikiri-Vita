#include "tjsCommHead.h"
#include "tjsMessage.h"

#include <chrono>
#include <cstdio>

tjs_uint32 TVPGetRoughTickCount32() {
    using namespace std::chrono;
    return static_cast<tjs_uint32>(duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count());
}

ttstr TVPGetMessageByLocale(const std::string& key) {
    return ttstr(key);
}

namespace TJS {

#ifdef TJS_NO_REGEXP
void TJSReleaseRegex() {}
#endif

void TVPConsoleLog(const tjs_char* line) {
    if (!line) return;
    const auto length = TJS_wcstombs(nullptr, line, 0);
    if (length == static_cast<std::size_t>(-1)) return;
    std::string text(length, '\0');
    TJS_wcstombs(text.data(), line, text.size());
    std::fprintf(stderr, "TJS: %s\n", text.c_str());
}

} // namespace TJS

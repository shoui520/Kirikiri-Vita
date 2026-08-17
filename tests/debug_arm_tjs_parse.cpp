#include "tjs.h"
#include "tjsError.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.size() < 2 || bytes[0] != 0xff || bytes[1] != 0xfe ||
        (bytes.size() & 1u) != 0) return 3;
    std::u16string source;
    source.reserve((bytes.size() - 2) / 2);
    for (std::size_t offset = 2; offset < bytes.size(); offset += 2) {
        source.push_back(static_cast<char16_t>(
            bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8)));
    }
    const auto release = [](TJS::tTJS *engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release)> engine(new TJS::tTJS(), release);
    try {
        TJS::tTJSVariant result;
        engine->EvalExpression(source.c_str(), &result, nullptr,
                               TJS_W("savedata.ksd"), 0);
        std::cout << "ok " << source.size() << "\n";
        return 0;
    } catch (const TJS::eTJS &exception) {
        std::cerr << TJS::ttstr(exception.GetMessage()).AsStdString() << "\n";
        return 1;
    }
}

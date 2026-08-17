#include "krkrvita/buffered_units.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        constexpr std::size_t capacity = 4096;
        krkrvita::BufferedUnits<char16_t, capacity> buffer;
        std::u16string expected;
        expected.reserve(39159);
        for (std::size_t index = 0; index < 39159; ++index)
            expected.push_back(static_cast<char16_t>(u'A' + index % 26));

        std::u16string output;
        std::size_t writes = 0;
        const auto sink = [&](const char16_t* data, std::size_t count) {
            output.append(data, count);
            ++writes;
        };
        for (const auto unit : expected) buffer.append(unit, sink);
        buffer.flush_to(sink);

        require(output == expected, "single-unit buffering changed output");
        require(writes == (expected.size() + capacity - 1) / capacity,
                "single-unit buffering performed an unexpected write count");
        require(buffer.buffered() == 0, "flush retained buffered units");

        krkrvita::BufferedUnits<char16_t, 7> chunked;
        output.clear();
        writes = 0;
        chunked.append(expected.data(), expected.size(), sink);
        chunked.flush_to(sink);
        require(output == expected, "chunk buffering changed output");
        require(writes == (expected.size() + 6) / 7,
                "chunk buffering performed an unexpected write count");

        krkrvita::BufferedUnits<char16_t, 8> mixed;
        output.clear();
        writes = 0;
        mixed.append(u'{', sink);
        mixed.append(u"\"count\"=>", 9, sink);
        mixed.append(u"39159", 5, sink);
        mixed.append(u',', sink);
        mixed.append(u"\"ratio\"=>1.5", 12, sink);
        mixed.append(u'}', sink);
        mixed.flush_to(sink);
        require(output == u"{\"count\"=>39159,\"ratio\"=>1.5}",
                "mixed scalar and chunk buffering changed field order");

        constexpr std::size_t persisted_units = 44849;
        std::vector<std::uint8_t> serialized(persisted_units * 2);
        for (std::size_t index = 0; index < serialized.size(); ++index)
            serialized[index] = static_cast<std::uint8_t>((index * 37) & 0xff);
        krkrvita::BufferedUnits<std::uint8_t, 8192> text_stream_buffer;
        std::vector<std::uint8_t> persisted;
        persisted.reserve(serialized.size());
        writes = 0;
        const auto byte_sink = [&](const std::uint8_t* data,
                                   std::size_t count) {
            persisted.insert(persisted.end(), data, data + count);
            ++writes;
        };
        const std::size_t fragments[] = {2, 1, 4, 9, 3, 17, 5, 31};
        std::size_t offset = 0;
        std::size_t fragment = 0;
        while (offset < serialized.size()) {
            const auto requested = fragments[fragment % 8];
            const auto count = std::min(requested, serialized.size() - offset);
            text_stream_buffer.append(serialized.data() + offset, count,
                                      byte_sink);
            offset += count;
            ++fragment;
        }
        text_stream_buffer.flush_to(byte_sink);
        require(persisted == serialized,
                "fragmented built-in text-stream buffering changed bytes");
        require(writes == (serialized.size() + 8191) / 8192,
                "fragmented built-in text stream exceeded bounded sink calls");

        bool rejected = false;
        try {
            chunked.append(nullptr, 1, sink);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "null nonempty input was accepted");
        std::cout << "buffered-unit tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

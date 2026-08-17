#include "krkrvita/kag_inline_script.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::atomic<std::size_t> allocations{0};
std::atomic<bool> count_allocations{false};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

void* operator new(std::size_t size) {
    if (count_allocations.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    try {
        std::vector<std::u16string> lines;
        lines.reserve(737);
        std::u16string expected;
        for (std::size_t index = 0; index < 737; ++index) {
            std::u16string line = u"retail configuration line ";
            line += static_cast<char16_t>(u'A' + index % 26);
            lines.push_back(line);
            expected += line;
            expected += u"\r\n";
        }

        allocations.store(0, std::memory_order_relaxed);
        count_allocations.store(true, std::memory_order_relaxed);
        const auto assembled =
            krkrvita::assemble_kag_inline_script<char16_t>(
                0, lines.size(), [&](std::size_t line) {
                    return lines[line].c_str();
                });
        count_allocations.store(false, std::memory_order_relaxed);

        require(assembled == expected, "assembled KAG script bytes changed");
        require(allocations.load(std::memory_order_relaxed) == 1,
                "large KAG script did not use exactly one backing allocation");

        const auto empty = krkrvita::assemble_kag_inline_script<char16_t>(
            4, 4, [](std::size_t) { return u"unused"; });
        require(empty.empty(), "empty KAG script is not empty");

        bool rejected = false;
        try {
            (void)krkrvita::assemble_kag_inline_script<char16_t>(
                2, 1, [](std::size_t) { return u"unused"; });
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "reversed KAG script range was accepted");
        std::cout << "KAG inline script assembly tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

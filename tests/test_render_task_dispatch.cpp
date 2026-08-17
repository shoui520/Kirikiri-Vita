#include "krkrvita/render_task_dispatch.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <new>
#include <utility>

namespace {

std::size_t allocation_count = 0;

void* allocate_counted(std::size_t size) {
    ++allocation_count;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}

struct PixelTask {
    // Exceed every common std::function small-object buffer so the parallel
    // mirror proves that type erasure really allocates in this test.
    std::array<unsigned char, 512> large_capture{};
    unsigned int* pixels = nullptr;
    int width = 0;
    int height = 0;
    int task_count = 0;
    int* calls = nullptr;

    void operator()(int task_index) const {
        ++*calls;
        const int y0 = height * task_index / task_count;
        const int y1 = height * (task_index + 1) / task_count;
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < width; ++x) {
                pixels[y * width + x] =
                    static_cast<unsigned int>((y + 1) * 257 + x * 17);
            }
        }
    }
};

struct ParallelMirror {
    int calls = 0;

    template <typename Task>
    void operator()(int task_count, Task&& task) {
        ++calls;
        std::function<void(int)> owned(std::forward<Task>(task));
        for (int i = 0; i < task_count; ++i) owned(i);
    }
};

bool equal_pixels(const unsigned int* left, const unsigned int* right,
                  std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (left[i] != right[i]) return false;
    }
    return true;
}

}  // namespace

void* operator new(std::size_t size) { return allocate_counted(size); }
void* operator new[](std::size_t size) { return allocate_counted(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

int main() {
    constexpr int width = 19;
    constexpr int height = 17;
    constexpr std::size_t pixel_count = width * height;
    std::array<unsigned int, pixel_count> serial_pixels{};
    std::array<unsigned int, pixel_count> parallel_pixels{};

    int serial_calls = 0;
    PixelTask serial_task{};
    serial_task.pixels = serial_pixels.data();
    serial_task.width = width;
    serial_task.height = height;
    serial_task.task_count = 1;
    serial_task.calls = &serial_calls;
    ParallelMirror serial_parallel{};

    const std::size_t serial_allocations_before = allocation_count;
    krkrvita::dispatch_render_tasks(1, serial_task, serial_parallel);
    const std::size_t serial_allocations_after = allocation_count;
    if (serial_allocations_after != serial_allocations_before ||
        serial_parallel.calls != 0 || serial_calls != 1) {
        return 1;
    }

    int zero_calls = 0;
    PixelTask zero_task = serial_task;
    zero_task.calls = &zero_calls;
    ParallelMirror zero_parallel{};
    const std::size_t zero_allocations_before = allocation_count;
    krkrvita::dispatch_render_tasks(0, zero_task, zero_parallel);
    if (allocation_count != zero_allocations_before || zero_calls != 0 ||
        zero_parallel.calls != 0) {
        return 2;
    }

    int parallel_task_calls = 0;
    PixelTask parallel_task{};
    parallel_task.pixels = parallel_pixels.data();
    parallel_task.width = width;
    parallel_task.height = height;
    parallel_task.task_count = 3;
    parallel_task.calls = &parallel_task_calls;
    ParallelMirror parallel_dispatch{};

    const std::size_t parallel_allocations_before = allocation_count;
    krkrvita::dispatch_render_tasks(3, parallel_task, parallel_dispatch);
    if (allocation_count <= parallel_allocations_before ||
        parallel_dispatch.calls != 1 || parallel_task_calls != 3) {
        return 3;
    }
    if (!equal_pixels(serial_pixels.data(), parallel_pixels.data(),
                      pixel_count)) {
        return 4;
    }
    return 0;
}

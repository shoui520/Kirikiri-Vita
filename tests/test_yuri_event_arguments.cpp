#include "krkrvita/event_arguments.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {

std::size_t array_allocations = 0;
std::size_t array_deallocations = 0;

struct Argument {
    int value = 0;
};

bool check(bool condition, const char* message)
{
    if(condition) return true;
    std::fprintf(stderr, "Yuri event-argument regression: %s\n", message);
    return false;
}

} // namespace

void* operator new[](std::size_t size)
{
    ++array_allocations;
    if(void* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}

void operator delete[](void* pointer) noexcept
{
    ++array_deallocations;
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    ++array_deallocations;
    std::free(pointer);
}

int main()
{
    const std::size_t before_empty_storage = array_allocations;
    Argument* empty_storage =
        krkrvita::allocate_event_argument_storage<Argument>(0);
    if(!check(empty_storage == nullptr,
              "zero arguments did not use null storage") ||
       !check(array_allocations == before_empty_storage,
              "zero-argument storage allocated an array")) {
        return 1;
    }

    bool empty_delivery_called = false;
    Argument** empty_delivery_arguments =
        reinterpret_cast<Argument**>(static_cast<std::size_t>(1));
    const std::size_t before_empty_delivery = array_allocations;
    krkrvita::with_event_argument_pointers<Argument>(
        nullptr, 0, [&](Argument** arguments) {
            empty_delivery_called = true;
            empty_delivery_arguments = arguments;
        });
    if(!check(empty_delivery_called,
              "zero-argument event was not delivered") ||
       !check(empty_delivery_arguments == nullptr,
              "zero-argument delivery did not pass null argv") ||
       !check(array_allocations == before_empty_delivery,
              "zero-argument delivery allocated an argv array")) {
        return 2;
    }

    const std::size_t before_nonempty_storage = array_allocations;
    const std::size_t before_nonempty_storage_free = array_deallocations;
    Argument* storage =
        krkrvita::allocate_event_argument_storage<Argument>(3);
    if(!check(storage != nullptr,
              "nonempty event storage was null") ||
       !check(array_allocations == before_nonempty_storage + 1,
              "nonempty event storage did not allocate exactly once")) {
        delete[] storage;
        return 3;
    }
    delete[] storage;
    if(!check(array_deallocations == before_nonempty_storage_free + 1,
              "nonempty event storage was not released")) {
        return 4;
    }

    Argument arguments[] = {{17}, {29}, {43}};
    bool nonempty_delivery_called = false;
    bool nonempty_delivery_exact = false;
    const std::size_t before_nonempty_delivery = array_allocations;
    const std::size_t before_nonempty_delivery_free = array_deallocations;
    krkrvita::with_event_argument_pointers(
        arguments, 3, [&](Argument** pointers) {
            nonempty_delivery_called = true;
            nonempty_delivery_exact = pointers != nullptr &&
                pointers[0] == &arguments[0] &&
                pointers[1] == &arguments[1] &&
                pointers[2] == &arguments[2] &&
                pointers[0]->value == 17 &&
                pointers[1]->value == 29 &&
                pointers[2]->value == 43;
        });
    if(!check(nonempty_delivery_called,
              "nonempty event was not delivered") ||
       !check(nonempty_delivery_exact,
              "nonempty event argv changed identity or order") ||
       !check(array_allocations == before_nonempty_delivery + 1,
              "nonempty delivery did not allocate exactly one argv array") ||
       !check(array_deallocations == before_nonempty_delivery_free + 1,
              "nonempty delivery did not release its argv array")) {
        return 5;
    }

    bool exception_observed = false;
    const std::size_t before_throw_delivery = array_allocations;
    const std::size_t before_throw_delivery_free = array_deallocations;
    try {
        krkrvita::with_event_argument_pointers(
            arguments, 3, [](Argument**) { throw 73; });
    } catch(int value) {
        exception_observed = value == 73;
    }
    if(!check(exception_observed,
              "delivery exception semantics changed") ||
       !check(array_allocations == before_throw_delivery + 1,
              "throwing delivery did not allocate one argv array") ||
       !check(array_deallocations == before_throw_delivery_free + 1,
              "throwing delivery leaked its argv array")) {
        return 6;
    }

    return 0;
}

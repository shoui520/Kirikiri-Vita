#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "ObjectList.h"

namespace {

std::size_t array_allocations = 0;

struct TestObject {};

bool check(bool condition, const char *message)
{
    if(condition) return true;
    std::fprintf(stderr, "ObjectList regression: %s\n", message);
    return false;
}

} // namespace

void *operator new[](std::size_t size)
{
    ++array_allocations;
    if(void *pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}

void operator delete[](void *pointer) noexcept
{
    std::free(pointer);
}

void operator delete[](void *pointer, std::size_t) noexcept
{
    std::free(pointer);
}

int main()
{
    TestObject first;
    TestObject second;
    tObjectList<TestObject> objects;
    if(!check(objects.Add(&first), "first insertion failed") ||
       !check(objects.Add(&second), "second insertion failed")) {
        return 1;
    }

    const std::size_t before_stable_locks = array_allocations;
    for(int iteration = 0; iteration < 256; ++iteration) {
        tObjectListSafeLockHolder<TestObject> lock(objects);
        if(!check(objects.GetSafeLockedObjectCount() == 2,
                  "stable safe lock changed the list count") ||
           !check(objects.GetSafeLockedObjectAt(0) == &first,
                  "stable safe lock changed the first entry") ||
           !check(objects.GetSafeLockedObjectAt(1) == &second,
                  "stable safe lock changed the second entry")) {
            return 1;
        }
    }
    if(!check(array_allocations == before_stable_locks,
              "unchanged safe locks allocated backing arrays")) {
        return 1;
    }

    const std::size_t before_locked_remove = array_allocations;
    {
        tObjectListSafeLockHolder<TestObject> lock(objects);
        if(!check(objects.Remove(&first), "locked removal failed") ||
           !check(objects.GetSafeLockedObjectCount() == 2,
                  "locked removal changed the stable snapshot size") ||
           !check(objects.GetSafeLockedObjectAt(0) == nullptr,
                  "locked removal remained visible in the stable snapshot")) {
            return 1;
        }
    }
    if(!check(array_allocations == before_locked_remove + 1,
              "locked removal did not create exactly one safety snapshot")) {
        return 1;
    }

    const std::size_t before_changed_compact = array_allocations;
    {
        tObjectListSafeLockHolder<TestObject> lock(objects);
        if(!check(objects.GetSafeLockedObjectCount() == 1,
                  "post-removal safe lock did not compact the null entry") ||
           !check(objects.GetSafeLockedObjectAt(0) == &second,
                  "post-removal compaction did not preserve the live entry")) {
            return 1;
        }
    }
    if(!check(array_allocations == before_changed_compact + 1,
              "changed list did not rebuild its compact backing array")) {
        return 1;
    }

    const std::size_t before_post_compact_locks = array_allocations;
    for(int iteration = 0; iteration < 256; ++iteration) {
        tObjectListSafeLockHolder<TestObject> lock(objects);
        if(!check(objects.GetSafeLockedObjectCount() == 1,
                  "post-compact stable count changed") ||
           !check(objects.GetSafeLockedObjectAt(0) == &second,
                  "post-compact stable entry changed")) {
            return 1;
        }
    }
    if(!check(array_allocations == before_post_compact_locks,
              "post-compact safe locks resumed allocating")) {
        return 1;
    }

    return 0;
}

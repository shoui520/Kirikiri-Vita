#include "krkrvita/vita_bitmap_allocator.hpp"

#include "krkrvita/retail_bootstrap.hpp"

#include <psp2/kernel/sysmem.h>

#include <cstdint>
#include <cstdlib>
#include <mutex>

namespace krkrvita {
namespace {

constexpr std::uint32_t kHeaderMagic = 0x4b564241u; // "KVBA"
constexpr std::uint32_t kMallocAllocation = 1;
constexpr std::uint32_t kMemblockAllocation = 2;

struct alignas(16) AllocationHeader {
    std::uint32_t magic;
    std::uint32_t kind;
    std::int32_t memblock_uid;
    std::uint32_t mapped_size;
    std::uintptr_t allocation_base;
    std::uint32_t requested_size;
    std::uint32_t reserved[2];
};

static_assert(sizeof(AllocationHeader) == kVitaBitmapAllocationHeaderBytes,
              "bitmap allocation header changed on Vita");

std::mutex budget_mutex;
std::uint64_t live_memblock_bytes = 0;
bool memblock_backend_reported = false;
bool malloc_fallback_reported = false;

void report_memblock_backend_locked() {
    if (memblock_backend_reported) return;
    krkrvita_boot_trace("yuri-bitmap-user-rw-memblocks-ready");
    memblock_backend_reported = true;
}

void release_budget(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(budget_mutex);
    if (bytes > live_memblock_bytes) {
        krkrvita_boot_trace("yuri-bitmap-memblock-budget-underflow");
        std::abort();
    }
    live_memblock_bytes -= bytes;
}

AllocationHeader* header_for_payload(void* payload) {
    return reinterpret_cast<AllocationHeader*>(
        static_cast<std::uint8_t*>(payload) - sizeof(AllocationHeader));
}

void initialize_header(AllocationHeader* header, std::uint32_t kind,
                       SceUID uid, std::size_t mapped_size, void* base,
                       std::size_t requested);

void* allocate_malloc(std::size_t size) {
    constexpr std::size_t overhead = sizeof(AllocationHeader) +
                                     kVitaBitmapPayloadAlignment - 1;
    if (size > std::numeric_limits<std::size_t>::max() - overhead)
        return nullptr;
    void* raw = std::malloc(size + overhead);
    if (!raw) return nullptr;
    const std::uintptr_t payload_address =
        (reinterpret_cast<std::uintptr_t>(raw) +
         sizeof(AllocationHeader) + kVitaBitmapPayloadAlignment - 1) &
        ~(static_cast<std::uintptr_t>(kVitaBitmapPayloadAlignment - 1));
    auto* header = reinterpret_cast<AllocationHeader*>(
        payload_address - sizeof(AllocationHeader));
    initialize_header(header, kMallocAllocation, -1, 0, raw, size);
    return reinterpret_cast<void*>(payload_address);
}

// Free USER_RW as the kernel sees it. Bitmaps at this size are allocated at
// scene granularity, not per frame, so one syscall per large allocation is
// cheaper than the fixed ceiling it replaces. A failed query returns 0, which
// routes the request to the malloc fallback rather than to an unbounded
// memblock allocation.
std::size_t free_user_memory() {
    SceKernelFreeMemorySizeInfo info{};
    info.size = sizeof(info);
    if (sceKernelGetFreeMemorySize(&info) < 0 || info.size_user <= 0) return 0;
    return static_cast<std::size_t>(info.size_user);
}

void report_malloc_fallback() {
    std::lock_guard<std::mutex> lock(budget_mutex);
    if (malloc_fallback_reported) return;
    krkrvita_boot_trace("yuri-bitmap-newlib-overflow-fallback");
    malloc_fallback_reported = true;
}

void initialize_header(AllocationHeader* header, std::uint32_t kind,
                       SceUID uid, std::size_t mapped_size, void* base,
                       std::size_t requested) {
    header->magic = kHeaderMagic;
    header->kind = kind;
    header->memblock_uid = uid;
    header->mapped_size = static_cast<std::uint32_t>(mapped_size);
    header->allocation_base = reinterpret_cast<std::uintptr_t>(base);
    header->requested_size = static_cast<std::uint32_t>(requested);
    header->reserved[0] = 0;
    header->reserved[1] = 0;
}

} // namespace

void* vita_bitmap_allocate(std::size_t size) {
    if (size == 0 || size > std::numeric_limits<std::uint32_t>::max())
        return nullptr;

    if (!vita_bitmap_uses_memblock(size)) return allocate_malloc(size);

    const std::size_t mapped_size = vita_bitmap_memblock_bytes(size);
    if (mapped_size == 0 ||
        mapped_size > std::numeric_limits<std::uint32_t>::max())
        return nullptr;
    bool memblock_reserved = false;
    {
        std::lock_guard<std::mutex> lock(budget_mutex);
        if (vita_bitmap_memblock_budget_allows(free_user_memory(), size)) {
            live_memblock_bytes += mapped_size;
            memblock_reserved = true;
            report_memblock_backend_locked();
        }
    }

    if (memblock_reserved) {
        const SceUID uid = sceKernelAllocMemBlock(
            "krkrvita bitmap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
            static_cast<SceSize>(mapped_size), nullptr);
        if (uid >= 0) {
            void* base = nullptr;
            if (sceKernelGetMemBlockBase(uid, &base) >= 0 && base) {
                const std::uintptr_t payload_address =
                    (reinterpret_cast<std::uintptr_t>(base) +
                     sizeof(AllocationHeader) +
                     kVitaBitmapPayloadAlignment - 1) &
                    ~(static_cast<std::uintptr_t>(
                        kVitaBitmapPayloadAlignment - 1));
                auto* header = reinterpret_cast<AllocationHeader*>(
                    payload_address - sizeof(AllocationHeader));
                initialize_header(header, kMemblockAllocation, uid,
                                  mapped_size, base, size);
                return reinterpret_cast<void*>(payload_address);
            }
            if (sceKernelFreeMemBlock(uid) >= 0) {
                release_budget(mapped_size);
            } else {
                // The kernel allocation is still live, so its reservation
                // must remain charged even though it cannot be used. Falsely
                // returning these pages to the budget would let later bitmap
                // allocations consume the non-bitmap safety margin.
                krkrvita_boot_trace("yuri-bitmap-memblock-cleanup-failed");
            }
        } else {
            release_budget(mapped_size);
        }
    }

    // Memblocks are the preferred tier because they are independently
    // reclaimable and immune to newlib fragmentation. This fallback covers the
    // cases the kernel cannot satisfy: the reserve is exhausted, the query
    // failed, or sceKernelAllocMemBlock refused the mapping. Spilling into the
    // fixed newlib heap keeps the capacity of Yuri's normal allocator instead
    // of turning a transient shortage into an immediate OOM.
    void* memory = allocate_malloc(size);
    if (memory) report_malloc_fallback();
    return memory;
}

void vita_bitmap_deallocate(void* memory) noexcept {
    if (!memory) return;
    AllocationHeader* header = header_for_payload(memory);
    if (header->magic != kHeaderMagic) {
        krkrvita_boot_trace("yuri-bitmap-allocation-header-invalid");
        std::abort();
    }
    const std::uint32_t kind = header->kind;
    const SceUID uid = header->memblock_uid;
    const std::size_t mapped_size = header->mapped_size;
    void* const base = reinterpret_cast<void*>(header->allocation_base);
    header->magic = 0;

    if (kind == kMallocAllocation) {
        std::free(base);
        return;
    }
    if (kind != kMemblockAllocation || uid < 0 || mapped_size == 0) {
        krkrvita_boot_trace("yuri-bitmap-allocation-kind-invalid");
        std::abort();
    }
    if (sceKernelFreeMemBlock(uid) < 0) {
        krkrvita_boot_trace("yuri-bitmap-memblock-free-failed");
        return;
    }
    release_budget(mapped_size);
}

std::uint64_t vita_bitmap_memblock_bytes_live() noexcept {
    std::lock_guard<std::mutex> lock(budget_mutex);
    return live_memblock_bytes;
}

} // namespace krkrvita

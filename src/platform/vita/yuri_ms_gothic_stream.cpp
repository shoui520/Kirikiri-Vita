#include "yuri_ms_gothic_stream.hpp"

#include "krkrvita/ms_gothic_fast_path.hpp"
#include "krkrvita/retail_bootstrap.hpp"
#include "tjs.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>

namespace {

constexpr const char* kMsGothicPath = "ux0:data/krkrvita/msgothic.ttc";

class SharedMsGothicFile final {
public:
    static SharedMsGothicFile* Open() noexcept {
        const SceUID descriptor = sceIoOpen(kMsGothicPath, SCE_O_RDONLY, 0);
        if (descriptor < 0) return nullptr;

        SceIoStat status{};
        if (sceIoGetstatByFd(descriptor, &status) < 0 || status.st_size <= 0 ||
            static_cast<std::uint64_t>(status.st_size) >
                std::numeric_limits<unsigned long>::max()) {
            sceIoClose(descriptor);
            return nullptr;
        }

        auto* file = new (std::nothrow) SharedMsGothicFile(
            descriptor, static_cast<std::uint64_t>(status.st_size));
        if (!file) sceIoClose(descriptor);
        return file;
    }

    std::uint64_t size() const noexcept { return size_; }

    std::size_t read(std::uint64_t offset,
                     void* destination,
                     std::size_t bytes) noexcept {
        if (!destination || bytes == 0 || offset >= size_) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!cache_ && !cache_allocation_failed_) {
            // Vita OOM recovery is called explicitly by the large-bitmap
            // allocators; there is no process-wide new_handler.  Therefore
            // this nothrow new cannot re-enter compact while mutex_ is held.
            cache_.reset(new (std::nothrow) krkrvita::MsGothicPageCache(
                size_, &ReadAt, this));
            if (cache_ && !cache_reported_) {
                krkrvita_boot_trace("yuri-msgothic-pread-cache-ready");
                cache_reported_ = true;
            }
            if (!cache_) cache_allocation_failed_ = true;
        }
        if (cache_) return cache_->read(offset, destination, bytes);
        if (!fallback_reported_) {
            krkrvita_boot_trace("yuri-msgothic-pread-cache-fallback");
            fallback_reported_ = true;
        }
        return read_fully(offset, destination, bytes);
    }

    void compact() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.reset();
        cache_allocation_failed_ = false;
    }

private:
    SharedMsGothicFile(SceUID descriptor, std::uint64_t size) noexcept
        : descriptor_(descriptor), size_(size) {}

    std::size_t read_fully(std::uint64_t offset,
                           void* destination,
                           std::size_t bytes) noexcept {
        auto* output = static_cast<std::uint8_t*>(destination);
        std::size_t completed = 0;
        while (completed < bytes && offset + completed < size_) {
            const std::uint64_t remaining64 = size_ - (offset + completed);
            const std::size_t request = remaining64 < bytes - completed
                ? static_cast<std::size_t>(remaining64)
                : bytes - completed;
            const std::size_t received = ReadAt(
                this, offset + completed, output + completed, request);
            if (received == 0 || received > request) break;
            completed += received;
        }
        return completed;
    }

    static std::size_t ReadAt(void* context,
                              std::uint64_t offset,
                              void* destination,
                              std::size_t bytes) noexcept {
        auto* self = static_cast<SharedMsGothicFile*>(context);
        const int result = sceIoPread(
            self->descriptor_, destination, static_cast<SceSize>(bytes),
            static_cast<SceOff>(offset));
        return result > 0 ? static_cast<std::size_t>(result) : 0;
    }

    SceUID descriptor_ = -1;
    std::uint64_t size_ = 0;
    std::mutex mutex_;
    std::unique_ptr<krkrvita::MsGothicPageCache> cache_;
    bool cache_reported_ = false;
    bool fallback_reported_ = false;
    bool cache_allocation_failed_ = false;
};

std::mutex shared_file_mutex;
SharedMsGothicFile* shared_file = nullptr;

SharedMsGothicFile* shared_ms_gothic_file() noexcept {
    // The fixed personal-use font is required for the process lifetime.  Keep
    // one descriptor and one cache across all Yuri face/rasterizer recreation;
    // individual streams contain only their own logical cursor.
    std::lock_guard<std::mutex> lock(shared_file_mutex);
    if (!shared_file) shared_file = SharedMsGothicFile::Open();
    return shared_file;
}

SharedMsGothicFile* existing_shared_ms_gothic_file() noexcept {
    std::lock_guard<std::mutex> lock(shared_file_mutex);
    return shared_file;
}

class MsGothicPreadStream final : public TJS::tTJSBinaryStream {
public:
    explicit MsGothicPreadStream(SharedMsGothicFile* file) noexcept
        : file_(file) {}

    tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset,
                                    tjs_int whence) override {
        std::uint64_t next = position_;
        if (krkrvita::ms_gothic_resolve_seek(
                position_, file_->size(), offset, whence, next))
            position_ = next;
        return position_;
    }

    tjs_uint TJS_INTF_METHOD Read(void* buffer, tjs_uint bytes) override {
        const std::size_t received = file_->read(position_, buffer, bytes);
        position_ += received;
        return static_cast<tjs_uint>(received);
    }

    tjs_uint TJS_INTF_METHOD Write(const void*, tjs_uint) override {
        return 0;
    }

    tjs_uint64 TJS_INTF_METHOD GetSize() override { return file_->size(); }

private:
    SharedMsGothicFile* file_ = nullptr;
    std::uint64_t position_ = 0;
};

}  // namespace

TJS::tTJSBinaryStream* krkrvita_create_ms_gothic_pread_stream() noexcept {
    SharedMsGothicFile* file = shared_ms_gothic_file();
    return file ? new (std::nothrow) MsGothicPreadStream(file) : nullptr;
}

void krkrvita_compact_ms_gothic_pread_cache() noexcept {
    if (SharedMsGothicFile* file = existing_shared_ms_gothic_file())
        file->compact();
}

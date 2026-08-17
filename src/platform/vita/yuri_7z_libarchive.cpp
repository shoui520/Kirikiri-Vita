#include "tjsCommHead.h"

#include "StorageIntf.h"
#include "UtilStreams.h"

extern "C" {
#include <archive.h>
#include <archive_entry.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

struct ArchiveReader {
    tTJSBinaryStream* stream = nullptr;
    std::array<unsigned char, 32 * 1024> buffer{};
};

la_ssize_t read_archive(struct archive*, void* opaque, const void** buffer) {
    auto* reader = static_cast<ArchiveReader*>(opaque);
    *buffer = reader->buffer.data();
    return static_cast<la_ssize_t>(
        reader->stream->Read(reader->buffer.data(), reader->buffer.size()));
}

la_int64_t seek_archive(struct archive*, void* opaque, la_int64_t offset,
                        int whence) {
    auto* reader = static_cast<ArchiveReader*>(opaque);
    int origin = TJS_BS_SEEK_SET;
    if (whence == SEEK_CUR) origin = TJS_BS_SEEK_CUR;
    else if (whence == SEEK_END) origin = TJS_BS_SEEK_END;
    return static_cast<la_int64_t>(reader->stream->Seek(offset, origin));
}

struct archive* open_7z_reader(ArchiveReader& reader) {
    reader.stream->SetPosition(0);
    struct archive* handle = archive_read_new();
    if (!handle) return nullptr;
    archive_read_support_filter_all(handle);
    archive_read_support_format_7zip(handle);
    archive_read_set_seek_callback(handle, seek_archive);
    if (archive_read_open2(handle, &reader, nullptr, read_archive, nullptr,
                           nullptr) != ARCHIVE_OK) {
        archive_read_free(handle);
        return nullptr;
    }
    return handle;
}

class VitaSevenZipArchive final : public tTVPArchive {
public:
    VitaSevenZipArchive(const ttstr& name, tTJSBinaryStream* stream)
        : tTVPArchive(name), stream_(stream) {}

    ~VitaSevenZipArchive() override { delete stream_; }

    bool open(bool normalize_names) {
        ArchiveReader reader{stream_};
        struct archive* handle = open_7z_reader(reader);
        if (!handle) return false;

        struct archive_entry* entry = nullptr;
        std::uint32_t archive_index = 0;
        while (archive_read_next_header(handle, &entry) == ARCHIVE_OK) {
            if (archive_entry_filetype(entry) == AE_IFREG) {
                const char* pathname = archive_entry_pathname_utf8(entry);
                if (!pathname) pathname = archive_entry_pathname(entry);
                if (!pathname) {
                    archive_read_free(handle);
                    return false;
                }
                Entry item{ttstr(pathname), archive_index};
                if (normalize_names) NormalizeInArchiveStorageName(item.name);
                entries_.push_back(std::move(item));
            }
            archive_read_data_skip(handle);
            ++archive_index;
        }
        archive_read_free(handle);
        if (normalize_names) {
            std::sort(entries_.begin(), entries_.end(),
                      [](const Entry& left, const Entry& right) {
                          return left.name < right.name;
                      });
        }
        return true;
    }

    void disown_stream() { stream_ = nullptr; }

    tjs_uint GetCount() override {
        return static_cast<tjs_uint>(entries_.size());
    }

    ttstr GetName(tjs_uint index) override { return entries_.at(index).name; }

    tTJSBinaryStream* CreateStreamByIndex(tjs_uint index) override {
        if (index >= entries_.size()) return nullptr;
        ArchiveReader reader{stream_};
        struct archive* handle = open_7z_reader(reader);
        if (!handle) return nullptr;

        struct archive_entry* entry = nullptr;
        std::uint32_t archive_index = 0;
        while (archive_read_next_header(handle, &entry) == ARCHIVE_OK &&
               archive_index != entries_[index].archive_index) {
            archive_read_data_skip(handle);
            ++archive_index;
        }
        if (!entry || archive_index != entries_[index].archive_index) {
            archive_read_free(handle);
            return nullptr;
        }

        const la_int64_t declared_size = archive_entry_size(entry);
        if (declared_size > static_cast<la_int64_t>(
                                std::numeric_limits<tjs_uint>::max())) {
            archive_read_free(handle);
            return nullptr;
        }

        auto* output = new tTVPMemoryStream();
        if (declared_size > 0)
            output->SetSize(static_cast<tjs_uint>(declared_size));
        std::array<unsigned char, 32 * 1024> data{};
        la_ssize_t read_size = 0;
        while ((read_size = archive_read_data(handle, data.data(), data.size())) > 0)
            output->WriteBuffer(data.data(), static_cast<tjs_uint>(read_size));
        archive_read_free(handle);
        if (read_size < 0) {
            delete output;
            return nullptr;
        }
        output->SetPosition(0);
        return output;
    }

private:
    struct Entry {
        ttstr name;
        std::uint32_t archive_index;
    };
    tTJSBinaryStream* stream_ = nullptr;
    std::vector<Entry> entries_;
};

} // namespace

tTVPArchive* TVPOpen7ZArchive(const ttstr& name, tTJSBinaryStream* stream,
                              bool normalize_names) {
    if (!stream) return nullptr;
    const tjs_uint64 original_position = stream->GetPosition();
    const bool has_signature = stream->ReadI16LE() == 0x7a37;
    stream->SetPosition(original_position);
    if (!has_signature) return nullptr;

    auto* result = new VitaSevenZipArchive(name, stream);
    if (!result->open(normalize_names)) {
        result->disown_stream();
        delete result;
        stream->SetPosition(original_position);
        return nullptr;
    }
    return result;
}

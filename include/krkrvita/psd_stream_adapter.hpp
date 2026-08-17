#pragma once

// The maintained PSD plug-in exposes generated layer images as Yuri's small
// COM-shaped IStream.  Yuri's Windows tTVPBinaryStreamAdapter is intentionally
// compiled out on Vita, so keep the boundary adapter local to PSD rather than
// re-enabling the legacy Windows implementation globally.

class krkrvita_psd_stream_adapter final : public tTJSBinaryStream {
    IStream *stream_;

public:
    explicit krkrvita_psd_stream_adapter(IStream *stream) : stream_(stream) {
        if (stream_)
            stream_->AddRef();
    }

    ~krkrvita_psd_stream_adapter() override {
        if (stream_)
            stream_->Release();
    }

    tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) override {
        if (!stream_)
            return 0;
        DWORD origin = STREAM_SEEK_SET;
        if (whence == TJS_BS_SEEK_CUR)
            origin = STREAM_SEEK_CUR;
        else if (whence == TJS_BS_SEEK_END)
            origin = STREAM_SEEK_END;
        LARGE_INTEGER move{};
        move.QuadPart = offset;
        ULARGE_INTEGER position{};
        if (FAILED(stream_->Seek(move, origin, &position)))
            return 0;
        return position.QuadPart;
    }

    tjs_uint TJS_INTF_METHOD Read(void *buffer, tjs_uint read_size) override {
        if (!stream_)
            return 0;
        ULONG read = 0;
        if (FAILED(stream_->Read(buffer, static_cast<ULONG>(read_size), &read)))
            return 0;
        return static_cast<tjs_uint>(read);
    }

    tjs_uint TJS_INTF_METHOD Write(const void *buffer,
                                   tjs_uint write_size) override {
        if (!stream_)
            return 0;
        ULONG written = 0;
        if (FAILED(stream_->Write(buffer, static_cast<ULONG>(write_size),
                                  &written)))
            return 0;
        return static_cast<tjs_uint>(written);
    }

    void TJS_INTF_METHOD SetEndOfStorage() override {
        if (!stream_)
            return;
        ULARGE_INTEGER size{};
        size.QuadPart = GetPosition();
        stream_->SetSize(size);
    }

    tjs_uint64 TJS_INTF_METHOD GetSize() override {
        if (!stream_)
            return 0;
        STATSTG stat{};
        if (FAILED(stream_->Stat(&stat, STATFLAG_NONAME)))
            return 0;
        return stat.cbSize.QuadPart;
    }
};

inline tTJSBinaryStream *krkrvita_psd_create_binary_stream(IStream *stream) {
    if (!stream)
        return nullptr;
    return new krkrvita_psd_stream_adapter(stream);
}

#pragma once

namespace TJS {
class tTJSBinaryStream;
}

// Open only the required personal-use MS Gothic collection.  nullptr means
// the caller must retain Yuri's normal storage stream fallback.
TJS::tTJSBinaryStream* krkrvita_create_ms_gothic_pread_stream() noexcept;

// Drop only the shared 512 KiB read cache under Kirikiri's maximum compact
// event.  The open descriptor and each stream's logical position remain valid.
void krkrvita_compact_ms_gothic_pread_cache() noexcept;

#include "tjsCommHead.h"

#include "StorageIntf.h"
#include "TextStream.h"
#include "krkrvita/retail_bootstrap.hpp"
#include "ncbind/ncbind.hpp"
#include "sqlite3.h"
#include "tjsArray.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#define NCB_MODULE_NAME TJS_W("sqlite3.dll")

namespace {

std::string tjs_to_utf8(const ttstr& text) {
    std::string output;
    if (!TVPStringEncode(text, output, TJS_W("utf8")))
        TVPThrowExceptionMessage(TJS_W("Cannot encode SQLite text as UTF-8"));
    return output;
}

ttstr utf8_to_tjs(const char* text, std::size_t size) {
    if (!text) return ttstr();
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        TVPThrowExceptionMessage(TJS_W("SQLite text is too large"));
    ttstr output;
    if (!TVPStringDecode(text, static_cast<int>(size), output, TJS_W("utf8")))
        TVPThrowExceptionMessage(TJS_W("SQLite returned invalid UTF-8"));
    return output;
}

struct TvpSqliteFile {
    sqlite3_file base;
    tTJSBinaryStream* stream;
};

int tvp_file_close(sqlite3_file* file) {
    auto* self = reinterpret_cast<TvpSqliteFile*>(file);
    delete self->stream;
    self->stream = nullptr;
    return SQLITE_OK;
}

int tvp_file_read(sqlite3_file* file, void* output, int amount,
                  sqlite3_int64 offset) {
    auto* self = reinterpret_cast<TvpSqliteFile*>(file);
    if (!self->stream || amount < 0 || offset < 0) return SQLITE_IOERR_READ;
    try {
        self->stream->SetPosition(static_cast<tjs_uint64>(offset));
        auto* bytes = static_cast<std::uint8_t*>(output);
        int consumed = 0;
        while (consumed < amount) {
            const auto request = static_cast<tjs_uint>(std::min<int>(
                amount - consumed, 64 * 1024));
            const auto read = self->stream->Read(bytes + consumed, request);
            if (!read) break;
            consumed += static_cast<int>(read);
        }
        if (consumed == amount) return SQLITE_OK;
        std::memset(bytes + consumed, 0,
                    static_cast<std::size_t>(amount - consumed));
        return SQLITE_IOERR_SHORT_READ;
    } catch (...) {
        return SQLITE_IOERR_READ;
    }
}

int tvp_file_write(sqlite3_file*, const void*, int, sqlite3_int64) {
    return SQLITE_READONLY;
}
int tvp_file_truncate(sqlite3_file*, sqlite3_int64) { return SQLITE_READONLY; }
int tvp_file_sync(sqlite3_file*, int) { return SQLITE_OK; }

int tvp_file_size(sqlite3_file* file, sqlite3_int64* size) {
    auto* self = reinterpret_cast<TvpSqliteFile*>(file);
    if (!self->stream || !size) return SQLITE_IOERR_FSTAT;
    try {
        const auto value = self->stream->GetSize();
        if (value > static_cast<tjs_uint64>(
                        std::numeric_limits<sqlite3_int64>::max()))
            return SQLITE_IOERR_FSTAT;
        *size = static_cast<sqlite3_int64>(value);
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_IOERR_FSTAT;
    }
}

int tvp_file_lock(sqlite3_file*, int) { return SQLITE_OK; }
int tvp_file_unlock(sqlite3_file*, int) { return SQLITE_OK; }
int tvp_file_reserved(sqlite3_file*, int* result) {
    if (result) *result = 0;
    return SQLITE_OK;
}
int tvp_file_control(sqlite3_file*, int, void*) { return SQLITE_NOTFOUND; }
int tvp_file_sector_size(sqlite3_file*) { return 4096; }
int tvp_file_characteristics(sqlite3_file*) {
    return SQLITE_IOCAP_IMMUTABLE | SQLITE_IOCAP_POWERSAFE_OVERWRITE;
}

const sqlite3_io_methods tvp_io_methods = {
    1,
    tvp_file_close,
    tvp_file_read,
    tvp_file_write,
    tvp_file_truncate,
    tvp_file_sync,
    tvp_file_size,
    tvp_file_lock,
    tvp_file_unlock,
    tvp_file_reserved,
    tvp_file_control,
    tvp_file_sector_size,
    tvp_file_characteristics,
};

int tvp_vfs_open(sqlite3_vfs*, const char* name, sqlite3_file* output,
                 int flags, int* output_flags) {
    if (!name || !output || (flags & SQLITE_OPEN_READONLY) == 0)
        return SQLITE_CANTOPEN;
    auto* file = reinterpret_cast<TvpSqliteFile*>(output);
    std::memset(file, 0, sizeof(*file));
    try {
        const auto storage = utf8_to_tjs(name, std::strlen(name));
        file->stream = TVPCreateStream(storage, TJS_BS_READ);
        if (!file->stream) return SQLITE_CANTOPEN;
        file->base.pMethods = &tvp_io_methods;
        if (output_flags) *output_flags = SQLITE_OPEN_READONLY;
        return SQLITE_OK;
    } catch (...) {
        delete file->stream;
        file->stream = nullptr;
        return SQLITE_CANTOPEN;
    }
}

int tvp_vfs_delete(sqlite3_vfs*, const char*, int) { return SQLITE_READONLY; }

int tvp_vfs_access(sqlite3_vfs*, const char* name, int, int* result) {
    if (!result) return SQLITE_IOERR_ACCESS;
    *result = 0;
    if (!name) return SQLITE_OK;
    try {
        const auto storage = utf8_to_tjs(name, std::strlen(name));
        *result = TVPIsExistentStorage(storage) ? 1 : 0;
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_IOERR_ACCESS;
    }
}

int tvp_vfs_full_path(sqlite3_vfs*, const char* name, int output_size,
                      char* output) {
    if (!name || !output || output_size <= 0) return SQLITE_CANTOPEN;
    const auto length = std::strlen(name);
    if (length >= static_cast<std::size_t>(output_size)) return SQLITE_CANTOPEN;
    std::memcpy(output, name, length + 1);
    return SQLITE_OK;
}

void* tvp_vfs_dlopen(sqlite3_vfs*, const char*) { return nullptr; }
void tvp_vfs_dlerror(sqlite3_vfs*, int size, char* output) {
    if (output && size > 0) output[0] = '\0';
}
void (*tvp_vfs_dlsym(sqlite3_vfs*, void*, const char*))(void) {
    return nullptr;
}
void tvp_vfs_dlclose(sqlite3_vfs*, void*) {}

sqlite3_vfs tvp_vfs{};
std::once_flag tvp_vfs_once;

sqlite3_vfs* ensure_tvp_vfs() {
    std::call_once(tvp_vfs_once, [] {
        sqlite3_vfs* base = sqlite3_vfs_find(nullptr);
        tvp_vfs.iVersion = 1;
        tvp_vfs.szOsFile = sizeof(TvpSqliteFile);
        tvp_vfs.mxPathname = 4096;
        tvp_vfs.zName = "krkrvita-tvp";
        tvp_vfs.pAppData = base;
        tvp_vfs.xOpen = tvp_vfs_open;
        tvp_vfs.xDelete = tvp_vfs_delete;
        tvp_vfs.xAccess = tvp_vfs_access;
        tvp_vfs.xFullPathname = tvp_vfs_full_path;
        tvp_vfs.xDlOpen = tvp_vfs_dlopen;
        tvp_vfs.xDlError = tvp_vfs_dlerror;
        tvp_vfs.xDlSym = tvp_vfs_dlsym;
        tvp_vfs.xDlClose = tvp_vfs_dlclose;
        if (base) {
            tvp_vfs.xRandomness = base->xRandomness;
            tvp_vfs.xSleep = base->xSleep;
            tvp_vfs.xCurrentTime = base->xCurrentTime;
            tvp_vfs.xGetLastError = base->xGetLastError;
        }
        if (sqlite3_vfs_register(&tvp_vfs, 0) != SQLITE_OK)
            TVPThrowExceptionMessage(TJS_W("Cannot register SQLite storage VFS"));
    });
    return &tvp_vfs;
}

// These tables and the contains-not-counting result are the contract of the
// official KiriKiri sqlite3 plug-in's extend.cpp.  Keep them here instead of
// replacing ncnt with a vaguely similar Unicode/case-fold operation: games
// use this exact search normalization for their scene databases.
constexpr tjs_char sqlite_normalize_before[] = TJS_W(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹＺ"
    "ａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒｓｔｕｖｗｘｙｚ"
    "１２３４５６７８９０"
    "あいうえおかきくけこさしすせそたちつてとなにぬねの"
    "はひふへほまみむめもやゆよらりるれろわゐゑをんぁぃぅぇぉっゃゅょ"
    "がぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽ"
    "アイウエオカキクケコサシスセソタチツテトナニヌネノ"
    "ハヒフヘホマミムメモヤユヨラリルレロワヰヱヲンァィゥェォッャュョ"
    "ガギグゲゴザジズゼゾダヂヅデドバビブベボパピプペポ"
    "ｧｨｩｪｫｯｬｭｮ"
    "ー・、。ｰ"
    "[]{}"
    "，．：；？！´｀＾￣＿〇ー―‐／＼～"
    "｜‘’“”（）〔〕［］｛｝〈〉《》「」『』【】＋－×＝"
    "＜＞￥＄％＃＆＊＠★●◎◆■▲▼※");

constexpr tjs_char sqlite_normalize_after[] = TJS_W(
    "abcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyz"
    "abcdefghijklmnopqrstuvwxyz"
    "1234567890"
    "ｱｲｳｴｵｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾅﾆﾇﾈﾉ"
    "ﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓﾔﾕﾖﾗﾘﾙﾚﾛﾜｲｴｦﾝｱｲｳｴｵﾂﾔﾕﾖ"
    "ｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾊﾋﾌﾍﾎﾊﾋﾌﾍﾎ"
    "ｱｲｳｴｵｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾅﾆﾇﾈﾉ"
    "ﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓﾔﾕﾖﾗﾘﾙﾚﾛﾜｲｴｦﾝｱｲｳｴｵﾂﾔﾕﾖ"
    "ｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾊﾋﾌﾍﾎﾊﾋﾌﾍﾎ"
    "ｱｲｳｴｵﾂﾔﾕﾖ"
    "-･,.-"
    "()()"
    ",.:;?!'`^~_◯---/＼-"
    "|`'\"\"()()()()()()｢｣｢｣()+-x="
    "<>\\$%#&*@☆○○◇□△▽*");

constexpr tjs_char sqlite_normalize_clear[] = TJS_W("ﾞ゛゜");

static_assert(std::size(sqlite_normalize_before) ==
              std::size(sqlite_normalize_after));

const std::array<tjs_char, 65536>& sqlite_normalize_table() {
    static const auto table = [] {
        std::array<tjs_char, 65536> value{};
        for (std::size_t index = 0; index < value.size(); ++index)
            value[index] = static_cast<tjs_char>(index);
        const tjs_char* before = sqlite_normalize_before;
        const tjs_char* after = sqlite_normalize_after;
        while (*before) value[static_cast<tjs_uint16>(*before++)] = *after++;
        for (const tjs_char* clear = sqlite_normalize_clear; *clear; ++clear)
            value[static_cast<tjs_uint16>(*clear)] = 0;
        return value;
    }();
    return table;
}

ttstr sqlite_normalize(const ttstr& source) {
    const auto& table = sqlite_normalize_table();
    ttstr output;
    for (const tjs_char* cursor = source.c_str(); *cursor; ++cursor) {
        const tjs_char normalized = table[static_cast<tjs_uint16>(*cursor)];
        if (normalized) output += normalized;
    }
    return output;
}

bool sqlite_contains(sqlite3_value* haystack, sqlite3_value* needle,
                     bool normalized) {
    if (!haystack || !needle || sqlite3_value_type(haystack) == SQLITE_NULL ||
        sqlite3_value_type(needle) == SQLITE_NULL)
        return false;
    const auto* haystack_text = reinterpret_cast<const char*>(
        sqlite3_value_text(haystack));
    const auto* needle_text = reinterpret_cast<const char*>(
        sqlite3_value_text(needle));
    if (!haystack_text || !needle_text) return false;
    ttstr haystack_tjs = utf8_to_tjs(haystack_text, std::strlen(haystack_text));
    ttstr needle_tjs = utf8_to_tjs(needle_text, std::strlen(needle_text));
    if (normalized) {
        haystack_tjs = sqlite_normalize(haystack_tjs);
        needle_tjs = sqlite_normalize(needle_tjs);
    }
    return TJS_strstr(haystack_tjs.c_str(), needle_tjs.c_str()) != nullptr;
}

void sqlite_cnt(sqlite3_context* context, int argc, sqlite3_value** argv) {
    sqlite3_result_int(context,
        argc >= 2 && sqlite_contains(argv[0], argv[1], false));
}

void sqlite_ncnt(sqlite3_context* context, int argc, sqlite3_value** argv) {
    sqlite3_result_int(context,
        argc >= 2 && sqlite_contains(argv[0], argv[1], true));
}

struct SqliteState {
    sqlite3* handle = nullptr;
    int open_error = SQLITE_OK;
    std::string open_message;

    ~SqliteState() {
        if (handle) sqlite3_close_v2(handle);
    }

    int error_code() const {
        return handle ? sqlite3_errcode(handle) : open_error;
    }

    std::string error_message() const {
        if (handle) return sqlite3_errmsg(handle);
        return open_message;
    }
};

void set_sqlite_result(sqlite3_stmt* statement, int column,
                       tTJSVariant& output) {
    switch (sqlite3_column_type(statement, column)) {
    case SQLITE_INTEGER:
        output = static_cast<tjs_int64>(sqlite3_column_int64(statement, column));
        return;
    case SQLITE_FLOAT:
        output = static_cast<tjs_real>(sqlite3_column_double(statement, column));
        return;
    case SQLITE_TEXT: {
        const auto* data = reinterpret_cast<const char*>(
            sqlite3_column_text(statement, column));
        const auto size = sqlite3_column_bytes(statement, column);
        output = utf8_to_tjs(data, static_cast<std::size_t>(size));
        return;
    }
    case SQLITE_BLOB: {
        const auto* data = static_cast<const std::uint8_t*>(
            sqlite3_column_blob(statement, column));
        const auto size = sqlite3_column_bytes(statement, column);
        output = tTJSVariant(data, static_cast<tjs_uint>(size));
        return;
    }
    case SQLITE_NULL:
    default:
        output.Clear();
        return;
    }
}

int bind_value(sqlite3_stmt* statement, int position,
               const tTJSVariant& value) {
    switch (value.Type()) {
    case tvtVoid:
        return sqlite3_bind_null(statement, position);
    case tvtInteger:
        return sqlite3_bind_int64(statement, position,
                                  static_cast<sqlite3_int64>(value.AsInteger()));
    case tvtReal:
        return sqlite3_bind_double(statement, position,
                                   static_cast<double>(value.AsReal()));
    case tvtString: {
        const auto text = tjs_to_utf8(ttstr(value));
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return SQLITE_TOOBIG;
        return sqlite3_bind_text(statement, position, text.data(),
                                 static_cast<int>(text.size()), SQLITE_TRANSIENT);
    }
    case tvtOctet: {
        const auto* octet = value.AsOctetNoAddRef();
        if (!octet) return sqlite3_bind_null(statement, position);
        if (octet->GetLength() > static_cast<tjs_uint>(
                                     std::numeric_limits<int>::max()))
            return SQLITE_TOOBIG;
        return sqlite3_bind_blob(statement, position, octet->GetData(),
                                 static_cast<int>(octet->GetLength()),
                                 SQLITE_TRANSIENT);
    }
    default:
        return SQLITE_MISMATCH;
    }
}

int bind_values(sqlite3_stmt* statement, const tTJSVariant* values) {
    if (!values || values->Type() == tvtVoid) return SQLITE_OK;
    if (values->Type() != tvtObject) return SQLITE_MISMATCH;

    auto closure = values->AsObjectClosureNoAddRef();
    iTJSDispatch2* object = closure.SelectObjectNoAddRef();
    if (!object) return SQLITE_MISMATCH;

    tTJSArrayNI* array = nullptr;
    if (TJS_SUCCEEDED(object->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
            reinterpret_cast<iTJSNativeInstance**>(&array))) && array) {
        for (std::size_t index = 0; index < array->Items.size(); ++index) {
            if (index >= static_cast<std::size_t>(sqlite3_bind_parameter_count(statement)))
                return SQLITE_RANGE;
            const int status = bind_value(statement, static_cast<int>(index + 1),
                                          array->Items[index]);
            if (status != SQLITE_OK) return status;
        }
        return SQLITE_OK;
    }

    const int count = sqlite3_bind_parameter_count(statement);
    for (int position = 1; position <= count; ++position) {
        const char* sqlite_name = sqlite3_bind_parameter_name(statement, position);
        if (!sqlite_name || !sqlite_name[0]) return SQLITE_RANGE;
        const char* plain_name = sqlite_name + 1;
        const auto name = utf8_to_tjs(plain_name, std::strlen(plain_name));
        tTJSVariant value;
        if (TJS_FAILED(object->PropGet(0, name.c_str(), nullptr, &value, object)))
            return SQLITE_RANGE;
        const int status = bind_value(statement, position, value);
        if (status != SQLITE_OK) return status;
    }
    return SQLITE_OK;
}

class NI_Sqlite final : public tTJSNativeInstance {
public:
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams,
                                        tTJSVariant** param,
                                        iTJSDispatch2*) override {
        if (numparams < 1) return TJS_E_BADPARAMCOUNT;
        state_ = std::make_shared<SqliteState>();
        const ttstr storage(*param[0]);
        const bool readonly = numparams >= 2 && param[1]->operator bool();
        std::string path;
        const char* vfs = nullptr;
        int flags = 0;
        if (readonly) {
            ensure_tvp_vfs();
            path = tjs_to_utf8(storage);
            vfs = tvp_vfs.zName;
            flags = SQLITE_OPEN_READONLY;
        } else {
            if (storage.IsEmpty() || storage.c_str()[0] == TJS_W(':')) {
                path = tjs_to_utf8(storage);
            } else {
                ttstr local = TVPNormalizeStorageName(storage);
                TVPGetLocalName(local);
                path = tjs_to_utf8(local);
            }
            flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        }
        state_->open_error = sqlite3_open_v2(
            path.c_str(), &state_->handle, flags, vfs);
        if (state_->open_error != SQLITE_OK) {
            if (state_->handle) state_->open_message = sqlite3_errmsg(state_->handle);
            else state_->open_message = sqlite3_errstr(state_->open_error);
            return TJS_S_OK;
        }
        sqlite3_extended_result_codes(state_->handle, 0);
        sqlite3_create_function_v2(state_->handle, "cnt", 2,
            SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, sqlite_cnt,
            nullptr, nullptr, nullptr);
        sqlite3_create_function_v2(state_->handle, "ncnt", 2,
            SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, sqlite_ncnt,
            nullptr, nullptr, nullptr);
        return TJS_S_OK;
    }

    std::shared_ptr<SqliteState> state() const { return state_; }

    bool execute(const ttstr& sql, const tTJSVariant* params,
                 const tTJSVariant* callback, tTJSVariant* first_value) {
        if (!state_ || !state_->handle) return false;
        const auto utf8 = tjs_to_utf8(sql);
        const char* cursor = utf8.c_str();
        const char* end = cursor + utf8.size();
        bool first_statement = true;
        while (cursor < end) {
            sqlite3_stmt* raw = nullptr;
            const char* tail = nullptr;
            int status = sqlite3_prepare_v2(state_->handle, cursor,
                                            static_cast<int>(end - cursor),
                                            &raw, &tail);
            std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>
                statement(raw, sqlite3_finalize);
            if (status != SQLITE_OK) return false;
            cursor = tail ? tail : end;
            if (!statement) continue;
            if (first_statement) {
                status = bind_values(statement.get(), params);
                if (status != SQLITE_OK) return false;
            }
            first_statement = false;
            for (;;) {
                status = sqlite3_step(statement.get());
                if (status == SQLITE_DONE) break;
                if (status != SQLITE_ROW) return false;
                const int columns = sqlite3_column_count(statement.get());
                if (first_value) {
                    if (columns > 0) set_sqlite_result(statement.get(), 0, *first_value);
                    return columns > 0;
                }
                if (callback && callback->Type() == tvtObject) {
                    std::vector<tTJSVariant> values(static_cast<std::size_t>(columns));
                    std::vector<tTJSVariant*> argv(static_cast<std::size_t>(columns));
                    for (int column = 0; column < columns; ++column) {
                        set_sqlite_result(statement.get(), column,
                                          values[static_cast<std::size_t>(column)]);
                        argv[static_cast<std::size_t>(column)] =
                            &values[static_cast<std::size_t>(column)];
                    }
                    const auto closure = callback->AsObjectClosureNoAddRef();
                    const auto called = closure.FuncCall(
                        0, nullptr, nullptr, nullptr, columns,
                        argv.empty() ? nullptr : argv.data(), nullptr);
                    if (TJS_FAILED(called)) return false;
                }
            }
        }
        return true;
    }

private:
    std::shared_ptr<SqliteState> state_;
};

class NI_SqliteStatement final : public tTJSNativeInstance {
public:
    ~NI_SqliteStatement() override { close(); }

    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams,
                                        tTJSVariant** param,
                                        iTJSDispatch2*) override;

    void attach(const std::shared_ptr<SqliteState>& state) {
        close();
        state_ = state;
    }

    int open(const std::shared_ptr<SqliteState>& state, const ttstr& sql,
             const tTJSVariant* params = nullptr) {
        close();
        state_ = state;
        bind_position_ = 1;
        if (!state_ || !state_->handle) return SQLITE_MISUSE;
        const auto utf8 = tjs_to_utf8(sql);
        int status = sqlite3_prepare_v2(state_->handle, utf8.data(),
                                        static_cast<int>(utf8.size()),
                                        &statement_, nullptr);
        if (status == SQLITE_OK && params)
            status = bind_values(statement_, params);
        return status;
    }

    int reopen(const ttstr& sql, const tTJSVariant* params = nullptr) {
        const auto state = state_;
        return open(state, sql, params);
    }

    int close() {
        bind_position_ = 1;
        const int status = statement_ ? sqlite3_finalize(statement_) : SQLITE_OK;
        statement_ = nullptr;
        return status;
    }

    int reset() {
        if (!statement_) return SQLITE_MISUSE;
        bind_position_ = 1;
        return sqlite3_reset(statement_);
    }

    int bind(const tTJSVariant& params) {
        if (!statement_) return SQLITE_MISUSE;
        return bind_values(statement_, &params);
    }

    int bind_at(const tTJSVariant& value, const tTJSVariant* requested) {
        if (!statement_) return SQLITE_MISUSE;
        int position = bind_position_;
        if (requested) {
            if (requested->Type() == tvtString) {
                const auto name = tjs_to_utf8(ttstr(*requested));
                position = sqlite3_bind_parameter_index(statement_, name.c_str());
            } else {
                position = static_cast<int>(requested->AsInteger()) + 1;
            }
        }
        if (position <= 0) return SQLITE_RANGE;
        const int status = bind_value(statement_, position, value);
        if (status == SQLITE_OK) bind_position_ = position + 1;
        return status;
    }

    int bind_next(const tTJSVariant& value) {
        return bind_at(value, nullptr);
    }

    int exec() {
        if (!statement_) return SQLITE_MISUSE;
        const int status = sqlite3_step(statement_);
        if (status != SQLITE_ROW) reset();
        return status;
    }

    bool step() {
        if (!statement_) return false;
        if (sqlite3_step(statement_) == SQLITE_ROW) return true;
        reset();
        return false;
    }

    void get(tjs_int numparams, tTJSVariant** param, tTJSVariant* result) {
        if (!result || !statement_) return;
        const int columns = sqlite3_column_count(statement_);
        if (numparams == 0 || param[0]->Type() == tvtVoid) {
            iTJSDispatch2* array = TJSCreateArrayObject();
            try {
                tTJSArrayNI* native = nullptr;
                if (TJS_FAILED(array->NativeInstanceSupport(
                        TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
                        reinterpret_cast<iTJSNativeInstance**>(&native))) || !native) {
                    TVPThrowExceptionMessage(TJS_W("Cannot create SQLite result array"));
                }
                native->Items.resize(static_cast<std::size_t>(columns));
                for (int column = 0; column < columns; ++column)
                    set_sqlite_result(statement_, column,
                        native->Items[static_cast<std::size_t>(column)]);
                result->SetObject(array, array);
                array->Release();
            } catch (...) {
                array->Release();
                throw;
            }
            return;
        }

        const int column = column_number(*param[0]);
        if (column < 0 || column >= columns ||
            sqlite3_column_type(statement_, column) == SQLITE_NULL) {
            if (numparams >= 2) *result = *param[1];
            else result->Clear();
            return;
        }
        set_sqlite_result(statement_, column, *result);
    }

    int column_count() const {
        return statement_ ? sqlite3_column_count(statement_) : 0;
    }
    int data_count() const {
        return statement_ ? sqlite3_data_count(statement_) : 0;
    }
    ttstr sql() const {
        if (!statement_) return ttstr();
        const char* sql = sqlite3_sql(statement_);
        return sql ? utf8_to_tjs(sql, std::strlen(sql)) : ttstr();
    }

    int column_number(const tTJSVariant& requested) const {
        if (!statement_) return -1;
        if (requested.Type() != tvtString)
            return static_cast<int>(requested.AsInteger());
        const ttstr name(requested);
        const int columns = column_count();
        for (int column = 0; column < columns; ++column) {
            const char* raw_name = sqlite3_column_name(statement_, column);
            if (!raw_name) continue;
            const ttstr candidate = utf8_to_tjs(raw_name, std::strlen(raw_name));
            if (TJS_stricmp(name.c_str(), candidate.c_str()) == 0)
                return column;
        }
        return -1;
    }
    sqlite3_stmt* raw() const { return statement_; }

private:
    std::shared_ptr<SqliteState> state_;
    sqlite3_stmt* statement_ = nullptr;
    int bind_position_ = 1;
};

static tjs_int32 ClassID_Sqlite = -1;
static tjs_int32 ClassID_SqliteStatement = -1;

NI_Sqlite* get_sqlite_instance(iTJSDispatch2* object) {
    if (!object) return nullptr;
    NI_Sqlite* instance = nullptr;
    if (TJS_FAILED(object->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, ClassID_Sqlite,
            reinterpret_cast<iTJSNativeInstance**>(&instance))))
        return nullptr;
    return instance;
}

tjs_error TJS_INTF_METHOD NI_SqliteStatement::Construct(
        tjs_int numparams, tTJSVariant** param, iTJSDispatch2*) {
    if (numparams < 1) return TJS_E_BADPARAMCOUNT;
    if (param[0]->Type() != tvtObject) return TJS_E_INVALIDPARAM;
    auto closure = param[0]->AsObjectClosureNoAddRef();
    auto* db = get_sqlite_instance(closure.SelectObjectNoAddRef());
    if (!db) return TJS_E_INVALIDPARAM;
    attach(db->state());
    if (numparams < 2) return TJS_S_OK;
    const int status = reopen(ttstr(*param[1]),
                              numparams >= 3 ? param[2] : nullptr);
    return status == SQLITE_OK ? TJS_S_OK : TJS_E_FAIL;
}

iTJSNativeInstance* TJS_INTF_METHOD create_sqlite_instance() {
    return new NI_Sqlite();
}
iTJSNativeInstance* TJS_INTF_METHOD create_statement_instance() {
    return new NI_SqliteStatement();
}

void set_constant(iTJSDispatch2* object, const tjs_char* name, tjs_int value) {
    tTJSVariant variant(value);
    if (TJS_FAILED(object->PropSet(TJS_MEMBERENSURE | TJS_STATICMEMBER,
                                   name, nullptr, &variant, object))) {
        TVPThrowExceptionMessage(TJS_W("Cannot register SQLite constant"));
    }
}

#ifdef TJS_NATIVE_CLASSID_NAME
#undef TJS_NATIVE_CLASSID_NAME
#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID
#endif
#define TJS_NATIVE_CLASSID_NAME ClassID_Sqlite
iTJSDispatch2* create_sqlite_class() {
    tTJSNativeClassForPlugin* classobj =
        TJSCreateNativeClassForPlugin(TJS_W("Sqlite"), create_sqlite_instance);
#define TJS_NCM_REG_THIS classobj
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
    TJS_BEGIN_NATIVE_MEMBERS(Sqlite)
        TJS_DECL_EMPTY_FINALIZE_METHOD
        TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, NI_Sqlite, Sqlite)
        { return TJS_S_OK; }
        TJS_END_NATIVE_CONSTRUCTOR_DECL(Sqlite)

        TJS_BEGIN_NATIVE_METHOD_DECL(exec)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_Sqlite);
            if (numparams < 1) return TJS_E_BADPARAMCOUNT;
            const tTJSVariant* params = numparams >= 2 ? param[1] : nullptr;
            const tTJSVariant* callback = numparams >= 3 ? param[2] : nullptr;
            const bool ok = _this->execute(ttstr(*param[0]), params, callback, nullptr);
            if (result) *result = ok;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(exec)

        TJS_BEGIN_NATIVE_METHOD_DECL(execValue)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_Sqlite);
            if (numparams < 1) return TJS_E_BADPARAMCOUNT;
            const tTJSVariant* params = numparams >= 2 ? param[1] : nullptr;
            tTJSVariant value;
            if (_this->execute(ttstr(*param[0]), params, nullptr, &value) && result)
                *result = value;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(execValue)

        TJS_BEGIN_NATIVE_METHOD_DECL(begin)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_Sqlite);
            const bool ok = _this->execute(TJS_W("BEGIN"), nullptr, nullptr, nullptr);
            if (result) *result = ok;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(begin)
        TJS_BEGIN_NATIVE_METHOD_DECL(commit)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_Sqlite);
            const bool ok = _this->execute(TJS_W("COMMIT"), nullptr, nullptr, nullptr);
            if (result) *result = ok;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(commit)
        TJS_BEGIN_NATIVE_METHOD_DECL(rollback)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_Sqlite);
            const bool ok = _this->execute(TJS_W("ROLLBACK"), nullptr, nullptr, nullptr);
            if (result) *result = ok;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(rollback)

        TJS_BEGIN_NATIVE_PROP_DECL(errorCode)
        {
            TJS_BEGIN_NATIVE_PROP_GETTER
            {
                TJS_GET_NATIVE_INSTANCE(_this, NI_Sqlite);
                if (result) *result = static_cast<tjs_int>(_this->state()->error_code());
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(errorCode)
        TJS_BEGIN_NATIVE_PROP_DECL(errorMessage)
        {
            TJS_BEGIN_NATIVE_PROP_GETTER
            {
                TJS_GET_NATIVE_INSTANCE(_this, NI_Sqlite);
                const auto message = _this->state()->error_message();
                if (result) *result = utf8_to_tjs(message.data(), message.size());
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(errorMessage)
        TJS_BEGIN_NATIVE_PROP_DECL(lastInsertRowId)
        {
            TJS_BEGIN_NATIVE_PROP_GETTER
            {
                TJS_GET_NATIVE_INSTANCE(_this, NI_Sqlite);
                if (result) *result = static_cast<tjs_int64>(
                    _this->state()->handle
                        ? sqlite3_last_insert_rowid(_this->state()->handle) : 0);
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(lastInsertRowId)
    TJS_END_NATIVE_MEMBERS

    set_constant(classobj, TJS_W("SQLITE_OK"), SQLITE_OK);
    set_constant(classobj, TJS_W("SQLITE_ERROR"), SQLITE_ERROR);
    set_constant(classobj, TJS_W("SQLITE_INTERNAL"), SQLITE_INTERNAL);
    set_constant(classobj, TJS_W("SQLITE_PERM"), SQLITE_PERM);
    set_constant(classobj, TJS_W("SQLITE_ABORT"), SQLITE_ABORT);
    set_constant(classobj, TJS_W("SQLITE_BUSY"), SQLITE_BUSY);
    set_constant(classobj, TJS_W("SQLITE_LOCKED"), SQLITE_LOCKED);
    set_constant(classobj, TJS_W("SQLITE_NOMEM"), SQLITE_NOMEM);
    set_constant(classobj, TJS_W("SQLITE_READONLY"), SQLITE_READONLY);
    set_constant(classobj, TJS_W("SQLITE_INTERRUPT"), SQLITE_INTERRUPT);
    set_constant(classobj, TJS_W("SQLITE_IOERR"), SQLITE_IOERR);
    set_constant(classobj, TJS_W("SQLITE_CORRUPT"), SQLITE_CORRUPT);
    set_constant(classobj, TJS_W("SQLITE_NOTFOUND"), SQLITE_NOTFOUND);
    set_constant(classobj, TJS_W("SQLITE_FULL"), SQLITE_FULL);
    set_constant(classobj, TJS_W("SQLITE_CANTOPEN"), SQLITE_CANTOPEN);
    set_constant(classobj, TJS_W("SQLITE_PROTOCOL"), SQLITE_PROTOCOL);
    set_constant(classobj, TJS_W("SQLITE_EMPTY"), SQLITE_EMPTY);
    set_constant(classobj, TJS_W("SQLITE_SCHEMA"), SQLITE_SCHEMA);
    set_constant(classobj, TJS_W("SQLITE_TOOBIG"), SQLITE_TOOBIG);
    set_constant(classobj, TJS_W("SQLITE_CONSTRAINT"), SQLITE_CONSTRAINT);
    set_constant(classobj, TJS_W("SQLITE_MISMATCH"), SQLITE_MISMATCH);
    set_constant(classobj, TJS_W("SQLITE_MISUSE"), SQLITE_MISUSE);
    set_constant(classobj, TJS_W("SQLITE_NOLFS"), SQLITE_NOLFS);
    set_constant(classobj, TJS_W("SQLITE_AUTH"), SQLITE_AUTH);
    set_constant(classobj, TJS_W("SQLITE_FORMAT"), SQLITE_FORMAT);
    set_constant(classobj, TJS_W("SQLITE_RANGE"), SQLITE_RANGE);
    set_constant(classobj, TJS_W("SQLITE_NOTADB"), SQLITE_NOTADB);
    set_constant(classobj, TJS_W("SQLITE_ROW"), SQLITE_ROW);
    set_constant(classobj, TJS_W("SQLITE_DONE"), SQLITE_DONE);
    return classobj;
}
#undef TJS_NATIVE_CLASSID_NAME
#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID

#define TJS_NATIVE_CLASSID_NAME ClassID_SqliteStatement
iTJSDispatch2* create_statement_class() {
    tTJSNativeClassForPlugin* classobj = TJSCreateNativeClassForPlugin(
        TJS_W("SqliteStatement"), create_statement_instance);
#define TJS_NCM_REG_THIS classobj
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
    TJS_BEGIN_NATIVE_MEMBERS(SqliteStatement)
        TJS_DECL_EMPTY_FINALIZE_METHOD
        TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, NI_SqliteStatement, SqliteStatement)
        {
            tTJSVariant missing(TJS_W("missing"));
            objthis->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &missing);
            return TJS_S_OK;
        }
        TJS_END_NATIVE_CONSTRUCTOR_DECL(SqliteStatement)
        TJS_BEGIN_NATIVE_METHOD_DECL(open)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            if (numparams < 1) return TJS_E_BADPARAMCOUNT;
            const int status = _this->reopen(
                ttstr(*param[0]), numparams >= 2 ? param[1] : nullptr);
            if (result) *result = static_cast<tjs_int>(status);
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(open)
        TJS_BEGIN_NATIVE_METHOD_DECL(close)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            const int status = _this->close();
            if (result) *result = static_cast<tjs_int>(status);
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(close)
        TJS_BEGIN_NATIVE_METHOD_DECL(reset)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            const int status = _this->reset();
            if (result) *result = static_cast<tjs_int>(status);
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(reset)
        TJS_BEGIN_NATIVE_METHOD_DECL(bind)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            if (numparams < 1) return TJS_E_BADPARAMCOUNT;
            const int status = _this->bind(*param[0]);
            if (result) *result = static_cast<tjs_int>(status);
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(bind)
        TJS_BEGIN_NATIVE_METHOD_DECL(bindAt)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            if (numparams < 1) return TJS_E_BADPARAMCOUNT;
            const int status = numparams >= 2
                ? _this->bind_at(*param[0], param[1])
                : _this->bind_next(*param[0]);
            if (result) *result = static_cast<tjs_int>(status);
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(bindAt)
        TJS_BEGIN_NATIVE_METHOD_DECL(exec)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            if (result) *result = static_cast<tjs_int>(_this->exec());
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(exec)
        TJS_BEGIN_NATIVE_METHOD_DECL(step)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            if (result) *result = _this->step();
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(step)
        TJS_BEGIN_NATIVE_METHOD_DECL(get)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            _this->get(numparams, param, result);
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(get)
        TJS_BEGIN_NATIVE_METHOD_DECL(getName)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            if (numparams < 1) return TJS_E_BADPARAMCOUNT;
            const int column = _this->column_number(*param[0]);
            if (result && _this->raw() && column >= 0 && column < _this->column_count()) {
                const char* name = sqlite3_column_name(_this->raw(), column);
                if (name) *result = utf8_to_tjs(name, std::strlen(name));
            }
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(getName)
        TJS_BEGIN_NATIVE_METHOD_DECL(getType)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            if (numparams < 1) return TJS_E_BADPARAMCOUNT;
            const int column = _this->column_number(*param[0]);
            if (result && _this->raw() && column >= 0 && column < _this->column_count())
                *result = static_cast<tjs_int>(sqlite3_column_type(_this->raw(), column));
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(getType)
        TJS_BEGIN_NATIVE_METHOD_DECL(isNull)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            if (numparams < 1) return TJS_E_BADPARAMCOUNT;
            const int column = _this->column_number(*param[0]);
            const bool value = !_this->raw() || column < 0 ||
                column >= _this->column_count() ||
                sqlite3_column_type(_this->raw(), column) == SQLITE_NULL;
            if (result) *result = value;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(isNull)
        TJS_BEGIN_NATIVE_METHOD_DECL(missing)
        {
            TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
            if (numparams < 3) return TJS_E_BADPARAMCOUNT;
            bool handled = false;
            if (param[0]->AsInteger() == 0 && _this->raw()) {
                const int column = _this->column_number(*param[1]);
                if (column >= 0 && column < _this->column_count()) {
                    tTJSVariant value;
                    set_sqlite_result(_this->raw(), column, value);
                    const auto destination = param[2]->AsObjectClosureNoAddRef();
                    if (TJS_SUCCEEDED(destination.PropSet(
                            0, nullptr, nullptr, &value, nullptr)))
                        handled = true;
                }
            }
            if (result) *result = handled;
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(missing)
        TJS_BEGIN_NATIVE_PROP_DECL(columnCount)
        {
            TJS_BEGIN_NATIVE_PROP_GETTER
            {
                TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
                if (result) *result = static_cast<tjs_int>(_this->column_count());
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(columnCount)
        TJS_BEGIN_NATIVE_PROP_DECL(count)
        {
            TJS_BEGIN_NATIVE_PROP_GETTER
            {
                TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
                if (result) *result = static_cast<tjs_int>(_this->data_count());
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(count)
        TJS_BEGIN_NATIVE_PROP_DECL(sql)
        {
            TJS_BEGIN_NATIVE_PROP_GETTER
            {
                TJS_GET_NATIVE_INSTANCE(_this, NI_SqliteStatement);
                if (result) *result = _this->sql();
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(sql)
    TJS_END_NATIVE_MEMBERS
    set_constant(classobj, TJS_W("SQLITE_INTEGER"), SQLITE_INTEGER);
    set_constant(classobj, TJS_W("SQLITE_FLOAT"), SQLITE_FLOAT);
    set_constant(classobj, TJS_W("SQLITE_TEXT"), SQLITE_TEXT);
    set_constant(classobj, TJS_W("SQLITE_BLOB"), SQLITE_BLOB);
    set_constant(classobj, TJS_W("SQLITE_NULL"), SQLITE_NULL);
    return classobj;
}
#undef TJS_NATIVE_CLASSID_NAME
#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID

void add_global_member(iTJSDispatch2* global, const tjs_char* name,
                       iTJSDispatch2* member) {
    tTJSVariant value(member);
    member->Release();
    if (TJS_FAILED(global->PropSet(TJS_MEMBERENSURE, name, nullptr,
                                   &value, global)))
        TVPThrowExceptionMessage(TJS_W("Cannot register SQLite class"));
}

void register_sqlite() {
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (!global) TVPThrowExceptionMessage(TJS_W("No script engine for SQLite"));
    add_global_member(global, TJS_W("Sqlite"), create_sqlite_class());
    add_global_member(global, TJS_W("SqliteStatement"), create_statement_class());
    global->Release();
}

void mark_sqlite_ready() {
    krkrvita_boot_trace("retail-sqlite3-ready");
}

} // namespace

NCB_PRE_REGIST_CALLBACK(register_sqlite);
NCB_POST_REGIST_CALLBACK(mark_sqlite_ready);

#include "tjs.h"
#include "StorageIntf.h"
#include "TextStream.h"
#include "ncbind/ncbind.hpp"

#include <codecvt>
#include <cstdlib>
#include <iostream>
#include <locale>
#include <memory>
#include <string>

namespace {

TJS::tTJS* script_engine = nullptr;

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void check(bool condition, const char* message) {
    if (!condition) fail(message);
}

std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>&
utf8_converter() {
    static std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>
        converter;
    return converter;
}

} // namespace

tjs_char TVPArchiveDelimiter = TJS_W('>');

void TVPThrowExceptionMessage(const tjs_char* message) {
    throw eTJSError(message);
}

iTJSDispatch2* TVPGetScriptDispatch() {
    return script_engine ? script_engine->GetGlobal() : nullptr;
}

bool TVPStringDecode(const void* input, int size, ttstr& output, ttstr) {
    if (!input || size < 0) return false;
    try {
        const auto* bytes = static_cast<const char*>(input);
        const auto decoded = utf8_converter().from_bytes(bytes, bytes + size);
        output = ttstr(reinterpret_cast<const tjs_char*>(decoded.c_str()),
                       static_cast<tjs_int>(decoded.size()));
        return true;
    } catch (...) {
        return false;
    }
}

bool TVPStringEncode(const ttstr& input, std::string& output, ttstr) {
    try {
        const auto* begin = reinterpret_cast<const char16_t*>(input.c_str());
        output = utf8_converter().to_bytes(begin, begin + input.GetLen());
        return true;
    } catch (...) {
        return false;
    }
}

ttstr TVPNormalizeStorageName(const ttstr& name) { return name; }
void TVPGetLocalName(ttstr&) {}
bool TVPIsExistentStorage(const ttstr&) { return false; }
tTJSBinaryStream* TVPCreateStream(const ttstr&, tjs_uint32) { return nullptr; }

extern "C" void krkrvita_boot_trace(const char*) {}
extern "C" void krkrvita_write_error(const char*) {}

int main() {
    static_assert(sizeof(tjs_char) == sizeof(char16_t));

    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        engine(new TJS::tTJS(), release_tjs);
    script_engine = engine.get();

    ncbAutoRegister::AllRegist();
    check(ncbAutoRegister::LoadModule(TJS_W("sqlite3.dll")),
          "sqlite3 module did not register through ncbind");

    engine->ExecScript(TJS::ttstr(R"TJS(
var db = new Sqlite(":memory:");
if(db.errorCode != Sqlite.SQLITE_OK) throw db.errorMessage;
if(!db.exec("create table sample(id integer primary key, Name text, value integer)"))
    throw db.errorMessage;

var insert = new SqliteStatement(db,
    "insert into sample(Name,value) values(?,?)");
insert.bind(["ＡＢＣ", 17]);
if(insert.exec() != Sqlite.SQLITE_DONE) throw db.errorMessage;

var selected = new SqliteStatement(db,
    "select Name as MixedCase,value from sample where id=?");
selected.bind([db.lastInsertRowId]);
if(!selected.step()) throw "missing inserted row";
var byName = selected.get("mixedcase");
var byMissing = selected.MixedCase;
var valueType = selected.getType("VALUE");
var valueName = selected.getName("value");
var valueNull = selected.isNull("value");

var persistent = new SqliteStatement(db, "select ?");
if(persistent.bindAt(37, 0) != Sqlite.SQLITE_OK) throw "zero-based bindAt failed";
if(!persistent.step()) throw "persistent first step failed";
var persistentFirst = persistent.get(0);
persistent.reset();
if(!persistent.step()) throw "reset discarded official persistent binding";
var persistentSecond = persistent.get(0);

var named = new SqliteStatement(db, "select :answer");
if(named.bindAt(42, ":answer") != Sqlite.SQLITE_OK) throw "named bindAt failed";
if(!named.step()) throw "named step failed";
var namedValue = named.get(0);

var delayed = new SqliteStatement(db);
if(delayed.open("select :answer", %[answer: 51]) != Sqlite.SQLITE_OK)
    throw "delayed open failed";
if(!delayed.step()) throw "delayed step failed";
var delayedValue = delayed.get(0);
delayed.close();
if(delayed.open("select 63") != Sqlite.SQLITE_OK || !delayed.step())
    throw "open after close lost database owner";
var reopenedValue = delayed.get(0);

var exactContains = db.execValue("select cnt('AbCd','bC')");
var normalizedContains = db.execValue("select ncnt('ＡＢＣ','abc')");
var normalizedVoicing = db.execValue("select ncnt('が','ｶ')");
var normalizedMiss = db.execValue("select ncnt('ＡＢＣ','abd')");

global.sqliteContract = [
    byName, byMissing, valueType, valueName, valueNull,
    persistentFirst, persistentSecond, namedValue, delayedValue, reopenedValue,
    exactContains, normalizedContains, normalizedVoicing, normalizedMiss,
    selected.columnCount, selected.count,
    SqliteStatement.SQLITE_INTEGER
];
)TJS"));

    TJS::tTJSVariant result;
    engine->EvalExpression(TJS_W("global.sqliteContract"), &result);
    check(result.Type() == TJS::tvtObject,
          "sqlite contract did not return an array");
    auto closure = result.AsObjectClosureNoAddRef();
    const auto get = [&](int index) {
        TJS::tTJSVariant value;
        check(TJS_SUCCEEDED(closure.PropGetByNum(
                  0, index, &value, closure.SelectObjectNoAddRef())),
              "cannot read sqlite contract result");
        return value;
    };

    check(ttstr(get(0)) == TJS_W("ＡＢＣ") &&
              ttstr(get(1)) == TJS_W("ＡＢＣ"),
          "case-insensitive column lookup or missing member differs");
    check(get(2).AsInteger() == 1 && ttstr(get(3)) == TJS_W("value") &&
              get(4).AsInteger() == 0,
          "column metadata contract differs");
    check(get(5).AsInteger() == 37 && get(6).AsInteger() == 37 &&
              get(7).AsInteger() == 42 && get(8).AsInteger() == 51 &&
              get(9).AsInteger() == 63,
          "statement open/reset/bind contract differs");
    check(get(10).AsInteger() == 1 && get(11).AsInteger() == 1 &&
              get(12).AsInteger() == 1 && get(13).AsInteger() == 0,
          "official cnt/ncnt normalization contract differs");
    check(get(14).AsInteger() == 2 && get(15).AsInteger() == 2 &&
              get(16).AsInteger() == 1,
          "statement metadata/constants contract differs");

    script_engine = nullptr;
    ncbAutoRegister::AllUnregist();
    std::cout << "sqlite module contract passed\n";
    return 0;
}

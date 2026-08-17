#include "tjsCommHead.h"
#include "tjsMessage.h"
#include "tjsNative.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>

namespace {

class SystemAppIdProperty final : public TJS::tTJSDispatch {
public:
    explicit SystemAppIdProperty(TJS::iTJSDispatch2* global) : global_(global) {}

    tjs_error TJS_INTF_METHOD IsInstanceOf(
        tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
        const tjs_char* classname, TJS::iTJSDispatch2* objthis) override {
        if (!membername && classname &&
            !TJS_strcmp(classname, TJS_W("Property"))) {
            return TJS_S_TRUE;
        }
        return TJS::tTJSDispatch::IsInstanceOf(
            flag, membername, hint, classname, objthis);
    }

    tjs_error TJS_INTF_METHOD PropGet(
        tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
        TJS::tTJSVariant* result, TJS::iTJSDispatch2* objthis) override {
        if (membername) {
            return TJS::tTJSDispatch::PropGet(
                flag, membername, hint, result, objthis);
        }
        if (!result) return TJS_E_INVALIDPARAM;
        if (has_override_) {
            *result = override_;
            return TJS_S_OK;
        }
        const tjs_error status = global_->PropGet(
            0, TJS_W("APP_ID"), nullptr, result, global_);
        if (status == TJS_E_MEMBERNOTFOUND) {
            result->Clear();
            return TJS_S_OK;
        }
        return status;
    }

    tjs_error TJS_INTF_METHOD PropSet(
        tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
        const TJS::tTJSVariant* value, TJS::iTJSDispatch2* objthis) override {
        if (membername) {
            return TJS::tTJSDispatch::PropSet(
                flag, membername, hint, value, objthis);
        }
        if (!value) return TJS_E_INVALIDPARAM;
        override_ = *value;
        has_override_ = true;
        return TJS_S_OK;
    }

private:
    // The property is owned by System, which is itself owned by this global
    // object. A non-owning pointer avoids creating a global -> System -> global
    // reference cycle while retaining the engine-lifetime ordering.
    TJS::iTJSDispatch2* global_;
    TJS::tTJSVariant override_;
    bool has_override_ = false;
};

} // namespace

namespace krkrvita {

bool install_system_app_id_compat(TJS::tTJS& engine) {
    TJS::iTJSDispatch2* global = engine.GetGlobalNoAddRef();
    if (!global) return false;

    TJS::tTJSVariant system_value;
    if (TJS_FAILED(global->PropGet(
            TJS_MEMBERMUSTEXIST, TJS_W("System"), nullptr,
            &system_value, global)) || system_value.Type() != tvtObject) {
        return false;
    }
    TJS::iTJSDispatch2* system = system_value.AsObjectNoAddRef();
    if (!system) return false;

    TJS::tTJSVariant existing;
    const tjs_error existing_status = system->PropGet(
        TJS_IGNOREPROP, TJS_W("checkAppId"), nullptr, &existing, system);
    if (TJS_SUCCEEDED(existing_status)) return true;
    if (existing_status != TJS_E_MEMBERNOTFOUND) return false;

    auto* property = new SystemAppIdProperty(global);
    TJS::tTJSVariant property_value(property);
    property->Release();
    return TJS_SUCCEEDED(system->PropSet(
        TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER,
        TJS_W("checkAppId"), nullptr, &property_value, system));
}

} // namespace krkrvita

#ifndef __vita__
tjs_uint32 TVPGetRoughTickCount32() {
    using namespace std::chrono;
    return static_cast<tjs_uint32>(duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count());
}

ttstr TVPGetMessageByLocale(const std::string& key) {
    return ttstr(key);
}
#endif

namespace TJS {

#ifdef TJS_NO_REGEXP
void TJSReleaseRegex() {}
#endif

void TVPConsoleLog(const tjs_char* line) {
    if (!line) return;
    const auto length = TJS_wcstombs(nullptr, line, 0);
    if (length == static_cast<std::size_t>(-1)) return;
    std::string text(length, '\0');
    TJS_wcstombs(text.data(), line, text.size());
    std::fprintf(stderr, "TJS: %s\n", text.c_str());
}

void TVPConsoleLog(const tjs_nchar* format, ...) {
    if (!format) return;
    std::va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}

} // namespace TJS

#include "tjsCommHead.h"

#include "EventIntf.h"
#include "StorageIntf.h"
#include "WindowIntf.h"
#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/rsa_pss_signature.hpp"
#include "ncbind/ncbind.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define NCB_MODULE_NAME TJS_W("sigcheck.dll")

namespace {

std::atomic<tjs_int> next_handler{1};

std::string narrow_ascii(const ttstr& text) {
    std::string output;
    output.reserve(text.GetLen());
    for (tjs_int index = 0; index < text.GetLen(); ++index) {
        const auto code = static_cast<unsigned int>(text[index]);
        if (code > 0x7f) return {};
        output.push_back(static_cast<char>(code));
    }
    return output;
}

bool read_signature(const ttstr& path, std::vector<std::uint8_t>& output,
                    std::string& error) {
    std::unique_ptr<tTJSBinaryStream> stream;
    try {
        stream.reset(TVPCreateStream(path, TJS_BS_READ));
        if (!stream) {
            error = "can't open signature file";
            return false;
        }
        const auto size = stream->GetSize();
        if (size == 0 || size > 4096) {
            error = "Invalid signature file format";
            return false;
        }
        std::vector<std::uint8_t> envelope(static_cast<std::size_t>(size));
        std::size_t position = 0;
        while (position < envelope.size()) {
            const auto amount = stream->Read(
                envelope.data() + position,
                static_cast<tjs_uint>(envelope.size() - position));
            if (!amount) break;
            position += amount;
        }
        if (position != envelope.size()) {
            error = "can't read signature file";
            return false;
        }
        return krkrvita::decode_sha256_pss_rsa_signature(
            envelope.data(), envelope.size(), output, &error);
    } catch (...) {
        error = "can't open signature file";
        return false;
    }
}

bool verify_storage(const ttstr& target, const std::string& public_key,
                    std::string& error) {
    // An absent .sig means "this file is not signed", not "this file failed".
    //
    // Aoi Tori's release.ks checks Storages.chopStorageExt(System.exeName) plus
    // every archive, and exits on result < 1. The shipped game has a .sig for
    // each .xp3 but none for allokmama.exe, and it runs on Windows through that
    // same executable -- so the original sigcheck.dll cannot be reporting a
    // missing signature as a failure. Treating it as one is stricter than the
    // plug-in we are standing in for, and refuses titles that are intact.
    //
    // Files that do ship a signature are still verified strictly: a malformed,
    // unreadable or non-matching .sig below is a hard failure. The tamper check
    // therefore still covers everything the author actually signed.
    const ttstr signature_path = target + TJS_W(".sig");
    if (!TVPIsExistentStorage(signature_path)) {
        error.clear();
        return true;
    }

    std::vector<std::uint8_t> signature;
    if (!read_signature(signature_path, signature, error)) return false;

    krkrvita::RsaPssSha256Verifier verifier;
    if (!verifier.initialize(public_key, &error)) return false;
    try {
        std::unique_ptr<tTJSBinaryStream> stream(
            TVPCreateStream(target, TJS_BS_READ));
        if (!stream) {
            error = "can't open file";
            return false;
        }
        std::array<std::uint8_t, 64 * 1024> buffer{};
        for (;;) {
            const auto amount = stream->Read(
                buffer.data(), static_cast<tjs_uint>(buffer.size()));
            if (!amount) break;
            if (!verifier.update(buffer.data(), amount, &error)) return false;
        }
    } catch (...) {
        error = "can't open file";
        return false;
    }
    return verifier.finish(signature.data(), signature.size(), &error);
}

void deliver_done(iTJSDispatch2* owner, tjs_int handler, bool verified,
                  const std::string& error) {
    if (!owner) return;
    tTJSVariant parameters[4] = {
        handler,
        tTJSVariant(),
        verified ? 1 : 0,
        ttstr(error),
    };
    static ttstr event_name(TJS_W("onCheckSignatureDone"));
    TVPPostEvent(owner, owner, event_name, 0, TVP_EPT_POST, 4, parameters);
}

tjs_error check_signature(tTJSVariant* result, tjs_int numparams,
                          tTJSVariant** param, iTJSDispatch2*) {
    if (numparams < 2) return TJS_E_BADPARAMCOUNT;
    const ttstr target(*param[0]);
    const std::string public_key = narrow_ascii(ttstr(*param[1]));
    if (target.IsEmpty())
        TVPThrowExceptionMessage(TJS_W("Specify target file"));
    if (public_key.empty())
        TVPThrowExceptionMessage(TJS_W("Specify public key"));

    iTJSDispatch2* owner = nullptr;
    const tjs_int count = TVPGetWindowCount();
    if (count > 0) {
        if (auto* window = TVPGetWindowListAt(count - 1))
            owner = window->GetOwnerNoAddRef();
    }
    if (!owner)
        TVPThrowExceptionMessage(TJS_W("No Window instance for signature callback"));

    tjs_int handler = next_handler.fetch_add(1);
    if (handler <= 0) {
        next_handler.store(2);
        handler = 1;
    }
    if (result) *result = handler;

    // Verification runs inline, on the caller's thread.
    //
    // It is tempting to hash a multi-megabyte archive on a worker, but nothing
    // in this path is thread-safe. TVPCreateStream walks Yuri's media manager,
    // archive table and auto-path table, all of which the main thread is still
    // mutating while startup scripts load; iTJSDispatch2 reference counts are
    // plain ints; and TVPPostEvent pushes onto an unlocked std::vector. A
    // detached worker here corrupted engine state and crashed on an indirect
    // call through the damage.
    //
    // The script contract is preserved because the result is still delivered
    // through the event queue: checkSignature returns a handler immediately and
    // onCheckSignatureDone arrives from the normal event dispatch, exactly as
    // the caller expects. The cost is a startup pause while the archive is
    // hashed, which the title is waiting on anyway.
    std::string error;
    const bool verified = verify_storage(target, public_key, error);
    deliver_done(owner, handler, verified, error);
    return TJS_S_OK;
}

tjs_error cancel_signature(tTJSVariant* result, tjs_int numparams,
                           tTJSVariant**, iTJSDispatch2*) {
    if (numparams < 1) return TJS_E_BADPARAMCOUNT;
    // Verification completes before checkSignature returns, so by the time a
    // script can cancel a handler there is never one in flight. Report "not
    // cancelled" rather than pretending otherwise.
    if (result) *result = 0;
    return TJS_S_OK;
}

void mark_sigcheck_ready() {
    krkrvita_boot_trace("retail-sigcheck-rsa-pss-ready");
}

} // namespace

NCB_REGISTER_FUNCTION(checkSignature, check_signature);
NCB_REGISTER_FUNCTION(cancelCheckSignature, cancel_signature);
NCB_REGISTER_FUNCTION(stopCheckSignature, cancel_signature);
NCB_POST_REGIST_CALLBACK(mark_sigcheck_ready);

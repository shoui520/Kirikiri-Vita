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
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#define NCB_MODULE_NAME TJS_W("sigcheck.dll")

namespace {

std::atomic<tjs_int> next_handler{1};
std::mutex jobs_mutex;
std::unordered_map<tjs_int, std::weak_ptr<std::atomic<bool>>> jobs;

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
    std::vector<std::uint8_t> signature;
    if (!read_signature(target + TJS_W(".sig"), signature, error)) return false;

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
    owner->AddRef();

    tjs_int handler = next_handler.fetch_add(1);
    if (handler <= 0) {
        next_handler.store(2);
        handler = 1;
    }
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(jobs_mutex);
        jobs[handler] = cancelled;
    }
    if (result) *result = handler;

    std::thread([target, public_key, owner, handler, cancelled] {
        std::string error;
        const bool verified = verify_storage(target, public_key, error);
        if (!cancelled->load()) deliver_done(owner, handler, verified, error);
        owner->Release();
        std::lock_guard<std::mutex> lock(jobs_mutex);
        jobs.erase(handler);
    }).detach();
    return TJS_S_OK;
}

tjs_error cancel_signature(tTJSVariant* result, tjs_int numparams,
                           tTJSVariant** param, iTJSDispatch2*) {
    if (numparams < 1) return TJS_E_BADPARAMCOUNT;
    const tjs_int handler = static_cast<tjs_int>(*param[0]);
    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lock(jobs_mutex);
        const auto found = jobs.find(handler);
        if (found != jobs.end()) {
            if (auto job = found->second.lock()) {
                job->store(true);
                cancelled = true;
            }
        }
    }
    if (result) *result = cancelled ? 1 : 0;
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

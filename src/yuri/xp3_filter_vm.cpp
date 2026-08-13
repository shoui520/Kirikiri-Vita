#include "krkrvita/xp3_filter_vm.hpp"

#include "tjsCommHead.h"
#include "tjsObject.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <thread>

namespace krkrvita {
namespace {

class BinaryAccessor final : public TJS::tTJSDispatch {
public:
    BinaryAccessor(std::uint8_t* bytes, std::size_t size)
        : bytes_(bytes), size_(size) {}

    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char* member,
        tjs_uint32*, TJS::tTJSVariant* result, tjs_int count,
        TJS::tTJSVariant** parameters, TJS::iTJSDispatch2*) override {
        if (result) result->Clear();
        if (!member || count < 3) return TJS_E_BADPARAMCOUNT;
        const auto offset = parameters[0]->AsInteger();
        const auto length = parameters[1]->AsInteger();
        const auto value = static_cast<std::uint8_t>(parameters[2]->AsInteger());
        if (offset < 0 || length < 0 || cursor_ > size_ ||
            static_cast<std::size_t>(offset) > size_ - cursor_ ||
            static_cast<std::size_t>(length) > size_ - cursor_ - offset) {
            return TJS_E_INVALIDPARAM;
        }
        auto* begin = bytes_ + cursor_ + offset;
        if (TJS_strcmp(member, TJS_W("xor")) == 0) {
            for (tjs_int i = 0; i < length; ++i) begin[i] ^= value;
            return TJS_S_OK;
        }
        if (TJS_strcmp(member, TJS_W("add")) == 0) {
            for (tjs_int i = 0; i < length; ++i) begin[i] += value;
            return TJS_S_OK;
        }
        return TJS_E_MEMBERNOTFOUND;
    }

    tjs_error TJS_INTF_METHOD PropGet(tjs_uint32, const tjs_char* member,
        tjs_uint32*, TJS::tTJSVariant* result, TJS::iTJSDispatch2*) override {
        if (!member || !result) return TJS_E_MEMBERNOTFOUND;
        if (TJS_strcmp(member, TJS_W("count")) == 0) {
            *result = static_cast<tjs_int64>(size_);
            return TJS_S_OK;
        }
        if (TJS_strcmp(member, TJS_W("ptr")) == 0) {
            *result = static_cast<tjs_int64>(cursor_);
            return TJS_S_OK;
        }
        return TJS_E_MEMBERNOTFOUND;
    }

    tjs_error TJS_INTF_METHOD PropGetByNum(tjs_uint32, tjs_int index,
        TJS::tTJSVariant* result, TJS::iTJSDispatch2*) override {
        if (!result || !inside(index)) return TJS_E_MEMBERNOTFOUND;
        *result = static_cast<tjs_int32>(bytes_[cursor_ + index]);
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD PropSet(tjs_uint32, const tjs_char* member,
        tjs_uint32*, const TJS::tTJSVariant* value, TJS::iTJSDispatch2*) override {
        if (!member || TJS_strcmp(member, TJS_W("ptr")) != 0) return TJS_E_MEMBERNOTFOUND;
        const auto position = value->AsInteger();
        if (position < 0 || static_cast<std::size_t>(position) > size_) return TJS_E_INVALIDPARAM;
        cursor_ = static_cast<std::size_t>(position);
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD PropSetByNum(tjs_uint32, tjs_int index,
        const TJS::tTJSVariant* value, TJS::iTJSDispatch2*) override {
        if (!inside(index)) return TJS_E_MEMBERNOTFOUND;
        bytes_[cursor_ + index] = static_cast<std::uint8_t>(value->AsInteger());
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD GetCount(tjs_int* result, const tjs_char* member,
        tjs_uint32*, TJS::iTJSDispatch2*) override {
        if (member || !result) return TJS_E_MEMBERNOTFOUND;
        *result = static_cast<tjs_int>(size_);
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD Invalidate(tjs_uint32, const tjs_char*,
        tjs_uint32*, TJS::iTJSDispatch2*) override {
        bytes_ = nullptr;
        size_ = cursor_ = 0;
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD IsValid(tjs_uint32, const tjs_char*,
        tjs_uint32*, TJS::iTJSDispatch2*) override {
        return bytes_ ? TJS_S_TRUE : TJS_S_FALSE;
    }

    tjs_error TJS_INTF_METHOD Operation(tjs_uint32 flag, const tjs_char* member,
        tjs_uint32*, TJS::tTJSVariant*, const TJS::tTJSVariant* value,
        TJS::iTJSDispatch2*) override {
        if (!member || TJS_strcmp(member, TJS_W("ptr")) != 0) return TJS_E_MEMBERNOTFOUND;
        auto amount = static_cast<tjs_int64>(1);
        if (value) amount = value->AsInteger();
        auto next = static_cast<tjs_int64>(cursor_);
        switch (flag & TJS_OP_MASK) {
        case TJS_OP_ADD: next += amount; break;
        case TJS_OP_SUB: next -= amount; break;
        case TJS_OP_INC: ++next; break;
        case TJS_OP_DEC: --next; break;
        default: return TJS_E_NOTIMPL;
        }
        if (next < 0 || static_cast<std::size_t>(next) > size_) return TJS_E_INVALIDPARAM;
        cursor_ = static_cast<std::size_t>(next);
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD OperationByNum(tjs_uint32 flag, tjs_int index,
        TJS::tTJSVariant*, const TJS::tTJSVariant* value,
        TJS::iTJSDispatch2*) override {
        if (!inside(index) || !value) return TJS_E_MEMBERNOTFOUND;
        auto& target = bytes_[cursor_ + index];
        const auto operand = static_cast<std::uint8_t>(value->AsInteger());
        switch (flag & TJS_OP_MASK) {
        case TJS_OP_BAND: target &= operand; break;
        case TJS_OP_BOR: target |= operand; break;
        case TJS_OP_BXOR: target ^= operand; break;
        case TJS_OP_SUB: target -= operand; break;
        case TJS_OP_ADD: target += operand; break;
        case TJS_OP_MOD: if (!operand) return TJS_E_INVALIDPARAM; target %= operand; break;
        case TJS_OP_DIV:
        case TJS_OP_IDIV: if (!operand) return TJS_E_INVALIDPARAM; target /= operand; break;
        case TJS_OP_MUL: target *= operand; break;
        case TJS_OP_SAR:
        case TJS_OP_SR: target >>= operand; break;
        case TJS_OP_SAL: target <<= operand; break;
        case TJS_OP_INC: ++target; break;
        case TJS_OP_DEC: --target; break;
        default: return TJS_E_NOTIMPL;
        }
        return TJS_S_OK;
    }

private:
    bool inside(tjs_int index) const {
        return bytes_ && index >= 0 && cursor_ <= size_ &&
               static_cast<std::size_t>(index) < size_ - cursor_;
    }

    std::uint8_t* bytes_ = nullptr;
    std::size_t size_ = 0;
    std::size_t cursor_ = 0;
};

class StorageDispatch final : public TJS::tTJSDispatch {
public:
    ~StorageDispatch() override {
        if (filter_.Object) filter_.Release();
    }

    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag, const tjs_char* member,
        tjs_uint32* hint, TJS::tTJSVariant* result, tjs_int count,
        TJS::tTJSVariant** parameters, TJS::iTJSDispatch2* object) override {
        if (!member || TJS_strcmp(member, TJS_W("setXP3ArchiveExtractionFilter")) != 0) {
            return TJS::tTJSDispatch::FuncCall(flag, member, hint, result, count,
                                               parameters, object);
        }
        if (count < 1) return TJS_E_BADPARAMCOUNT;
        if (result) result->Clear();
        if (filter_.Object) filter_.Release();
        filter_ = parameters[0]->AsObjectClosure();
        return TJS_S_OK;
    }

    bool active() const { return filter_.Object != nullptr; }

    bool decode(std::uint32_t hash, std::uint64_t offset,
                std::span<std::uint8_t> bytes, std::string_view filename) {
        if (!filter_.Object) return false;
        TJS::tTJSVariant hash_value(static_cast<tjs_int64>(hash));
        TJS::tTJSVariant offset_value(static_cast<tjs_int64>(offset));
        auto* accessor = new BinaryAccessor(bytes.data(), bytes.size());
        TJS::tTJSVariant buffer_value(accessor);
        accessor->Release();
        TJS::tTJSVariant length_value(static_cast<tjs_int64>(bytes.size()));
        TJS::tTJSVariant filename_value{std::string(filename)};
        TJS::tTJSVariant context_value;
        TJS::tTJSVariant* parameters[] = {
            &hash_value, &offset_value, &buffer_value, &length_value,
            &filename_value, &context_value,
        };
        return TJS_SUCCEEDED(filter_.FuncCall(0, nullptr, nullptr, nullptr,
            static_cast<tjs_int>(std::size(parameters)), parameters, nullptr));
    }

private:
    TJS::tTJSVariantClosure filter_{nullptr};
};

struct Decoder {
    explicit Decoder(std::string_view script) {
        engine = new TJS::tTJS();
        storage = new StorageDispatch();
        TJS::tTJSVariant value(storage);
        storage->Release();
        const auto result = engine->GetGlobalNoAddRef()->PropSet(
            TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_W("Storages"), nullptr,
            &value, engine->GetGlobalNoAddRef());
        if (TJS_FAILED(result)) throw std::runtime_error("cannot register Storages object");
        engine->ExecScript(ttstr(std::string(script)));
        if (!storage->active()) {
            throw std::runtime_error("script did not register an XP3 extraction filter");
        }
    }

    ~Decoder() {
        if (engine) engine->Release();
    }

    TJS::tTJS* engine = nullptr;
    StorageDispatch* storage = nullptr;
};

} // namespace

struct Xp3FilterVm::Impl {
    std::string script;
    mutable std::mutex mutex;
    std::map<std::thread::id, std::unique_ptr<Decoder>> decoders;

    Decoder& decoder() {
        std::lock_guard lock(mutex);
        auto& value = decoders[std::this_thread::get_id()];
        if (!value) value = std::make_unique<Decoder>(script);
        return *value;
    }
};

Xp3FilterVm::Xp3FilterVm() : impl_(std::make_unique<Impl>()) {}
Xp3FilterVm::~Xp3FilterVm() = default;
Xp3FilterVm::Xp3FilterVm(Xp3FilterVm&&) noexcept = default;
Xp3FilterVm& Xp3FilterVm::operator=(Xp3FilterVm&&) noexcept = default;

bool Xp3FilterVm::load(std::string script, std::string* error) {
    try {
        if (script.empty()) throw std::runtime_error("empty xp3filter script");
        auto trial = std::make_unique<Decoder>(script);
        std::lock_guard lock(impl_->mutex);
        impl_->script = std::move(script);
        impl_->decoders.clear();
        impl_->decoders.emplace(std::this_thread::get_id(), std::move(trial));
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    } catch (...) {
        if (error) *error = "TJS rejected xp3filter script";
        return false;
    }
}

bool Xp3FilterVm::active() const {
    std::lock_guard lock(impl_->mutex);
    return !impl_->script.empty();
}

bool Xp3FilterVm::decode(std::uint32_t file_hash, std::uint64_t offset,
                         std::span<std::uint8_t> bytes, std::string_view filename,
                         std::string* error) {
    try {
        if (!active()) throw std::runtime_error("xp3filter VM is not loaded");
        if (!impl_->decoder().storage->decode(file_hash, offset, bytes, filename)) {
            throw std::runtime_error("xp3filter callback failed");
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    } catch (...) {
        if (error) *error = "xp3filter callback threw a TJS exception";
        return false;
    }
}

} // namespace krkrvita

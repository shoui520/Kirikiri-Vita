#include "tjsCommHead.h"

#include "StorageIntf.h"
#include "TextStream.h"
#include "krkrvita/psb.hpp"
#include "krkrvita/retail_bootstrap.hpp"
#include "ncbind/ncbind.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <memory>
#include <limits>
#include <string>
#include <vector>

#define NCB_MODULE_NAME TJS_W("psbfile.dll")

namespace {

ttstr utf8_to_tjs(const std::string& text) {
    if (text.size() > static_cast<std::size_t>(INT_MAX))
        TVPThrowExceptionMessage(TJS_W("PSB string is too large"));
    ttstr output;
    if (!TVPStringDecode(text.data(), static_cast<int>(text.size()), output,
                         TJS_W("utf8"))) {
        TVPThrowExceptionMessage(TJS_W("PSB contains invalid UTF-8"));
    }
    return output;
}

void value_to_tjs(const krkrvita::PsbValuePtr& source, tTJSVariant& output) {
    if (!source) TVPThrowExceptionMessage(TJS_W("PSB contains a null node"));
    switch (source->type) {
    case krkrvita::PsbValue::Type::null_value:
        output.Clear();
        return;
    case krkrvita::PsbValue::Type::boolean:
        output = source->boolean;
        return;
    case krkrvita::PsbValue::Type::integer:
        output = static_cast<tjs_int64>(source->integer);
        return;
    case krkrvita::PsbValue::Type::real:
        output = static_cast<tjs_real>(source->real);
        return;
    case krkrvita::PsbValue::Type::string:
        output = utf8_to_tjs(source->string);
        return;
    case krkrvita::PsbValue::Type::binary: {
        if (!source->binary ||
            source->binary->size() >
                static_cast<std::size_t>(std::numeric_limits<tjs_uint>::max())) {
            TVPThrowExceptionMessage(TJS_W("PSB binary resource is too large"));
        }
        const auto* data = source->binary->empty()
            ? nullptr : source->binary->data();
        output = tTJSVariant(data,
            static_cast<tjs_uint>(source->binary->size()));
        return;
    }
    case krkrvita::PsbValue::Type::array: {
        iTJSDispatch2* array = TJSCreateArrayObject();
        try {
            for (std::size_t index = 0; index < source->array.size(); ++index) {
                if (index > static_cast<std::size_t>(
                                std::numeric_limits<tjs_int>::max())) {
                    TVPThrowExceptionMessage(TJS_W("PSB array is too large"));
                }
                tTJSVariant item;
                value_to_tjs(source->array[index], item);
                if (TJS_FAILED(array->PropSetByNum(
                        TJS_MEMBERENSURE, static_cast<tjs_int>(index),
                        &item, array))) {
                    TVPThrowExceptionMessage(TJS_W("Cannot create PSB array"));
                }
            }
            output.SetObject(array, array);
            array->Release();
        } catch (...) {
            array->Release();
            throw;
        }
        return;
    }
    case krkrvita::PsbValue::Type::object: {
        iTJSDispatch2* object = TJSCreateDictionaryObject();
        try {
            for (const auto& member : source->object) {
                const auto name = utf8_to_tjs(member.first);
                tTJSVariant item;
                value_to_tjs(member.second, item);
                if (TJS_FAILED(object->PropSet(TJS_MEMBERENSURE,
                                               name.c_str(), nullptr,
                                               &item, object))) {
                    TVPThrowExceptionMessage(TJS_W("Cannot create PSB object"));
                }
            }
            output.SetObject(object, object);
            object->Release();
        } catch (...) {
            object->Release();
            throw;
        }
        return;
    }
    }
    TVPThrowExceptionMessage(TJS_W("Unknown PSB value type"));
}

std::vector<std::uint8_t> read_storage(const ttstr& path) {
    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateStream(path, TJS_BS_READ));
    if (!stream) TVPThrowExceptionMessage(TJS_W("Cannot open PSB storage"));
    const auto length = stream->GetSize();
    if (length == 0 || length > 256u * 1024u * 1024u ||
        length > static_cast<tjs_uint64>(
            std::numeric_limits<std::size_t>::max())) {
        TVPThrowExceptionMessage(TJS_W("Invalid PSB storage size"));
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto request = static_cast<tjs_uint>(std::min<std::size_t>(
            bytes.size() - offset, 64u * 1024u));
        const auto amount = stream->Read(bytes.data() + offset, request);
        if (!amount) break;
        offset += amount;
    }
    if (offset != bytes.size())
        TVPThrowExceptionMessage(TJS_W("Cannot read complete PSB storage"));
    return bytes;
}

class NI_PSBFile final : public tTJSNativeInstance {
public:
    tjs_error TJS_INTF_METHOD Construct(tjs_int numparams,
                                        tTJSVariant** param,
                                        iTJSDispatch2*) override {
        if (numparams < 1) return TJS_E_BADPARAMCOUNT;
        const std::uint8_t* data = nullptr;
        std::size_t size = 0;
        std::vector<std::uint8_t> storage;
        if (param[0]->Type() == tvtOctet) {
            const auto* octet = param[0]->AsOctetNoAddRef();
            if (!octet) TVPThrowExceptionMessage(TJS_W("Empty PSB octet"));
            data = octet->GetData();
            size = octet->GetLength();
        } else if (param[0]->Type() == tvtString) {
            storage = read_storage(ttstr(*param[0]));
            data = storage.data();
            size = storage.size();
        } else {
            TVPThrowExceptionMessage(TJS_W("PSBFile requires a storage or octet"));
        }
        krkrvita::PsbDocument document;
        std::string error;
        if (!krkrvita::parse_psb(data, size, document, &error)) {
            const auto message = utf8_to_tjs("Cannot parse PSB: " + error);
            TVPThrowExceptionMessage(message.c_str());
        }
        value_to_tjs(document.root, root_);
        return TJS_S_OK;
    }

    const tTJSVariant& root() const { return root_; }

private:
    tTJSVariant root_;
};

iTJSNativeInstance* TJS_INTF_METHOD create_psb_instance() {
    return new NI_PSBFile();
}

#ifdef TJS_NATIVE_CLASSID_NAME
#undef TJS_NATIVE_CLASSID_NAME
#undef TJS_NCM_REG_THIS
#undef TJS_NATIVE_SET_ClassID
#endif
#define TJS_NATIVE_CLASSID_NAME ClassID_PSBFile
static tjs_int32 TJS_NATIVE_CLASSID_NAME = -1;

iTJSDispatch2* create_psb_class() {
    tTJSNativeClassForPlugin* classobj =
        TJSCreateNativeClassForPlugin(TJS_W("PSBFile"), create_psb_instance);
#define TJS_NCM_REG_THIS classobj
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
    TJS_BEGIN_NATIVE_MEMBERS(PSBFile)
        TJS_DECL_EMPTY_FINALIZE_METHOD
        TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, NI_PSBFile, PSBFile)
        {
            return TJS_S_OK;
        }
        TJS_END_NATIVE_CONSTRUCTOR_DECL(PSBFile)
        TJS_BEGIN_NATIVE_PROP_DECL(root)
        {
            TJS_BEGIN_NATIVE_PROP_GETTER
            {
                TJS_GET_NATIVE_INSTANCE(_this, NI_PSBFile);
                *result = _this->root();
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(root)
    TJS_END_NATIVE_MEMBERS
    return classobj;
}

void add_global_member(iTJSDispatch2* global, const tjs_char* name,
                       iTJSDispatch2* member) {
    tTJSVariant value(member);
    member->Release();
    if (TJS_FAILED(global->PropSet(TJS_MEMBERENSURE, name, nullptr,
                                   &value, global))) {
        TVPThrowExceptionMessage(TJS_W("Cannot register PSBFile"));
    }
}

void register_psbfile() {
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (!global) TVPThrowExceptionMessage(TJS_W("No script engine for PSBFile"));
    add_global_member(global, TJS_W("PSBFile"), create_psb_class());
    global->Release();
}

void mark_psbfile_ready() {
    krkrvita_boot_trace("retail-psbfile-ready");
}

} // namespace

NCB_PRE_REGIST_CALLBACK(register_psbfile);
NCB_POST_REGIST_CALLBACK(mark_psbfile_ready);

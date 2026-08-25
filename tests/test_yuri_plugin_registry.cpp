// Sealed Vita plugin-registry contract.
//
// The Vita build seals dynamic plugin loading: TVPLoadPlugin() resolves a
// module purely through ncbAutoRegister and throws TVPCannotLoadPlugin when
// the name is unknown. Some retail scripts
// wrap Plugins.link() in a catch that calls System.inform(), so a registry
// miss (or a fallback surface that throws while registering) turns into a
// modal window over a black frame on hardware.
//
// Compiling the module source, or finding its boot-trace marker in the ELF,
// proves neither of those things.  This test drives the exact registration
// path the Vita ELF uses: static ncbAutoRegister constructors -> AllRegist()
// -> LoadModule(), with a TVPExecuteScript that really compiles and runs the
// fallback literal on Yuri's TJS VM.
#include "ncbind/ncbind.hpp"
#include "tjs.h"

#include "PluginImpl.h"
#include "TextStream.h"

extern "C" {
#include "md5.h"
}

#include <codecvt>
#include <cstdlib>
#include <iostream>
#include <locale>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Declared by the generated Vita PluginImpl.cpp exactly this way; defined in
// ncbind.cpp.
extern std::set<ttstr> TVPRegisteredPlugins;

namespace {

TJS::tTJS* script_engine = nullptr;
std::vector<std::string> executed_scripts;
std::vector<std::string> boot_traces;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
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

std::string to_utf8(const ttstr& value) {
    const auto* begin = reinterpret_cast<const char16_t*>(value.c_str());
    return utf8_converter().to_bytes(begin, begin + value.GetLen());
}

// Mirrors the generated Vita PluginImpl.cpp: registry hit, then internal
// module load.  The integrated-alias arm is deliberately excluded so this
// test cannot be satisfied by name aliasing alone.
bool try_load_plugin(const ttstr& name) {
    const ttstr module = name.AsLowerCase();
    if (TVPRegisteredPlugins.find(module) != TVPRegisteredPlugins.end())
        return true;
    return ncbAutoRegister::LoadModule(module);
}

void link_plugin(const char16_t* name, const char* label) {
    try {
        if (!try_load_plugin(ttstr(reinterpret_cast<const tjs_char*>(name))))
            fail(label);
    } catch (eTJSScriptError& error) {
        std::cerr << "  registering " << label << " threw: "
                  << to_utf8(error.GetMessage()) << " in "
                  << to_utf8(error.GetBlockName()) << " at "
                  << error.GetPosition() << '\n';
        fail(label);
    } catch (eTJSError& error) {
        std::cerr << "  registering " << label << " threw: "
                  << to_utf8(error.GetMessage()) << '\n';
        fail(label);
    } catch (...) {
        fail(label);
    }
}

TJS::tTJSVariant eval(const char* expression) {
    TJS::tTJSVariant result;
    const auto wide = utf8_converter().from_bytes(expression);
    script_engine->EvalExpression(
        TJS::ttstr(reinterpret_cast<const tjs_char*>(wide.c_str())), &result);
    return result;
}

void exec(const char* source) {
    const auto wide = utf8_converter().from_bytes(source);
    try {
        script_engine->ExecScript(
            TJS::ttstr(reinterpret_cast<const tjs_char*>(wide.c_str())));
    } catch (eTJSScriptError& error) {
        std::cerr << "  script error: " << to_utf8(error.GetMessage())
                  << " in " << to_utf8(error.GetBlockName()) << " at "
                  << error.GetPosition() << '\n';
        fail(source);
    } catch (eTJSError& error) {
        std::cerr << "  error: " << to_utf8(error.GetMessage()) << '\n';
        fail(source);
    }
}

// Assert a TJS expression, reporting the value that was actually produced so a
// regression names the broken member instead of only the contract.
void require(const char* expression, const char* message) {
    TJS::tTJSVariant value;
    try {
        value = eval(expression);
    } catch (eTJSScriptError& error) {
        std::cerr << "  " << expression << " raised "
                  << to_utf8(error.GetMessage()) << '\n';
        fail(message);
    }
    if (value.AsInteger() == 0) {
        std::cerr << "  " << expression << " evaluated to "
                  << to_utf8(ttstr(value)) << '\n';
        fail(message);
    }
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
        output = to_utf8(input);
        return true;
    } catch (...) {
        return false;
    }
}

void TVPThrowExceptionMessage(const tjs_char* message, const ttstr& detail) {
    throw eTJSError(ttstr(message) + detail);
}

ttstr TVPNormalizeStorageName(const ttstr& name) { return name; }
ttstr TVPExtractStorageName(const ttstr& name) { return name; }
void TVPGetLocalName(ttstr&) {}
bool TVPIsExistentStorage(const ttstr&) { return false; }
tTJSBinaryStream* TVPCreateStream(const ttstr&, tjs_uint32) { return nullptr; }
void TVPAddLog(const ttstr&) {}

iTJSTextReadStream* TVPCreateTextStreamForRead(const ttstr&, const ttstr&) {
    TVPThrowExceptionMessage(TJS_W("no storage in the registry host test"));
}

void TVPExecuteExpression(const ttstr& content, tTJSVariant* result) {
    script_engine->EvalExpression(content, result);
}

void TVPExecuteExpression(const ttstr& content, const ttstr& name, tjs_int,
                          iTJSDispatch2* context, tTJSVariant* result) {
    script_engine->EvalExpression(content, result, context, &name);
}

// Same control flow as the engine's tTJSPluginTryBlock: run the block, convert
// a TJS or unknown throw into a description, and rethrow only when the plugin
// asks for it.
void TVPDoTryBlock(tTVPTryBlockFunction tryblock,
                   tTVPCatchBlockFunction catchblock,
                   tTVPFinallyBlockFunction finallyblock, void* data) {
    try {
        tryblock(data);
    } catch (const eTJS& error) {
        if (finallyblock) finallyblock(data);
        tTVPExceptionDesc desc;
        desc.type = TJS_W("eTJS");
        desc.message = error.GetMessage();
        if (catchblock(data, desc)) throw;
        return;
    } catch (...) {
        if (finallyblock) finallyblock(data);
        tTVPExceptionDesc desc;
        desc.type = TJS_W("unknown");
        if (catchblock(data, desc)) throw;
        return;
    }
    if (finallyblock) finallyblock(data);
}

// The registration callbacks call this with the fallback literal.  Running it
// on the real VM is the point of the test: a fallback that fails to compile
// would otherwise only surface as an inform() window on hardware.
void TVPExecuteScript(const ttstr& content, const ttstr& name, tjs_int,
                      tTJSVariant* result) {
    executed_scripts.push_back(to_utf8(name));
    script_engine->ExecScript(content, result, nullptr, &name);
}

void TVP_md5_init(TVP_md5_state_t* pms) {
    md5_init(reinterpret_cast<md5_state_t*>(pms));
}

void TVP_md5_append(TVP_md5_state_t* pms, const tjs_uint8* data, int nbytes) {
    md5_append(reinterpret_cast<md5_state_t*>(pms),
               reinterpret_cast<const md5_byte_t*>(data), nbytes);
}

void TVP_md5_finish(TVP_md5_state_t* pms, tjs_uint8* digest) {
    md5_finish(reinterpret_cast<md5_state_t*>(pms), digest);
}

extern "C" void krkrvita_boot_trace(const char* stage) {
    boot_traces.push_back(stage ? stage : "");
}

extern "C" void krkrvita_write_error(const char*) {}

int main() {
    static_assert(sizeof(tjs_char) == sizeof(char16_t));

    const auto release_tjs = [](TJS::tTJS* engine) {
        if (engine) engine->Release();
    };
    std::unique_ptr<TJS::tTJS, decltype(release_tjs)>
        engine(new TJS::tTJS(), release_tjs);
    script_engine = engine.get();

    // On Vita these globals are registered native classes before any
    // Plugins.link() call.  This harness links only TJS2 and ncbind, so stand
    // in class objects of the same shape: without them a fallback that
    // clobbers Layer, or a plug-in that cannot find Scripts, would pass here
    // and fail on the device.
    exec(R"TJS(
// scriptsEx attaches to Kirikiri's built-in Scripts class. The engine build
// registers it from ScriptMgnIntf.cpp; this harness links only TJS2 and
// ncbind, so stand in a class object for ncbAttachTJS2Class to extend.
class Scripts {
    function Scripts() {}
}
global.Scripts = Scripts;

class Layer {
    var left = 0, top = 0, width = 0, height = 0;
    function Layer(window, parent) {}
    function copyRect(dl, dt, src, sl, st, sw, sh) { return void; }
}
global.Layer = Layer;
var krkrvitaProbeLayer = new Layer(void, void);
)TJS");

    ncbAutoRegister::AllRegist();

    // Names arrive from scripts exactly as the retail source spells them.
    link_plugin(u"motionplayer.dll", "motionplayer.dll did not link");
    link_plugin(u"layerExDraw.dll", "layerExDraw.dll did not link");
    link_plugin(u"scriptsEx.dll", "scriptsEx.dll did not link");

    // custom.tjs and initialize.tjs link some modules more than once, and KAGEX
    // re-links after a plugin-directory rescan.  A second link must not throw.
    link_plugin(u"layerExDraw.dll", "re-linking layerExDraw.dll failed");
    link_plugin(u"LAYEREXDRAW.DLL", "case-folded layerExDraw link failed");

    // Every fallback must have announced itself through the same boot trace the
    // hardware log is read for.
    const auto traced = [](const char* marker) {
        for (const auto& entry : boot_traces)
            if (entry == marker) return true;
        return false;
    };
    check(traced("retail-motionplayer-surface-ready"),
          "motionplayer surface did not emit its boot trace");
    check(traced("retail-layerexdraw-surface-ready"),
          "layerExDraw surface did not emit its boot trace");
    check(traced("retail-scriptsex-surface-ready"),
          "scriptsEx surface did not emit its boot trace");

    // Registration must be idempotent: the two script-level fallbacks run once
    // each, not once per link. scriptsEx is a native module and runs no script.
    check(executed_scripts.size() == 2,
          "fallback surfaces re-executed on a repeated Plugins.link");

    // motionplayer: a retail title reaches Motion.ResourceManager from
    // motion.tjs(20) immediately after the caught link.
    exec(R"TJS(
var krkrvitaMotionManager = new Motion.ResourceManager(void, 16);
var krkrvitaMotionPlayer = new Motion.Player(krkrvitaMotionManager);
krkrvitaMotionManager.load("bg/test.psb");
krkrvitaMotionPlayer.play("walk", Motion.PlayFlagForce);
krkrvitaMotionPlayer.progress(200);
var krkrvitaMotionAdaptor = new Motion.SeparateLayerAdaptor(krkrvitaProbeLayer);
)TJS");
    require("Motion.ResourceManager !== void",
            "Motion.ResourceManager missing after link");
    require("krkrvitaMotionPlayer.motion == 'walk'",
            "Motion.Player.play did not record the motion name");
    require("krkrvitaMotionPlayer.playing == false",
            "Motion.Player.progress did not retire a finished motion");
    require("Motion.PlayFlagStealth == 16",
            "Motion play-flag constants differ");

    // layerExDraw: GdiPlus.Image must construct, clone and report bounds, and
    // the Layer draw entry points must exist on an instance created before the
    // link (KAGEX builds its base layers during initialize.tjs).
    exec(R"TJS(
var krkrvitaImage = new GdiPlus.Image();
krkrvitaImage.load("image/title.png");
krkrvitaImage.setSize(320, 240);
var krkrvitaImageClone = krkrvitaImage.Clone();
var krkrvitaBounds = krkrvitaImage.GetBounds();
)TJS");
    require("krkrvitaImageClone.imageWidth == 320 && "
            "krkrvitaImageClone.imageHeight == 240",
            "GdiPlus.Image.Clone did not carry the image size");
    require("krkrvitaBounds.width == 320",
            "GdiPlus.Image.GetBounds contract differs");
    require("typeof Layer.drawImage == 'Object'",
            "Layer.drawImage was not installed by the layerExDraw fallback");
    require("typeof krkrvitaProbeLayer.copyRect == 'Object'",
            "the layerExDraw fallback clobbered the native Layer class");

    // scriptsEx: KAGEX calls getObjectCount during startup, and the wider
    // surface has to behave like the upstream plug-in rather than return
    // plausible defaults.  TJS2 dictionaries expose no "count" member, so a
    // script-level fallback cannot answer this at all.
    exec(R"TJS(
var krkrvitaStruct = %[b: 2, a: 1, nested: %[x: 1]];
var krkrvitaKeys = Scripts.getObjectKeys(krkrvitaStruct);
var krkrvitaCopy = Scripts.clone(krkrvitaStruct);
var krkrvitaTarget = %[];
Scripts.propSet(krkrvitaTarget, "made", 7, Scripts.pfMemberEnsure);
)TJS");
    require("Scripts.getObjectCount(%[a:1, b:2]) == 2",
            "Scripts.getObjectCount did not count dictionary members");
    require("krkrvitaKeys.count == 3 && krkrvitaKeys[0] == 'a'",
            "Scripts.getObjectKeys did not return sorted member names");
    require("Scripts.equalStruct(krkrvitaCopy, krkrvitaStruct) == true",
            "Scripts.clone/equalStruct did not deep-copy a nested structure");
    require("krkrvitaCopy.nested !== krkrvitaStruct.nested",
            "Scripts.clone returned a shallow reference");
    require("Scripts.propGet(krkrvitaTarget, 'made') == 7",
            "Scripts.propSet/propGet round trip differs");
    require("Scripts.getObjectContext(krkrvitaProbeLayer.copyRect) === "
            "krkrvitaProbeLayer",
            "Scripts.getObjectContext did not return the bound object");
    require("Scripts.getMD5HashString(<% 61 62 63 %>) == "
            "'900150983cd24fb0d6963f7d28e17f72'",
            "Scripts.getMD5HashString differs from the reference digest");

    // An unknown module must still fail, otherwise the registry check is
    // vacuous and every missing plugin would silently look supported.
    check(!try_load_plugin(TJS_W("krkrvita_not_a_plugin.dll")),
          "the sealed registry accepted an unregistered module name");

    std::cout << "yuri plugin registry contract ok\n";
    return 0;
}

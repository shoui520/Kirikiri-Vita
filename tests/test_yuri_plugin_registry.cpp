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
#include "krkrvita/yuri_checked_plugin_loader.hpp"
#include "krkrvita/yuri_startup_execution.hpp"
#include "krkrvita/kag_inline_script.hpp"
#include "krkrvita/xp3_archive.hpp"
#include "krkrvita/text_codec.hpp"

extern "C" {
#include "md5.h"
}

#include <codecvt>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Declared by the generated Vita PluginImpl.cpp exactly this way; defined in
// ncbind.cpp.
extern std::set<ttstr> TVPRegisteredPlugins;

namespace {

TJS::tTJS* script_engine = nullptr;
std::vector<std::string> executed_scripts;
std::vector<std::string> boot_traces;
std::string platform_language = "ja_jp";

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

// Execute the same checked loader included by Vita's generated PluginImpl.
bool try_load_plugin(const ttstr& name) {
    return krkrvita::try_load_yuri_plugin(name);
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
ttstr TVPExtractStorageName(const ttstr& name) {
    const auto text = to_utf8(name);
    const auto slash = text.find_last_of("/\\:>");
    return ttstr(slash == std::string::npos ? text : text.substr(slash + 1));
}
bool TVPLoadInternalPlugin(const ttstr& name) {
    return ncbAutoRegister::LoadModule(name);
}
tTJSMessageHolder TVPCannotLoadPlugin(
    TJS_W("CannotLoadPlugin"), TJS_W("Cannot load plugin: %1"));
std::string TVPGetCurrentLanguage() { return platform_language; }
void TVPGetLocalName(ttstr&) {}
bool TVPIsExistentStorage(const ttstr&) { return false; }
tTJSBinaryStream* TVPCreateStream(const ttstr&, tjs_uint32) { return nullptr; }
void TVPAddLog(const ttstr&) {}

iTJSTextReadStream* TVPCreateTextStreamForRead(const ttstr&, const ttstr&) {
    TVPThrowExceptionMessage(TJS_W("no storage in the registry host test"));
    return nullptr;
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

namespace {

// The native Plugins.link boundary uses the exact checked loader compiled
// into Vita, including name normalization, registration and unknown-DLL errors.
tjs_error TJS_INTF_METHOD probe_link(tTJSVariant*, tjs_int count,
                                     tTJSVariant** parameters, iTJSDispatch2*) {
    if (count < 1) return TJS_E_BADPARAMCOUNT;
    krkrvita::load_yuri_plugin(ttstr(*parameters[0]));
    return TJS_S_OK;
}

void install_probe_link() {
    exec("class Plugins {}");
    auto plugins = eval("Plugins");
    auto* object = plugins.AsObjectNoAddRef();
    auto* method = TJSCreateNativeClassMethod(probe_link);
    tTJSVariant value(method);
    method->Release();
    check(TJS_SUCCEEDED(object->PropSet(TJS_MEMBERENSURE, TJS_W("link"),
                                       nullptr, &value, object)),
          "cannot install native Plugins.link");
}

void execute_named(const std::string& text, const char* name) {
    const auto wide = utf8_converter().from_bytes(text);
    const auto wide_name = utf8_converter().from_bytes(name);
    const ttstr block(reinterpret_cast<const tjs_char*>(wide_name.c_str()));
    script_engine->ExecScript(
        ttstr(reinterpret_cast<const tjs_char*>(wide.c_str())),
        nullptr, nullptr, &block);
}

std::string archive_text(const krkrvita::Xp3Archive& archive,
                         const char* name) {
    const auto* entry = archive.find(name);
    check(entry != nullptr, "required SeaBed script is missing");
    std::string error, text;
    const auto bytes = archive.read(*entry, 4 * 1024 * 1024, nullptr, &error);
    check(bytes.has_value(), "cannot read SeaBed script");
    check(krkrvita::decode_kirikiri_text(*bytes, text, &error),
          "cannot decode SeaBed script");
    return text;
}

std::string first_inline_script(const std::string& scenario) {
    std::istringstream input(scenario);
    std::vector<std::string> lines;
    std::string line;
    bool inside = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "[iscript]") { inside = true; continue; }
        if (inside && line == "[endscript]") {
            return krkrvita::assemble_kag_inline_script<char>(0, lines.size(),
                [&](std::size_t index) { return lines[index].c_str(); });
        }
        if (inside) lines.push_back(line);
    }
    fail("SeaBed first.ks has no complete iscript block");
}

// Extract these complete methods unchanged from the local game, including
// their real sflags/menu/history side effects. No proprietary fixture is
// embedded in the repository. The three methods have tab-indented closing
// braces at class-member level in both supported SeaBed script revisions.
std::string game_method(const std::string& source, const char* name) {
    const auto begin = source.find(std::string("\tfunction ") + name + "(");
    check(begin != std::string::npos, "SeaBed method is missing");
    auto end = source.find("\n\t}\r", begin);
    if (end == std::string::npos) end = source.find("\n\t}\n", begin);
    check(end != std::string::npos, "SeaBed method is unterminated");
    return source.substr(begin, end + 3 - begin);
}

void test_fbfsteam(const std::filesystem::path& game) {
    install_probe_link();
    std::string startup = R"TJS(
global.FBFSteam = null;
Plugins.link('FBFSteamPlugin.dll');
global.FBFSteam = new CFBFSteam();
FBFSteam.InitSteamAPI();
function FBFCallback() { FBFSteam.RunCallBacks(); kag.update(); }
System.addContinuousHandler(FBFCallback);
Scripts.execStorage('system/Initialize.tjs');
)TJS";
    std::string first = R"TJS(
if (sf.textfade === void) {
    if (FBFSteam != void) {
        var language = FBFSteam.GetUserLanguage();
        if (language == 1) kag.onJapaneseLanguageMenuClick();
        else kag.onEnglishLanguageMenuClick();
    }
    sf.textfade = 1;
}
)TJS";
    std::string methods = R"TJS(
function onJapaneseLanguageMenuClick() { sflags.fbflang=1; }
function onEnglishLanguageMenuClick() { sflags.fbflang=0; }
function onConductorScript(text, name, line) { Scripts.exec(text, name, line); }
)TJS";
    if (!game.empty()) {
        check(!std::filesystem::exists(game / "patch.xp3"),
              "this probe requires the unpatched SeaBed archive layout");
        std::string error;
        const auto archive = krkrvita::Xp3Archive::open(game / "data.xp3", &error);
        check(archive.has_value(), "cannot open SeaBed data.xp3");
        startup = archive_text(*archive, "startup.tjs");
        first = first_inline_script(archive_text(*archive, "scenario/first.ks"));
        const auto window = archive_text(*archive, "system/MainWindow.tjs");
        methods = game_method(window, "onJapaneseLanguageMenuClick") + "\n" +
                  game_method(window, "onEnglishLanguageMenuClick") + "\n" +
                  game_method(window, "onConductorScript");
        std::cout << "Loaded actual SeaBed startup, first.ks and language/"
                     "script-dispatch methods from data.xp3\n";
    }
    exec(R"TJS(
var initializedStorages=[], registeredHandlers=[];
class System {
    function addContinuousHandler(handler) { registeredHandlers.add(handler); }
}
Scripts.execStorage=function(path) { initializedStorages.add(path); };
var f=%[], sf=%[];
)TJS");
    // Graphics/window services stop at instrumented host boundaries. The
    // game methods, TJS VM, native plugin and loader execute for real.
    exec((R"TJS(
class ProbeKAG {
    var sflags=sf, updateCount=0, menuUpdates=0;
    var historyLayer=%['onLanguageChange'=>function() {}];
    function update() { ++updateCount; }
    function FBFSetWindowMenuTexts() { ++menuUpdates; }
)TJS" + methods + "\n}\nvar kag=new ProbeKAG();").c_str());

    // Scripts.exec dispatches exactly like KAG's native script callback.
    auto scripts = eval("Scripts");
    auto* script_class = scripts.AsObjectNoAddRef();
    auto* method = TJSCreateNativeClassMethod(
        [](tTJSVariant* result, tjs_int count, tTJSVariant** parameters,
           iTJSDispatch2*) -> tjs_error {
            if (count < 1) return TJS_E_BADPARAMCOUNT;
            const ttstr name = count > 1 ? ttstr(*parameters[1]) : ttstr();
            script_engine->ExecScript(ttstr(*parameters[0]), result,
                                      nullptr, &name,
                                      count > 2 ? parameters[2]->AsInteger() : 0);
            return TJS_S_OK;
        });
    tTJSVariant method_value(method);
    method->Release();
    check(TJS_SUCCEEDED(script_class->PropSet(TJS_MEMBERENSURE, TJS_W("exec"),
        nullptr, &method_value, script_class)), "cannot install Scripts.exec");

    // A patch class can already exist when the game's Plugins.link executes.
    // The native registration must replace it with the numeric-language API.
    if (game.empty())
        exec("class CFBFSteam { function GetUserLanguage() { return 'JP'; } }");
    if (!game.empty() && std::filesystem::exists(game / "patch.tjs")) {
        std::ifstream input(game / "patch.tjs", std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        std::string text, error;
        check(krkrvita::decode_kirikiri_text(bytes, text, &error),
              "cannot decode local patch.tjs");
        execute_named(text, "patch.tjs");
    }
    bool recovered = false;
    const bool original_succeeded = krkrvita::execute_yuri_startup(
        [&] { execute_named(startup, "startup.tjs"); },
        [] { return true; }, [&] { recovered = true; });
    check(original_succeeded && !recovered,
          "SeaBed startup used KAG recovery after an earlier failure");
    require("FBFSteam !== null", "startup left FBFSteam null");
    require("registeredHandlers.count == 1 && initializedStorages.count == 1",
            "startup did not reach callback registration and KAG initialization");
    require("initializedStorages[0] == 'system/Initialize.tjs'",
            "unexpected KAG initialization entry point");
    require("FBFSteam.GetUserLanguage() === 1", "Japanese language is not numeric");
    exec("var savedSteamClass=CFBFSteam, savedSteamObject=FBFSteam;"
         "Plugins.link('plugin/FBFSTEAMPLUGIN.DLL');");
    require("CFBFSteam === savedSteamClass && FBFSteam === savedSteamObject",
            "relinking replaced live Steam objects");
    require("FBFSteam.InitSteamAPI() == false && FBFSteam.UploadStats() == false",
            "offline module claimed a Steam connection or upload");
    exec("FBFSteam.ResetAllStats(); FBFSteam.UnlockAchievement(0);"
         "FBFSteam.UnlockAchievement(42); FBFSteam.ShutDownSteamAPI();"
         "FBFSteam.ShutDownSteamAPI(); registeredHandlers[0]();");
    require("kag.updateCount == 1", "real startup callback did not update KAG");

    const auto wide = utf8_converter().from_bytes(first);
    tTJSVariant inline_text(ttstr(reinterpret_cast<const tjs_char*>(wide.c_str())));
    auto* global = script_engine->GetGlobalNoAddRef();
    global->PropSet(TJS_MEMBERENSURE, TJS_W("inlineText"), nullptr,
                    &inline_text, global);
    for (const auto* language : {"ja_jp", "en_us", "fr_fr", "ja-JP"}) {
        platform_language = language;
        exec("delete sf.textfade; kag.onConductorScript(inlineText, 'first.ks', 23);");
        require(language[0] == 'j' ? "sf.fbflang === 1" : "sf.fbflang === 0",
                "real first.ks selected the wrong language");
        require("sf.textfade === 1", "first.ks did not complete language setup");
    }
    exec("sf.fbflang=7; kag.onConductorScript(inlineText, 'first.ks', 23);");
    require("sf.fbflang === 7", "startup changed an existing language preference");
    exec("var rejected=false; try { Plugins.link('unknown.dll'); }"
         "catch(e) { rejected=true; }");
    require("rejected", "unknown DLL was silently accepted");
    check(TVPRegisteredPlugins.count(TJS_W("fbfsteamplugin.dll")) == 1,
          "FBFSteam module was not entered into the Vita registry");
    check(std::count(boot_traces.begin(), boot_traces.end(),
                     "retail-fbfsteam-offline-ready") == 1,
          "FBFSteam registration did not run exactly once");

    // Negative control: exercise Vita's real recovery policy, then prove the
    // resulting null access remains visible rather than passing the probe.
    recovered = false;
    check(!krkrvita::execute_yuri_startup([&] {
        execute_named("global.FBFSteam=null; Plugins.link('missing.dll');",
                      "broken-startup.tjs");
    }, [] { return true; }, [&] { recovered = true; }),
          "broken startup was reported as successful");
    check(recovered, "missing plugin did not enter KAG recovery");
    bool null_failed = false;
    exec("delete sf.textfade;");
    try { execute_named(first, "first.ks"); }
    catch (const eTJS&) { null_failed = true; }
    check(null_failed, "negative control did not reproduce null-object failure");
    bool propagated = false;
    recovered = false;
    try {
        krkrvita::execute_yuri_startup([] { throw std::runtime_error("startup"); },
            [] { return false; }, [&] { recovered = true; });
    } catch (const std::runtime_error&) { propagated = true; }
    check(propagated && !recovered,
          "startup swallowed an error when Initialize.tjs was unavailable");
    std::cout << "FBFSteam offline execution: startup, native link, relink, "
                 "callback, language branches, saved preference and negative "
                 "recovery control passed (window/render boundaries instrumented)\n";
}

} // namespace

int main(int argc, char** argv) {
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

    check(argc == 1 || (argc == 3 && std::string(argv[1]) == "--seabed"),
          "usage: plugin-registry-test [--seabed GAME_DIR]");
    test_fbfsteam(argc == 3 ? std::filesystem::path(argv[2]) : std::filesystem::path());

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

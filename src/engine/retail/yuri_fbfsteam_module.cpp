#include "ncbind/ncbind.hpp"

#include "krkrvita/retail_bootstrap.hpp"

#include <string>

#define NCB_MODULE_NAME TJS_W("FBFSteamPlugin.dll")

extern std::string TVPGetCurrentLanguage();

// Offline Fruitbat Factory API. Steam services are unavailable on Vita;
// initialization and uploads must not claim to have contacted Steam. Language
// selection remains functional, using the engine's platform locale. SeaBed
// expects numeric 1 for Japanese and 0 for English, not Steam language strings.
class CFBFSteam {
public:
    bool InitSteamAPI() { return false; }
    void ShutDownSteamAPI() {}
    void ResetAllStats() {}
    void RunCallBacks() {}
    void UnlockAchievement(tjs_int) {}
    bool UploadStats() { return false; }
    tjs_int GetUserLanguage() {
        const std::string language = TVPGetCurrentLanguage();
        return language == "ja" || language.compare(0, 3, "ja_") == 0 ||
               language.compare(0, 3, "ja-") == 0 ? 1 : 0;
    }
};

NCB_REGISTER_CLASS(CFBFSteam) {
    Constructor();
    NCB_METHOD(InitSteamAPI);
    NCB_METHOD(ShutDownSteamAPI);
    NCB_METHOD(ResetAllStats);
    NCB_METHOD(RunCallBacks);
    NCB_METHOD(UnlockAchievement);
    NCB_METHOD(UploadStats);
    NCB_METHOD(GetUserLanguage);
}

namespace {
void trace_fbfsteam_ready() {
    krkrvita_boot_trace("retail-fbfsteam-offline-ready");
}
}

NCB_POST_REGIST_CALLBACK(trace_fbfsteam_ready);

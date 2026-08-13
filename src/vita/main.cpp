#include "krkrvita/bubble.hpp"
#include "krkrvita/game.hpp"
#include "krkrvita/patch_manifest.hpp"
#include "krkrvita/patch_repository.hpp"
#include "krkrvita/profile.hpp"
#include "krkrvita/retail_filter.hpp"
#include "krkrvita/runtime.hpp"

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/system_param.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace krkrvita;
constexpr auto kDataRoot = "ux0:data/krkrvita";

class NetworkSession {
public:
    bool initialize(std::string* error) {
        memory_.resize(1024 * 1024);
        if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0 ||
            sceSysmoduleLoadModule(SCE_SYSMODULE_SSL) < 0) {
            if (error) *error = "cannot load Vita network modules";
            return false;
        }
        SceNetInitParam parameters{memory_.data(), static_cast<int>(memory_.size()), 0};
        const auto net = sceNetInit(&parameters);
        if (net < 0 && net != static_cast<int>(0x80410103)) {
            if (error) *error = "sceNetInit failed";
            return false;
        }
        sceNetCtlInit();
        initialized_ = true;
        return true;
    }

    ~NetworkSession() {
        if (!initialized_) return;
        sceNetCtlTerm();
        sceNetTerm();
        sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
        sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    }

private:
    std::vector<unsigned char> memory_;
    bool initialized_ = false;
};

std::vector<GameDescriptor> scan_games() {
    std::vector<GameDescriptor> games;
    const std::filesystem::path root = std::filesystem::path(kDataRoot) / "games";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    for (const auto& item : std::filesystem::directory_iterator(root, ec)) {
        if (!item.is_directory()) continue;
        try {
            auto game = GameScanner::scan(item.path());
            if (!game.archives.empty()) games.push_back(std::move(game));
        } catch (...) {
        }
    }
    std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) {
        return a.display_name < b.display_name;
    });
    return games;
}

std::string direct_game_id() {
    char parameter[1024]{};
    if (sceAppMgrGetAppParam(parameter) < 0) return {};
    const std::string value(parameter);
    const auto marker = value.find("game=");
    if (marker == std::string::npos) return {};
    auto game_id = value.substr(marker + 5);
    const auto end = game_id.find_first_of("& \r\n");
    if (end != std::string::npos) game_id.resize(end);
    return game_id;
}

bool prepare_game(const GameDescriptor& game, GameProfile& profile, std::string& status) {
    profile = GameProfile::defaults(game);
    PatchRepository repository(std::filesystem::path(kDataRoot) / "cache");
    try {
        NetworkSession network;
        bool network_ready = false;
        const auto ensure_network = [&]() {
            if (network_ready) return true;
            network_ready = network.initialize(&status);
            return network_ready;
        };
        if (!std::filesystem::is_regular_file(repository.manifest_path())) {
            if (!ensure_network()) return false;
            CurlHttpClient http;
            if (!repository.update_manifest(http, &status)) return false;
        }
        const auto manifest = repository.load_manifest();
        const auto resolution = PatchResolver::resolve(game, manifest);
        if (resolution.automatic && resolution.best()) {
            auto files = repository.cached_bundle(*resolution.best()->entry);
            if (files.empty()) {
                if (!ensure_network()) return false;
                CurlHttpClient http;
                files = repository.fetch_bundle(http, *resolution.best()->entry);
            }
            profile.patch_brand = resolution.best()->entry->brand;
            profile.patch_title = resolution.best()->entry->canonical_title;
            profile.patch_commit = std::string(kPatchCommit);
            profile.filter_origin = "patch-repository";
            for (const auto& file : files) {
                const auto filename = std::filesystem::path(file.relative_path).filename();
                if (filename == "xp3filter.tjs") profile.xp3_filter_path = file.cache_path;
                if (filename == "patch.tjs") profile.patch_root = file.cache_path.parent_path();
            }
            if (profile.patch_root.empty() && !profile.xp3_filter_path.empty()) {
                profile.patch_root = profile.xp3_filter_path.parent_path();
            }
        }
        if (profile.xp3_filter_path.empty()) {
            const auto fallback = prepare_filter_fallback(
                game, std::filesystem::path(kDataRoot) / "generated", &status);
            if (!fallback) return false;
            profile.xp3_filter_path = fallback->path;
            profile.patch_root = fallback->path.parent_path();
            profile.filter_origin = fallback->origin;
            if (profile.patch_commit.empty()) profile.patch_commit = "local";
        }
        std::string error;
        FilterVerification verification;
        if (!verify_retail_filter(game, profile.xp3_filter_path, &verification, &error)) {
            status = error;
            return false;
        }
        const auto profile_path = std::filesystem::path(kDataRoot) / "profiles" /
                                  (profile.game_id + ".ini");
        if (!profile.save(profile_path, &error)) {
            status = error;
            return false;
        }
        status = "XP3 FILTER VERIFIED " + std::to_string(verification.recognized) + "/" +
                 std::to_string(verification.samples);
        return true;
    } catch (const std::exception& exception) {
        status = exception.what();
        return false;
    }
}

bool make_bubble(const GameDescriptor& game, std::string& status) {
    const auto title_id = bubble_title_id(game);
    const auto game_id = game.fingerprint.substr(0, 16);
    const auto staging = std::filesystem::path(kDataRoot) / "install" / title_id;
    if (!stage_bubble({game.display_name, title_id, game_id, game.executable},
                      "app0:bubble", staging, &status)) {
        return false;
    }
    const auto result = install_staged_bubble(staging);
    if (result < 0) {
        char message[80]{};
        std::snprintf(message, sizeof(message), "BUBBLE INSTALL FAILED 0X%08X", result);
        status = message;
        return false;
    }
    status = "DIRECT BUBBLE INSTALLED";
    return true;
}

} // namespace

int main() {
    SceAppUtilInitParam app_parameters{};
    SceAppUtilBootParam boot_parameters{};
    sceAppUtilInit(&app_parameters, &boot_parameters);

    VitaRenderer renderer;
    std::string status;
    if (!renderer.initialize(&status)) sceKernelExitProcess(1);
    VitaAudio audio;
    if (!audio.initialize(&status)) status = "AUDIO INIT FAILED";
    VitaInput input;
    auto games = scan_games();
    std::size_t selection = 0;
    bool exit = false;
    bool direct_attempted = false;
    int enter_assignment = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enter_assignment);
    const auto accept_button = enter_assignment == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE
        ? SCE_CTRL_CIRCLE : SCE_CTRL_CROSS;

    const auto requested_game = direct_game_id();
    if (!requested_game.empty()) {
        const auto found = std::find_if(games.begin(), games.end(), [&](const auto& game) {
            return game.fingerprint.rfind(requested_game, 0) == 0;
        });
        if (found != games.end()) selection = static_cast<std::size_t>(found - games.begin());
    }

    while (!exit) {
        const auto state = input.poll();
        if (!games.empty()) {
            if (state.pressed & SCE_CTRL_UP) selection = selection == 0 ? games.size() - 1 : selection - 1;
            if (state.pressed & SCE_CTRL_DOWN) selection = (selection + 1) % games.size();
            if ((state.pressed & accept_button) ||
                (!requested_game.empty() && !direct_attempted)) {
                direct_attempted = true;
                GameProfile profile;
                if (prepare_game(games[selection], profile, status)) {
                    std::string engine_error;
                    if (!yuri_runtime().start(profile, &engine_error)) status = engine_error;
                }
            }
            if (state.pressed & SCE_CTRL_SQUARE) make_bubble(games[selection], status);
        }
        if (state.pressed & SCE_CTRL_TRIANGLE) exit = true;

        renderer.begin();
        renderer.rectangle(0, 0, 960, 68, 0.02f, 0.45f, 0.55f);
        renderer.text(28, 19, 4, "KIRIKIRI VITA");
        renderer.text(700, 25, 2, "RETAIL COMPATIBILITY");
        if (games.empty()) {
            renderer.text(40, 130, 3, "NO GAMES FOUND");
            renderer.text(40, 175, 2, "COPY EACH GAME TO UX0:DATA/KRKRVITA/GAMES");
        } else {
            renderer.text(38, 92, 2, "GAME LIBRARY");
            const auto first = selection > 4 ? selection - 4 : 0;
            const auto last = std::min(games.size(), first + 8);
            for (auto i = first; i < last; ++i) {
                const auto y = 125.0f + static_cast<float>(i - first) * 42;
                if (i == selection) renderer.rectangle(25, y - 8, 910, 34, 0.08f, 0.38f, 0.48f);
                const auto label = games[i].display_name.empty()
                    ? games[i].fingerprint.substr(0, 16) : games[i].display_name;
                renderer.text(42, y, 2, label);
                renderer.text(670, y, 2, games[i].fingerprint.substr(0, 16));
            }
        }
        renderer.rectangle(0, 485, 960, 59, 0.01f, 0.08f, 0.11f);
        renderer.text(25, 498, 2, "X START   SQUARE CREATE BUBBLE   TRIANGLE EXIT");
        if (!status.empty()) renderer.text(25, 524, 1.5f, status.substr(0, 90), 1.0f, 0.78f, 0.25f);
        renderer.end();
        sceKernelDelayThread(16000);
    }
    sceAppUtilShutdown();
    sceKernelExitProcess(0);
    return 0;
}

#include "krkrvita/runtime.hpp"

#include "krkrvita/storage.hpp"

#include <filesystem>
#include <memory>

namespace krkrvita {
namespace {

class YuriRuntime final : public EngineRuntime {
public:
    bool start(const GameProfile& profile, std::string* error) override {
        if (!std::filesystem::is_directory(profile.game_path)) {
            if (error) *error = "game directory is unavailable";
            return false;
        }
        auto mounted = GameStorage::mount(profile, error);
        if (!mounted) return false;
        storage_ = std::make_unique<GameStorage>(std::move(*mounted));

        // Force the exact retail startup path through XP3 decryption and the
        // Kirikiri text codec before any engine objects are created.
        const auto startup = storage_->read_script("startup.tjs", error);
        if (!startup) {
            storage_.reset();
            return false;
        }
        // Compatibility bundles can provide a patch.tjs that must execute
        // before startup.tjs. Presence is optional, but decoding is not.
        if (storage_->exists("patch.tjs") && !storage_->read_script("patch.tjs", error)) {
            storage_.reset();
            return false;
        }

        // The platform lifecycle is live, but Yuri's Cocos-bound window and render
        // classes still have to be replaced before TVPApplication can safely enter
        // its event loop. Keep this a hard failure: a launcher screen must never be
        // reported as a working retail game.
        if (error) *error = "retail startup mounted; Yuri native classes are not linked yet";
        return false;
    }

    bool running() const override { return running_; }
    void input(const InputSnapshot&) override {}
    void frame() override {}
    void stop() override {
        running_ = false;
        storage_.reset();
    }

private:
    bool running_ = false;
    std::unique_ptr<GameStorage> storage_;
};

} // namespace

EngineRuntime& yuri_runtime() {
    static YuriRuntime runtime;
    return runtime;
}

} // namespace krkrvita

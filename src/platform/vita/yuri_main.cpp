#include "tjsCommHead.h"

#include "Application.h"
#include "RenderManager.h"
#include "krkrvita/engine_tick_pacer.hpp"
#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/threading_self_test.hpp"
#include "krkrvita/vita_memory_budget.hpp"
#include "krkrvita/vita_thread_policy.hpp"
#include "krkrvita/vitagl_presenter.hpp"
#include "yuri_input.hpp"
#include "yuri_window_layer.hpp"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <exception>
#include <cstdint>
#include <string>
#include <thread>

extern int _argc;
extern char** _argv;
extern std::thread::id TVPMainThreadID;
extern bool TVPStartupSuccess;
extern "C" std::uint64_t krkrvita_yuri_recycled_texture_count();

// VitaSDK otherwise reserves a fixed 128 MiB newlib heap and a 256 KiB main
// stack. ATTRIBUTE2=12 supplies the expanded application budget. Large decoded
// game images use independently reclaimable cached USER_RW memblocks; the
// 80 MiB VitaGL threshold keeps 64 MiB available for them plus 16 MiB for
// non-bitmap allocations outside both fixed pools. The larger stack is a real
// Vita process parameter:
// Kirikiri's nested
// script/render error path exceeded the default stack while reporting an
// allocation failure on hardware.
extern "C" {
unsigned int _newlib_heap_size_user =
    static_cast<unsigned int>(krkrvita::kVitaNewlibHeapBytes);
unsigned int sceUserMainThreadStackSize = 2u * 1024u * 1024u;
}

namespace {

struct InputShutdownGuard {
    ~InputShutdownGuard() { krkrvita_yuri_input_shutdown(); }
};

const char* option_value(int argc, char** argv, const char* prefix) {
    const std::string marker(prefix);
    for (int index = 1; index < argc; ++index) {
        if (!argv[index]) continue;
        const std::string argument(argv[index]);
        if (argument.compare(0, marker.size(), marker) == 0)
            return argv[index] + marker.size();
    }
    return nullptr;
}

const char* project_argument(int argc, char** argv) {
    for (int index = argc - 1; index >= 1; --index) {
        if (!argv[index] || argv[index][0] == '-') continue;
        if (std::string(argv[index]).compare(0, 5, "game=") == 0) continue;
        return argv[index];
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    krkrvita_boot_trace("main-entered");
    krkrvita_boot_trace("vita-newlib-heap-128m");
    krkrvita_boot_trace("vita-main-thread-stack-2m");
    try {
        krkrvita::apply_vita_main_thread_policy();
        krkrvita_boot_trace("vita-main-thread-policy-ready");
        krkrvita_boot_trace("vita-threading-self-test-entered");
        if (!krkrvita_vita_threading_self_test()) {
            krkrvita_report_launch_error(
                "Vita pthread/libstdc++ synchronization self-test failed.");
            return 1;
        }
        krkrvita_boot_trace("vita-threading-self-test-passed");

        krkrvita_resolve_launch(argc, argv);
        krkrvita_boot_trace("retail-launch-resolved");
        if (krkrvita_launch_error_reported()) return 1;

        const char* project = project_argument(argc, argv);
        if (!project || !*project) {
            krkrvita_report_launch_error("No retail game project was selected.");
            return 1;
        }

        _argc = argc;
        _argv = argv;
        TVPMainThreadID = std::this_thread::get_id();
        krkrvita_yuri_input_initialize(
            option_value(argc, argv, "-krkrprofile="));
        InputShutdownGuard input_shutdown_guard;
        if (!krkrvita_vitagl_initialize()) return 1;
        krkrvita_boot_trace("yuri-platform-ready");

        Application->StartApplication(ttstr(project));
        krkrvita_boot_trace("yuri-start-application-returned");
        if (!TVPStartupSuccess) {
            krkrvita_report_launch_error(
                "Yuri did not complete the retail startup script.");
            return 1;
        }

        krkrvita_boot_trace("yuri-event-loop-entered");
        const std::uint64_t event_loop_started = sceKernelGetProcessTimeWide();
        bool five_second_proof_written = false;
        bool thirty_second_proof_written = false;
        bool texture_recycler_proof_written = false;
        while (!Application->IsTarminate()) {
            const std::uint64_t loop_started = sceKernelGetProcessTimeWide();
            krkrvita_yuri_input_pump();
            Application->Run();
            krkrvita_yuri_present_frame();
            const std::uint64_t recycled_before =
                krkrvita_yuri_recycled_texture_count();
            iTVPTexture2D::RecycleProcess();
            if (!texture_recycler_proof_written &&
                krkrvita_yuri_recycled_texture_count() > recycled_before) {
                krkrvita_boot_trace("yuri-texture-recycler-drained");
                texture_recycler_proof_written = true;
            }
            const std::uint64_t elapsed =
                sceKernelGetProcessTimeWide() - event_loop_started;
            if (!five_second_proof_written && elapsed >= 5u * 1000u * 1000u &&
                krkrvita_vitagl_contentful_frames() > 0) {
                krkrvita_boot_trace("retail-runtime-5s-stable-with-video");
                five_second_proof_written = true;
            }
            if (!thirty_second_proof_written &&
                elapsed >= 30u * 1000u * 1000u &&
                krkrvita_vitagl_contentful_frames() > 0) {
                krkrvita_boot_trace("retail-runtime-30s-stable-with-video");
                thirty_second_proof_written = true;
            }
            // Yuri Android lets the Cocos director impose one 60 Hz deadline
            // over update + draw + swap. Do the same here. VitaGL's swap is
            // deliberately nonblocking so a changed frame does not serialize
            // compositor time with an additional vblank before the next KAG
            // timer can be serviced. A slow frame receives no further delay.
            // This cadence also preserves Yuri's engine-frame-based compressed
            // texture lifetime instead of turning it into a millisecond cache.
            const std::uint32_t delay = krkrvita::yuri_frame_delay_us(
                loop_started, sceKernelGetProcessTimeWide());
            if (delay != 0) sceKernelDelayThread(delay);
        }
        krkrvita_boot_trace("yuri-event-loop-exited");
        krkrvita_yuri_input_shutdown();
        Application->OnExit();
        return 0;
    } catch (const std::exception& error) {
        krkrvita_report_launch_error(error.what());
    } catch (...) {
        krkrvita_report_launch_error("Unhandled native exception in Yuri backend.");
    }
    return 1;
}

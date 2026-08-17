#include "tjsCommHead.h"

#include "Application.h"
#include "RenderManager.h"
#include "krkrvita/engine_tick_pacer.hpp"
#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/threading_self_test.hpp"
#include "krkrvita/vita_memory_budget.hpp"
#include "krkrvita/vita_thread_policy.hpp"
#include "krkrvita/vitagl_presenter.hpp"
#include "krkrvita/yuri_performance.hpp"
#include "yuri_input.hpp"
#include "yuri_window_layer.hpp"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>

#include <algorithm>
#include <cstdio>
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

struct PerformanceCounters {
    std::uint64_t loops = 0;
    std::uint64_t application_run_us = 0;
    std::uint64_t application_run_max_us = 0;
    std::uint64_t presentation_poll_us = 0;
    std::uint64_t presentation_poll_max_us = 0;
    std::uint64_t presented_frames = 0;
    std::uint64_t idle_polls = 0;
};

void write_performance_snapshot(const PerformanceCounters& counters,
                                std::uint64_t elapsed_us) {
    const krkrvita::YuriPerformanceSnapshot yuri =
        krkrvita::yuri_performance_snapshot();
    char text[1536]{};
    const int length = std::snprintf(
        text, sizeof(text),
        "elapsed_ms=%llu\n"
        "event_loops=%llu\n"
        "application_run_us_total=%llu\n"
        "application_run_us_max=%llu\n"
        "presentation_poll_us_total=%llu\n"
        "presentation_poll_us_max=%llu\n"
        "presented_frames=%llu\n"
        "uploaded_frames=%u\n"
        "full_frame_uploads=%u\n"
        "partial_frame_uploads=%u\n"
        "uploaded_pixels_total=%llu\n"
        "cursor_only_redraws=%llu\n"
        "idle_polls=%llu\n"
        "compositor_calls=%llu\n"
        "compositor_us_total=%llu\n"
        "compositor_us_max=%llu\n"
        "dirty_rects_total=%llu\n"
        "dirty_rects_max=%llu\n"
        "dirty_pixels_total=%llu\n"
        "dirty_pixels_max=%llu\n"
        "normal_window_updates=%llu\n"
        "full_window_updates=%llu\n"
        "bitmap_independ_calls=%llu\n"
        "bitmap_independ_copies=%llu\n"
        "bitmap_independ_copy_us_total=%llu\n"
        "bitmap_independ_copy_us_max=%llu\n"
        "bitmap_independ_copy_bytes_total=%llu\n"
        "bitmap_independ_copy_bytes_max=%llu\n",
        static_cast<unsigned long long>(elapsed_us / 1000),
        static_cast<unsigned long long>(counters.loops),
        static_cast<unsigned long long>(counters.application_run_us),
        static_cast<unsigned long long>(counters.application_run_max_us),
        static_cast<unsigned long long>(counters.presentation_poll_us),
        static_cast<unsigned long long>(counters.presentation_poll_max_us),
        static_cast<unsigned long long>(counters.presented_frames),
        krkrvita_vitagl_uploaded_frames(),
        krkrvita_vitagl_full_uploads(),
        krkrvita_vitagl_partial_uploads(),
        static_cast<unsigned long long>(krkrvita_vitagl_uploaded_pixels()),
        static_cast<unsigned long long>(
            counters.presented_frames >= krkrvita_vitagl_uploaded_frames()
                ? counters.presented_frames - krkrvita_vitagl_uploaded_frames()
                : 0),
        static_cast<unsigned long long>(counters.idle_polls),
        static_cast<unsigned long long>(yuri.compositor_calls),
        static_cast<unsigned long long>(yuri.compositor_us_total),
        static_cast<unsigned long long>(yuri.compositor_us_max),
        static_cast<unsigned long long>(yuri.dirty_rects_total),
        static_cast<unsigned long long>(yuri.dirty_rects_max),
        static_cast<unsigned long long>(yuri.dirty_pixels_total),
        static_cast<unsigned long long>(yuri.dirty_pixels_max),
        static_cast<unsigned long long>(yuri.normal_window_updates),
        static_cast<unsigned long long>(yuri.full_window_updates),
        static_cast<unsigned long long>(yuri.bitmap_independ_calls),
        static_cast<unsigned long long>(yuri.bitmap_independ_copies),
        static_cast<unsigned long long>(yuri.bitmap_independ_copy_us_total),
        static_cast<unsigned long long>(yuri.bitmap_independ_copy_us_max),
        static_cast<unsigned long long>(yuri.bitmap_independ_copy_bytes_total),
        static_cast<unsigned long long>(yuri.bitmap_independ_copy_bytes_max));
    if (length <= 0) return;
    const SceUID file = sceIoOpen("ux0:data/krkrvita/perf-stats.txt",
                                 SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                                 0666);
    if (file < 0) return;
    sceIoWrite(file, text,
               static_cast<SceSize>(std::min<int>(length, sizeof(text) - 1)));
    sceIoClose(file);
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
        bool performance_telemetry_proof_written = false;
        std::uint64_t next_performance_snapshot = 5u * 1000u * 1000u;
        std::uint64_t last_presented_frame = 0;
        PerformanceCounters performance;
        while (!Application->IsTarminate()) {
            const std::uint64_t loop_started = sceKernelGetProcessTimeWide();
            krkrvita_yuri_input_pump();
            const std::uint64_t application_started =
                sceKernelGetProcessTimeWide();
            Application->Run();
            const std::uint64_t application_elapsed =
                sceKernelGetProcessTimeWide() - application_started;
            performance.application_run_us += application_elapsed;
            performance.application_run_max_us = std::max(
                performance.application_run_max_us, application_elapsed);

            const std::uint64_t presentation_started =
                sceKernelGetProcessTimeWide();
            const bool frame_presented = krkrvita_yuri_present_frame();
            const std::uint64_t presentation_elapsed =
                sceKernelGetProcessTimeWide() - presentation_started;
            performance.presentation_poll_us += presentation_elapsed;
            performance.presentation_poll_max_us = std::max(
                performance.presentation_poll_max_us, presentation_elapsed);
            ++performance.loops;
            if (frame_presented)
                ++performance.presented_frames;
            else
                ++performance.idle_polls;
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
            if (frame_presented) last_presented_frame = elapsed;
            if (krkrvita::yuri_performance_snapshot_is_safe(
                    elapsed, next_performance_snapshot,
                    last_presented_frame)) {
                write_performance_snapshot(performance, elapsed);
                next_performance_snapshot = elapsed + 5u * 1000u * 1000u;
                if (!performance_telemetry_proof_written) {
                    krkrvita_boot_trace("vita-performance-telemetry-ready");
                    performance_telemetry_proof_written = true;
                }
            }
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

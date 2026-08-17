#include "krkrvita/vita_thread_policy.hpp"

#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include <stdexcept>
#include <string>

namespace krkrvita {
namespace {

static_assert(kVitaMainThreadPolicy.cpu_affinity_mask ==
                  SCE_KERNEL_CPU_MASK_USER_0,
              "main-thread affinity no longer matches the Vita SDK");
static_assert(kVitaRenderWorkerPolicies[0].cpu_affinity_mask ==
                  SCE_KERNEL_CPU_MASK_USER_1,
              "first render-worker affinity no longer matches the Vita SDK");
static_assert(kVitaRenderWorkerPolicies[1].cpu_affinity_mask ==
                  SCE_KERNEL_CPU_MASK_USER_2,
              "second render-worker affinity no longer matches the Vita SDK");

void apply_policy(const char* role, const VitaThreadPolicy& policy) {
    const SceUID thread = sceKernelGetThreadId();
    if (thread < 0)
        throw std::runtime_error(std::string("Cannot identify Vita ") + role +
                                 " thread (" + std::to_string(thread) + ")");

    const int affinity_result =
        sceKernelChangeThreadCpuAffinityMask(thread, policy.cpu_affinity_mask);
    if (affinity_result < 0)
        throw std::runtime_error(std::string("Cannot set Vita ") + role +
                                 " CPU affinity (" +
                                 std::to_string(affinity_result) + ")");

    const int priority_result =
        sceKernelChangeThreadPriority(thread, policy.priority);
    if (priority_result < 0)
        throw std::runtime_error(std::string("Cannot set Vita ") + role +
                                 " priority (" +
                                 std::to_string(priority_result) + ")");

    const int actual_affinity = sceKernelGetThreadCpuAffinityMask(thread);
    const int actual_priority = sceKernelGetThreadCurrentPriority();
    if (actual_affinity != policy.cpu_affinity_mask ||
        actual_priority != policy.priority)
        throw std::runtime_error(std::string("Vita ") + role +
                                 " scheduling policy did not take effect");
}

} // namespace

void apply_vita_main_thread_policy() {
    apply_policy("main", kVitaMainThreadPolicy);
}

void apply_vita_render_worker_policy(int task_index) {
    if (task_index < 1 || task_index > 2)
        throw std::runtime_error("Invalid Vita render-worker index");
    apply_policy("render-worker", kVitaRenderWorkerPolicies[task_index - 1]);
}

} // namespace krkrvita

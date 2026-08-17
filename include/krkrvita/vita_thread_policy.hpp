#pragma once

namespace krkrvita {

struct VitaThreadPolicy {
    int priority;
    int cpu_affinity_mask;
};

// Keep the engine thread in the common queue at Sony's documented game-app
// default.  Yuri's timer and audio workers also live in that queue through
// VitaSDK pthreads, so their requested higher priorities must be able to
// preempt the engine during an over-budget software-compositor frame.
constexpr VitaThreadPolicy kVitaMainThreadPolicy{160, 0x00010000};

// Sony documents priorities 64..127 as per-CPU ready queues when paired with
// one CPU affinity bit.  Retain the two row-disjoint render workers on their
// dedicated cores at the policy used by TheOfficialFloW's GTA:SA Vita port.
constexpr VitaThreadPolicy kVitaRenderWorkerPolicies[] = {
    {64, 0x00020000},
    {64, 0x00040000},
};

void apply_vita_main_thread_policy();
void apply_vita_render_worker_policy(int task_index);

} // namespace krkrvita

#pragma once

namespace krkrvita {

// Yuri recovers an interrupted startup.tjs by running System/Initialize.tjs.
// Keep this order shared with host probes: recovery can start KAG even when
// earlier startup statements (such as constructing FBFSteam) never ran.
template <class Startup, class HasInitialize, class Initialize>
bool execute_yuri_startup(Startup&& startup, HasInitialize&& has_initialize,
                          Initialize&& initialize) {
    try {
        startup();
        return true;
    } catch (...) {
        if (!has_initialize()) throw;
    }
    initialize();
    return false;
}

} // namespace krkrvita

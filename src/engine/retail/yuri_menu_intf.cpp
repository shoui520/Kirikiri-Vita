// Yuri's old spin-lock names map directly to current TJS critical sections.
#define tTJSSpinLock tTJSCriticalSection
#define tTJSSpinLockHolder tTJSCriticalSectionHolder

// MenuItemIntf.cpp includes "WindowIntf.h" relative to Yuri's source tree.
// Prime the shared include guard with the current krkrz ABI so the otherwise
// platform-neutral menu implementation does not drag Yuri's old drawable ABI
// into this translation unit.
#include "WindowIntf.h"

// This legacy desktop hook was removed from current krkrz. Menu click delivery
// itself remains intact; persistence is handled by the script runtime.
#define TVPDoSaveSystemVariables() ((void)0)
#include "MenuItemIntf.cpp"

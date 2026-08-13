// Yuri's old spin-lock names map directly to current TJS critical sections.
#define tTJSSpinLock tTJSCriticalSection
#define tTJSSpinLockHolder tTJSCriticalSectionHolder

// Keep Yuri's MenuItem implementation, but bind it to the current Window ABI.
#include "../../../vendor/krkrsdl2/external/krkrz/visual/WindowIntf.h"
#include "../../../vendor/yuri/src/core/visual/win32/MenuItemImpl.cpp"

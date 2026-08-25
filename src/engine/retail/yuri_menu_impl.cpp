// Yuri's old spin-lock names map directly to current TJS critical sections.
#define tTJSSpinLock tTJSCriticalSection
#define tTJSSpinLockHolder tTJSCriticalSectionHolder

// Keep Yuri's MenuItem implementation, but bind it to the current Window ABI.
#include "WindowIntf.h"
#include "MenuItemImpl.cpp"

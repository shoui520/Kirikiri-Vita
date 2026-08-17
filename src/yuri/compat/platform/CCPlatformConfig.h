#pragma once

// Yuri's POSIX storage implementation uses Cocos' platform constants only to
// select an iOS-specific home-directory rewrite. Vita needs the normal POSIX
// path, and importing Cocos for this preprocessor decision would cross the
// frontend/backend boundary.
#define CC_PLATFORM_UNKNOWN 0
#define CC_PLATFORM_IOS 1
#define CC_PLATFORM_VITA 100
#define CC_TARGET_PLATFORM CC_PLATFORM_VITA

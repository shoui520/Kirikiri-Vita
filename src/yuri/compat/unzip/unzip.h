#pragma once

// ZIPArchive.cpp was taken from Cocos' namespaced minizip copy and refers to
// its public types as cocos2d::*. Yuri also carries the same minizip headers,
// but without that namespace. Recreate only the original namespace boundary;
// the implementation itself remains Yuri's ZIPArchive.cpp.
#include <zlib.h>

namespace cocos2d {
extern "C" {
#include <minizip/ioapi.h>
#include <minizip/unzip.h>
}
} // namespace cocos2d

// ZIPArchive.cpp provides the minizip implementation itself. These two names
// are used unqualified by that implementation; the remaining public records
// are deliberately redeclared by Yuri for its embedded version.
using unzFile = cocos2d::unzFile;

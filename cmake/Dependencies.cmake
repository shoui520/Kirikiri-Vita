include_guard(GLOBAL)

include(FetchContent)

# Keep every imported package out of the source tree. Each build directory
# receives an immutable checkout/download under its own _deps directory.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# Primary Kirikiri runtime. This is the same revision previously recorded by
# the former Yuri Git submodule.
FetchContent_Declare(krkrvita_yuri
    GIT_REPOSITORY https://github.com/YuriSizuku/Kirikiroid2Yuri.git
    GIT_TAG 6e61ce3b81416ceb2be3427a5f0471edefab7151
    GIT_SHALLOW TRUE
    GIT_PROGRESS FALSE
    GIT_SUBMODULES ""
    SOURCE_SUBDIR krkrvita-fetch-only)

# Regex engine used by Yuri's TJS implementation. This is the exact revision
# previously recorded by the former Oniguruma Git submodule.
FetchContent_Declare(krkrvita_oniguruma
    GIT_REPOSITORY https://github.com/kkos/oniguruma.git
    GIT_TAG 4ef89209a239c1aea328cf13c05a2807e5c146d1
    GIT_SHALLOW TRUE
    GIT_PROGRESS FALSE
    GIT_SUBMODULES "")

# Authoritative portable implementations of scriptsEx.dll and
# layerExBTOA.dll. The prior repository copies differed only by changing
# ncbind.hpp to ncbind/ncbind.hpp; the build now supplies ncbind's directory
# directly and compiles these pinned upstream files unchanged.
FetchContent_Declare(krkrvita_krkr2_next
    GIT_REPOSITORY https://github.com/reAAAq/KrKr2-Next.git
    GIT_TAG 1abd1ed4e8aec7abd5d2524c3a9ad7886880602f
    GIT_SHALLOW TRUE
    GIT_PROGRESS FALSE
    GIT_SUBMODULES ""
    SOURCE_SUBDIR krkrvita-fetch-only)

# Official public-domain SQLite 3.53.4 amalgamation. The archive hash is
# checked before extraction; shell.c and sqlite3ext.h are not compiled.
FetchContent_Declare(krkrvita_sqlite
    URL https://sqlite.org/2026/sqlite-amalgamation-3530400.zip
    URL_HASH
        SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR krkrvita-fetch-only)

FetchContent_MakeAvailable(
    krkrvita_yuri
    krkrvita_oniguruma
    krkrvita_krkr2_next
    krkrvita_sqlite)

set(KRKRVITA_YURI_SOURCE_DIR "${krkrvita_yuri_SOURCE_DIR}")
set(KRKRVITA_ONIGURUMA_SOURCE_DIR "${krkrvita_oniguruma_SOURCE_DIR}")
set(KRKRVITA_ONIGURUMA_BINARY_DIR "${krkrvita_oniguruma_BINARY_DIR}")
set(KRKRVITA_KRKR2_NEXT_SOURCE_DIR "${krkrvita_krkr2_next_SOURCE_DIR}")
set(KRKRVITA_SQLITE_SOURCE_DIR "${krkrvita_sqlite_SOURCE_DIR}")

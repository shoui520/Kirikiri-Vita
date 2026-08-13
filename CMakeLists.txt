cmake_minimum_required(VERSION 3.20)
project(krkrvita VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KRKRVITA_BUILD_TESTS "Build host-side tests" ON)
option(KRKRVITA_ENABLE_YURI_ENGINE "Compile the in-progress Yuri engine adapter" OFF)

file(GLOB KRKRVITA_YURI_TJS_SOURCES CONFIGURE_DEPENDS
    vendor/yuri/src/core/tjs2/*.cpp
)
add_library(krkrvita-yuri-tjs STATIC
    ${KRKRVITA_YURI_TJS_SOURCES}
    src/yuri/tjs_platform.cpp
)
set_target_properties(krkrvita-yuri-tjs PROPERTIES CXX_STANDARD 17)
target_include_directories(krkrvita-yuri-tjs PUBLIC
    src/yuri/compat
    vendor/yuri/src/core/tjs2
    vendor/yuri/src/core/environ
    vendor/yuri/src/core/utils
)
target_compile_definitions(krkrvita-yuri-tjs PUBLIC
    TJS_TEXT_OUT_CRLF
    USE_UNICODE_FSTRING
)
target_compile_options(krkrvita-yuri-tjs PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/src/yuri/tjs_compat.hpp
    -Wno-deprecated-declarations
    -Wno-implicit-fallthrough
)

set(KRKRVITA_COMMON_SOURCES
    src/common/bubble.cpp
    src/common/filter_heuristic.cpp
    src/common/game.cpp
    src/common/patch_manifest.cpp
    src/common/patch_repository.cpp
    src/common/pe_resources.cpp
    src/common/png.cpp
    src/common/profile.cpp
    src/common/retail_filter.cpp
    src/common/sfo.cpp
    src/common/sha256.cpp
    src/common/storage.cpp
    src/common/text_codec.cpp
    src/common/xp3_archive.cpp
    src/yuri/xp3_filter_vm.cpp
)

add_library(krkrvita-common STATIC ${KRKRVITA_COMMON_SOURCES})
target_include_directories(krkrvita-common PUBLIC include)
target_compile_options(krkrvita-common PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(krkrvita-common PUBLIC krkrvita-yuri-tjs)

if(VITA)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    target_link_libraries(krkrvita-yuri-tjs PUBLIC onigmo)
    target_compile_definitions(krkrvita-common PUBLIC KRKRVITA_VITA=1 CURL_STATICLIB)
    target_link_libraries(krkrvita-common PUBLIC png16 curl ssl crypto z zstd)

    add_executable(krkrvita
        src/vita/main.cpp
        src/vita/audio.cpp
        src/vita/bubble.cpp
        src/vita/input.cpp
        src/vita/renderer.cpp
        src/vita/yuri_runtime.cpp
    )
    target_link_libraries(krkrvita PRIVATE
        krkrvita-common
        vitaGL vitashark SceShaccCgExt SceShaccCg_stub taihen_stub mathneon openal
        SceAppMgr_stub SceAppUtil_stub SceAudio_stub SceAudioIn_stub SceCommonDialog_stub
        SceCtrl_stub SceDisplay_stub SceGxm_stub SceIofilemgr_stub
        SceKernelDmacMgr_stub SceNet_stub SceNetCtl_stub ScePower_stub
        SceProcessmgr_stub ScePromoterUtil_stub ScePvf_stub SceSysmodule_stub SceTouch_stub
        m pthread
    )
    vita_create_self(krkrvita.self krkrvita UNSAFE)

    add_executable(krkrvita-booter src/vita/booter.cpp)
    target_link_libraries(krkrvita-booter PRIVATE
        SceAppMgr_stub SceIofilemgr_stub SceLibKernel_stub SceProcessmgr_stub)
    vita_create_self(krkrvita-booter.self krkrvita-booter UNSAFE)

    vita_create_vpk(krkrvita.vpk KRVITA001 krkrvita.self
        VERSION 01.00
        NAME "Kirikiri Vita"
        FILE resources/vita/sce_sys/icon0.png sce_sys/icon0.png
        FILE resources/vita/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
        FILE resources/vita/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
        FILE resources/vita/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
        FILE ${CMAKE_CURRENT_BINARY_DIR}/krkrvita-booter.self bubble/eboot.bin
        FILE resources/vita/bubble/template.xml bubble/sce_sys/livearea/contents/template.xml
        FILE resources/vita/bubble/bg0.png bubble/sce_sys/livearea/contents/bg0.png
        FILE resources/vita/bubble/startup.png bubble/sce_sys/livearea/contents/startup.png
    )
else()
    target_compile_definitions(krkrvita-yuri-tjs PUBLIC TJS_NO_REGEXP)
    find_package(CURL REQUIRED)
    find_package(PNG REQUIRED)
    find_package(ZLIB REQUIRED)
    target_link_libraries(krkrvita-common PUBLIC CURL::libcurl PNG::PNG ZLIB::ZLIB)

    add_executable(krkrvita-tool src/tool/main.cpp)
    target_link_libraries(krkrvita-tool PRIVATE krkrvita-common)

    if(KRKRVITA_BUILD_TESTS)
        enable_testing()
        add_executable(krkrvita-tests tests/test_main.cpp)
        target_link_libraries(krkrvita-tests PRIVATE krkrvita-common)
        add_test(NAME krkrvita-tests COMMAND krkrvita-tests)
    endif()
endif()

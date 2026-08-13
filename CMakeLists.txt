cmake_minimum_required(VERSION 3.20)
project(krkrvita VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KRKRVITA_BUILD_TESTS "Build host-side tests" ON)

file(GLOB KRKRVITA_YURI_TJS_SOURCES CONFIGURE_DEPENDS
    vendor/yuri/src/core/tjs2/*.cpp
)

# Yuri's bundled UTF-16 formatter treats a va_list function parameter as a
# pointer to the list. That happens to work on its original targets, but is a
# pointer-to-pointer on x86_64 SysV and crashes while reporting script errors.
# Generate a host-only translation unit with the standards-compliant va_copy.
if(NOT VITA)
    set(KRKRVITA_TJS_CONFIG_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/yuri/src/core/tjs2/tjsConfig.cpp")
    file(READ "${KRKRVITA_TJS_CONFIG_SOURCE}" KRKRVITA_TJS_CONFIG_TEXT)
    set(KRKRVITA_BAD_VA_LIST
        "va_list *pap = (va_list *)&ap;\n    r = printf_core(&f, fmt, pap, nl_arg, nl_type);")
    set(KRKRVITA_FIXED_VA_LIST
        "va_list copied;\n    va_copy(copied, ap);\n    r = printf_core(&f, fmt, &copied, nl_arg, nl_type);\n    va_end(copied);")
    string(REPLACE "${KRKRVITA_BAD_VA_LIST}" "${KRKRVITA_FIXED_VA_LIST}"
        KRKRVITA_TJS_CONFIG_PATCHED "${KRKRVITA_TJS_CONFIG_TEXT}")
    if(KRKRVITA_TJS_CONFIG_PATCHED STREQUAL KRKRVITA_TJS_CONFIG_TEXT)
        message(FATAL_ERROR "Yuri tjsConfig va_list patch no longer applies")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/tjsConfig.cpp"
        "${KRKRVITA_TJS_CONFIG_PATCHED}")
    list(REMOVE_ITEM KRKRVITA_YURI_TJS_SOURCES "${KRKRVITA_TJS_CONFIG_SOURCE}")
    list(APPEND KRKRVITA_YURI_TJS_SOURCES
        "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/tjsConfig.cpp")
endif()

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
    message(FATAL_ERROR
        "The obsolete partial Vita runtime was removed. Use scripts/build-vita.sh "
        "to build the wholesale engine port.")
else()
    # A matching header/library pair matters here: Ruby's private Onigmo
    # header is not ABI-compatible with a distribution libonig. Pinning the
    # small host dependency also makes regex-enabled script probes repeatable.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(BUILD_TEST OFF CACHE BOOL "Build Oniguruma tests" FORCE)
    set(INSTALL_DOCUMENTATION OFF CACHE BOOL "Install Oniguruma docs" FORCE)
    set(INSTALL_EXAMPLES OFF CACHE BOOL "Install Oniguruma examples" FORCE)
    add_subdirectory(vendor/oniguruma EXCLUDE_FROM_ALL)
    target_compile_definitions(krkrvita-yuri-tjs PUBLIC
        KRKRVITA_USE_ONIGURUMA=1)
    target_link_libraries(krkrvita-yuri-tjs PUBLIC onig)
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

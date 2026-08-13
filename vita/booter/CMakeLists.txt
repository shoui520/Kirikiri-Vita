cmake_minimum_required(VERSION 3.20)
project(krkrvita-booter LANGUAGES C CXX)

if(NOT VITA)
    message(FATAL_ERROR "Configure with the VitaSDK toolchain")
endif()
if(NOT DEFINED KRKRVITA_ROOT)
    get_filename_component(KRKRVITA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

add_executable(krkrvita-booter
    "${KRKRVITA_ROOT}/src/vita/booter.cpp"
)
target_compile_features(krkrvita-booter PRIVATE cxx_std_14)
target_compile_options(krkrvita-booter PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(krkrvita-booter
    SceAppMgr_stub
    SceProcessmgr_stub
)

vita_create_self(krkrvita-booter.self krkrvita-booter UNSAFE)

set(KRKRVITA_TEMPLATE "${CMAKE_CURRENT_BINARY_DIR}/template")
add_custom_command(
    OUTPUT "${KRKRVITA_TEMPLATE}/eboot.bin"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${KRKRVITA_TEMPLATE}/sce_sys/livearea/contents"
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${CMAKE_CURRENT_BINARY_DIR}/krkrvita-booter.self"
        "${KRKRVITA_TEMPLATE}/eboot.bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${KRKRVITA_ROOT}/resources/vita/bubble"
        "${KRKRVITA_TEMPLATE}/sce_sys/livearea/contents"
    DEPENDS krkrvita-booter.self-self
            "${KRKRVITA_ROOT}/resources/vita/bubble/template.xml"
            "${KRKRVITA_ROOT}/resources/vita/bubble/bg0.png"
            "${KRKRVITA_ROOT}/resources/vita/bubble/startup.png"
    VERBATIM
)
add_custom_target(krkrvita-booter-template ALL
    DEPENDS "${KRKRVITA_TEMPLATE}/eboot.bin")

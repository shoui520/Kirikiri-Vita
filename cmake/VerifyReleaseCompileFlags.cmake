if(NOT DEFINED COMPILE_COMMANDS OR NOT EXISTS "${COMPILE_COMMANDS}")
    message(FATAL_ERROR "Release flag check has no compile_commands.json")
endif()
if(NOT DEFINED BUILD_TYPE OR NOT BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "Vita packages must be configured with CMAKE_BUILD_TYPE=Release")
endif()

file(READ "${COMPILE_COMMANDS}" compile_commands)
string(JSON command_count LENGTH "${compile_commands}")
math(EXPR command_last "${command_count} - 1")

foreach(command_index RANGE 0 ${command_last})
    string(JSON source GET "${compile_commands}" ${command_index} file)
    string(JSON command GET "${compile_commands}" ${command_index} command)

    foreach(forbidden_flag
            " -g "
            " -g0 "
            " -g1 "
            " -g2 "
            " -g3 "
            " -ggdb"
            " -O0 "
            " -pg "
            " -fprofile"
            " -fsanitize="
            " -DDEBUG"
            " -D_DEBUG")
        string(FIND " ${command} " "${forbidden_flag}" forbidden_offset)
        if(NOT forbidden_offset LESS 0)
            message(FATAL_ERROR
                "Debug, sanitizer, or profiler flag '${forbidden_flag}' in ${source}")
        endif()
    endforeach()

    string(FIND " ${command} " " -DNDEBUG " ndebug_offset)
    if(ndebug_offset LESS 0)
        message(FATAL_ERROR "Release compile command lacks -DNDEBUG: ${source}")
    endif()
    if(NOT command MATCHES "(^| )-O(2|3|s|z)( |$)")
        message(FATAL_ERROR "Release compile command lacks optimization: ${source}")
    endif()
endforeach()

message(STATUS "Vita compile commands contain only optimized release flags")

foreach(required TOOL GAME_DIR FILTER FFMPEG WORK_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Retail audio check is missing ${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(extract_and_decode archive entry output seconds)
    execute_process(
        COMMAND "${TOOL}" xp3-extract "${GAME_DIR}/${archive}" "${entry}"
            "${WORK_DIR}/${output}" "${FILTER}"
        RESULT_VARIABLE extract_result
        OUTPUT_VARIABLE extract_output
        ERROR_VARIABLE extract_error)
    if(NOT extract_result EQUAL 0)
        message(FATAL_ERROR
            "Cannot decrypt retail audio ${entry}: ${extract_output}${extract_error}")
    endif()
    execute_process(
        COMMAND "${FFMPEG}" -nostdin -v error -t "${seconds}"
            -i "${WORK_DIR}/${output}" -f null -
        RESULT_VARIABLE decode_result
        OUTPUT_VARIABLE decode_output
        ERROR_VARIABLE decode_error)
    if(NOT decode_result EQUAL 0)
        message(FATAL_ERROR
            "Cannot decode retail audio ${entry}: ${decode_output}${decode_error}")
    endif()
endfunction()

# Exercise both long-form music from data.xp3 and the separate encrypted voice
# archive. These are exactly the two audio paths the milestone game needs.
extract_and_decode("data.xp3" "bgm/bgm01.ogg" "bgm01.ogg" 5)
extract_and_decode("voice.xp3" "a0001.ogg" "a0001.ogg" 30)

file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "Retail BGM and voice decrypted and decoded")

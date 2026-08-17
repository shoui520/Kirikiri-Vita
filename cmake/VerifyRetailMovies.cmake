if(NOT DEFINED TOOL OR NOT EXISTS "${TOOL}")
    message(FATAL_ERROR "Retail movie check has no extraction tool")
endif()
if(NOT DEFINED FFMPEG OR NOT EXISTS "${FFMPEG}")
    message(FATAL_ERROR "Retail movie check has no FFmpeg executable")
endif()
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
    message(FATAL_ERROR "Retail movie check has no bounded work directory")
endif()

foreach(required_path
        CHICHIMIKO_ARCHIVE PL0002_ARCHIVE LPK30008_ARCHIVE)
    if(NOT DEFINED ${required_path} OR NOT EXISTS "${${required_path}}")
        message(FATAL_ERROR
            "Retail movie check is missing ${required_path}: ${${required_path}}")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(extract_movie archive entry output)
    execute_process(
        COMMAND "${TOOL}" xp3-extract "${archive}" "${entry}" "${output}"
        RESULT_VARIABLE extract_result
        OUTPUT_VARIABLE extract_output
        ERROR_VARIABLE extract_error)
    if(NOT extract_result EQUAL 0)
        message(FATAL_ERROR
            "Cannot extract retail movie ${archive}>${entry}:\n"
            "${extract_output}${extract_error}")
    endif()
endfunction()

function(require_movie_decode movie expected_video expected_audio)
    execute_process(
        COMMAND "${FFMPEG}" -nostdin -hide_banner -loglevel error -xerror
            -threads 1 -i "${movie}" -map 0:v:0 -frames:v 1
            -pix_fmt rgba -f md5 -
        RESULT_VARIABLE video_result
        OUTPUT_VARIABLE video_md5
        ERROR_VARIABLE video_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT video_result EQUAL 0 OR
       NOT video_md5 STREQUAL "MD5=${expected_video}")
        message(FATAL_ERROR
            "Retail movie video decode contract failed for ${movie}:\n"
            "expected MD5=${expected_video}\n"
            "actual ${video_md5}\n${video_error}")
    endif()

    execute_process(
        COMMAND "${FFMPEG}" -nostdin -hide_banner -loglevel error -xerror
            -threads 1 -i "${movie}" -map 0:a:0 -t 0.25
            -f s16le -ac 2 -ar 48000 -f md5 -
        RESULT_VARIABLE audio_result
        OUTPUT_VARIABLE audio_md5
        ERROR_VARIABLE audio_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT audio_result EQUAL 0 OR
       NOT audio_md5 STREQUAL "MD5=${expected_audio}")
        message(FATAL_ERROR
            "Retail movie audio decode contract failed for ${movie}:\n"
            "expected MD5=${expected_audio}\n"
            "actual ${audio_md5}\n${audio_error}")
    endif()
endfunction()

set(chichimiko_movie "${WORK_DIR}/chichimiko.mpg")
set(pl0002_movie "${WORK_DIR}/pl0002.mpg")
set(lpk30008_movie "${WORK_DIR}/lpk30008.wmv")
extract_movie("${CHICHIMIKO_ARCHIVE}" "ccm.mpg" "${chichimiko_movie}")
extract_movie("${PL0002_ARCHIVE}" "video/PL-0002_OP.mpg" "${pl0002_movie}")
extract_movie("${LPK30008_ARCHIVE}" "others/a_ev009a.wmv" "${lpk30008_movie}")

# The archive fingerprints are pinned by the 19-title manifest. These hashes
# additionally prove that Phase 1 extraction yields real MPEG-1/MP2 and
# WMV3/WMA2 payloads which decode into stable RGBA and stereo PCM output.
require_movie_decode("${chichimiko_movie}"
    "b83b1f9fd5fa2f639d3e659f84697ccc"
    "750cd33dc75035f61b1404e9ff8f446d")
require_movie_decode("${pl0002_movie}"
    "498e23c82ca78a4bcd7ba88e5243b6fc"
    "969c714e885b9e74253259f1bfe6bddf")
require_movie_decode("${lpk30008_movie}"
    "85de948027c496e06e4e9c530ccb93db"
    "783ee3e1b8916c827344c477b1410387")

file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "Retail MPEG/WMV decode contracts passed")

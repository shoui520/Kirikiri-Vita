if(NOT DEFINED VPK OR NOT EXISTS "${VPK}")
    message(FATAL_ERROR "VPK contract check has no package")
endif()
if(NOT DEFINED VERIFY_DIR OR VERIFY_DIR STREQUAL "")
    message(FATAL_ERROR "VPK contract check has no extraction directory")
endif()

file(REMOVE_RECURSE "${VERIFY_DIR}")
file(MAKE_DIRECTORY "${VERIFY_DIR}")
file(ARCHIVE_EXTRACT INPUT "${VPK}" DESTINATION "${VERIFY_DIR}")

set(TTC_SCAN_ROOT "${VERIFY_DIR}")
set(TTC_SCAN_CONTEXT "VPK")
include("${CMAKE_CURRENT_LIST_DIR}/VerifyNoTtcPayload.cmake")
unset(TTC_SCAN_ROOT)
unset(TTC_SCAN_CONTEXT)

foreach(required_file
    "eboot.bin"
    "sce_sys/param.sfo"
    "sce_sys/icon0.png"
    "sce_sys/livearea/contents/bg0.png"
    "sce_sys/livearea/contents/startup.png"
    "sce_sys/livearea/contents/template.xml"
    "krkrvita/default-xp3filter.tjs"
    "krkrvita/retail-after-startup.tjs"
    "krkrvita/patches/alldata.js"
    "krkrvita/patches/patches.zip")
    if(NOT EXISTS "${VERIFY_DIR}/${required_file}")
        message(FATAL_ERROR "VPK is missing ${required_file}")
    endif()
endforeach()

file(READ "${VERIFY_DIR}/krkrvita/retail-after-startup.tjs"
    retail_after_startup_source)
foreach(required_guard
    "System.platformName === \"PlayStation Vita\""
    "typeof System.checkAppId !== \"undefined\""
    "System.checkAppId === \"c1ce5e17-e483-4195-a8f7-a019f96c6805\""
    "global.kag instanceof \"KrkrzAddMainWindow\""
    "global.kag.menutimer.enabled = false"
    "krkrvita-vita-menubar-timer-disabled")
    string(FIND "${retail_after_startup_source}" "${required_guard}"
        required_guard_offset)
    if(required_guard_offset LESS 0)
        message(FATAL_ERROR
            "VPK retail post-startup adaptation is missing: ${required_guard}")
    endif()
endforeach()

file(SIZE "${VERIFY_DIR}/eboot.bin" eboot_size)
if(eboot_size LESS 1000000)
    message(FATAL_ERROR "VPK eboot.bin is unexpectedly small")
endif()

# Resolve the SELF app-info offset instead of assuming a fixed header size.
# Safe homebrew cannot access the shader compiler installed on ur0:.
file(READ "${VERIFY_DIR}/eboot.bin" self_magic OFFSET 0 LIMIT 4 HEX)
if(NOT self_magic STREQUAL "53434500")
    message(FATAL_ERROR "VPK eboot.bin is not a SELF executable")
endif()
file(READ "${VERIFY_DIR}/eboot.bin" appinfo_offset_hex
    OFFSET 56 LIMIT 8 HEX)
set(appinfo_offset 0)
foreach(byte_index RANGE 0 7)
    math(EXPR hex_offset "${byte_index} * 2")
    string(SUBSTRING "${appinfo_offset_hex}" ${hex_offset} 2 byte_hex)
    math(EXPR appinfo_offset
        "${appinfo_offset} | (0x${byte_hex} << (${byte_index} * 8))")
endforeach()
math(EXPR last_appinfo_offset "${eboot_size} - 8")
if(appinfo_offset LESS 128 OR appinfo_offset GREATER last_appinfo_offset)
    message(FATAL_ERROR "VPK SELF app-info offset is invalid")
endif()
file(READ "${VERIFY_DIR}/eboot.bin" self_authid
    OFFSET ${appinfo_offset} LIMIT 8 HEX)
if(NOT self_authid STREQUAL "010000000000002f")
    message(FATAL_ERROR
        "VPK must use UNSAFE homebrew permissions for ur0: shader access")
endif()
file(SIZE "${VERIFY_DIR}/krkrvita/patches/patches.zip" patch_bundle_size)
if(patch_bundle_size LESS 10000000)
    message(FATAL_ERROR "Embedded retail patch bundle is unexpectedly small")
endif()

file(READ "${VERIFY_DIR}/sce_sys/param.sfo" sfo_hex HEX)
string(TOLOWER "${sfo_hex}" sfo_hex)
string(FIND "${sfo_hex}" "4b5256495441303031" title_id_offset)
if(title_id_offset LESS 0)
    message(FATAL_ERROR "VPK PARAM.SFO does not contain legal title ID KRVITA001")
endif()

# PARAM.SFO is a PSF table with little-endian index entries. Merely finding
# the ATTRIBUTE2 key string would not prove that Sony will grant the expanded
# application-memory budget, so resolve the key's index and check its DWORD.
function(read_sfo_le16 byte_offset output)
    math(EXPR char_offset "${byte_offset} * 2")
    string(SUBSTRING "${sfo_hex}" ${char_offset} 4 encoded)
    string(SUBSTRING "${encoded}" 0 2 byte0)
    string(SUBSTRING "${encoded}" 2 2 byte1)
    math(EXPR decoded "0x${byte1}${byte0}")
    set(${output} ${decoded} PARENT_SCOPE)
endfunction()

function(read_sfo_le32 byte_offset output)
    math(EXPR char_offset "${byte_offset} * 2")
    string(SUBSTRING "${sfo_hex}" ${char_offset} 8 encoded)
    string(SUBSTRING "${encoded}" 0 2 byte0)
    string(SUBSTRING "${encoded}" 2 2 byte1)
    string(SUBSTRING "${encoded}" 4 2 byte2)
    string(SUBSTRING "${encoded}" 6 2 byte3)
    math(EXPR decoded "0x${byte3}${byte2}${byte1}${byte0}")
    set(${output} ${decoded} PARENT_SCOPE)
endfunction()

read_sfo_le32(8 sfo_key_table_offset)
read_sfo_le32(12 sfo_data_table_offset)
read_sfo_le32(16 sfo_entry_count)
set(attribute2_verified FALSE)
if(sfo_entry_count GREATER 0)
    math(EXPR sfo_last_entry "${sfo_entry_count} - 1")
    foreach(entry RANGE 0 ${sfo_last_entry})
        math(EXPR entry_offset "20 + ${entry} * 16")
        read_sfo_le16(${entry_offset} key_offset)
        math(EXPR key_char_offset
            "(${sfo_key_table_offset} + ${key_offset}) * 2")
        string(SUBSTRING "${sfo_hex}" ${key_char_offset} 20 key_hex)
        if(key_hex STREQUAL "41545452494255544532")
            math(EXPR format_offset "${entry_offset} + 2")
            math(EXPR length_offset "${entry_offset} + 4")
            math(EXPR data_offset_field "${entry_offset} + 12")
            read_sfo_le16(${format_offset} attribute2_format)
            read_sfo_le32(${length_offset} attribute2_length)
            read_sfo_le32(${data_offset_field} attribute2_data_offset)
            math(EXPR attribute2_value_offset
                "${sfo_data_table_offset} + ${attribute2_data_offset}")
            read_sfo_le32(${attribute2_value_offset} attribute2_value)
            if(NOT attribute2_format EQUAL 1028 OR
               NOT attribute2_length EQUAL 4 OR
               NOT attribute2_value EQUAL 12)
                message(FATAL_ERROR
                    "VPK PARAM.SFO ATTRIBUTE2 is not the DWORD value 12")
            endif()
            set(attribute2_verified TRUE)
        endif()
    endforeach()
endif()
if(NOT attribute2_verified)
    message(FATAL_ERROR
        "VPK PARAM.SFO does not request Sony's ATTRIBUTE2=12 memory budget")
endif()

set(patch_check_dir "${VERIFY_DIR}/patch-check")
file(MAKE_DIRECTORY "${patch_check_dir}")
file(ARCHIVE_EXTRACT
    INPUT "${VERIFY_DIR}/krkrvita/patches/patches.zip"
    DESTINATION "${patch_check_dir}")
set(TTC_SCAN_ROOT "${patch_check_dir}")
set(TTC_SCAN_CONTEXT "Embedded retail patch bundle")
include("${CMAKE_CURRENT_LIST_DIR}/VerifyNoTtcPayload.cmake")
unset(TTC_SCAN_ROOT)
unset(TTC_SCAN_CONTEXT)
file(GLOB_RECURSE bundled_patch_scripts LIST_DIRECTORIES FALSE
    "${patch_check_dir}/patch/*.tjs")
set(has_patch_script FALSE)
set(has_filter_script FALSE)
foreach(bundled_patch_script IN LISTS bundled_patch_scripts)
    get_filename_component(bundled_patch_name
        "${bundled_patch_script}" NAME)
    if(bundled_patch_name STREQUAL "patch.tjs")
        set(has_patch_script TRUE)
    elseif(bundled_patch_name STREQUAL "xp3filter.tjs")
        set(has_filter_script TRUE)
    endif()
endforeach()
if(NOT has_patch_script OR NOT has_filter_script)
    message(FATAL_ERROR
        "Embedded patch bundle lacks required patch and filter scripts")
endif()

file(REMOVE_RECURSE "${VERIFY_DIR}")
message(STATUS
    "VPK identity, UNSAFE permissions, ATTRIBUTE2=12, assets, and retail-patch contracts passed")

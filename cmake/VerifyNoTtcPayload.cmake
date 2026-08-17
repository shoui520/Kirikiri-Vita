if(NOT DEFINED TTC_SCAN_ROOT OR NOT IS_DIRECTORY "${TTC_SCAN_ROOT}")
    message(FATAL_ERROR "TTC packaging guard has no directory to scan")
endif()
if(NOT DEFINED TTC_SCAN_CONTEXT OR TTC_SCAN_CONTEXT STREQUAL "")
    set(TTC_SCAN_CONTEXT "Package payload")
endif()

# Scan the extracted payload instead of trusting the packaging manifest: this
# catches additions made by vita_create_vpk itself, future FILE entries, and
# mixed-case extensions. The external MS Gothic collection is a personal
# runtime dependency at ux0:data/krkrvita/msgothic.ttc, never VPK content.
file(GLOB_RECURSE ttc_payload_entries
    LIST_DIRECTORIES FALSE
    RELATIVE "${TTC_SCAN_ROOT}"
    "${TTC_SCAN_ROOT}/*")
foreach(ttc_payload_entry IN LISTS ttc_payload_entries)
    string(TOLOWER "${ttc_payload_entry}" ttc_payload_entry_lower)
    if(ttc_payload_entry_lower MATCHES "\\.ttc$")
        message(FATAL_ERROR
            "${TTC_SCAN_CONTEXT} contains prohibited TTC payload: "
            "${ttc_payload_entry}. Install the required personal font "
            "externally at ux0:data/krkrvita/msgothic.ttc")
    endif()
endforeach()

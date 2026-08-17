# Keep the three places a compatibility claim can be made in agreement.
#
# The recurring failure has been prose and the executable gate disagreeing:
# docs called a title compatible while the manifest still said phase1 and the
# only real evidence — a physical Vita run — said it was broken. Nothing here
# runs a game; it checks that no file claims more than the evidence supports.
#
# Required: -DMANIFEST=, -DEVIDENCE=, -DGATE_DOC=

cmake_minimum_required(VERSION 3.19)

foreach(required MANIFEST EVIDENCE GATE_DOC)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "VerifyCompatibilityClaims: -D${required} is required")
    endif()
    if(NOT EXISTS "${${required}}")
        message(FATAL_ERROR "VerifyCompatibilityClaims: missing ${required}: ${${required}}")
    endif()
endforeach()

# Manifest state -> the label the documentation table must use.
set(state_labels
    "phase1=host-audit passed"
    "runtime_blocked=runtime-blocked"
    "phase2=phase2"
    "hardware_blocked=hardware-blocked")

function(state_label state out)
    foreach(pair IN LISTS state_labels)
        string(FIND "${pair}" "=" separator)
        string(SUBSTRING "${pair}" 0 ${separator} key)
        math(EXPR value_start "${separator} + 1")
        string(SUBSTRING "${pair}" ${value_start} -1 value)
        if(state STREQUAL key)
            set(${out} "${value}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out} "" PARENT_SCOPE)
endfunction()

# --- hardware receipts -------------------------------------------------------
file(STRINGS "${EVIDENCE}" evidence_rows ENCODING UTF-8)
set(receipt_ids)
set(receipt_states)
foreach(row IN LISTS evidence_rows)
    if(row MATCHES "^#" OR row STREQUAL "")
        continue()
    endif()
    # Parse by regex, not list(): a symptom may legitimately contain ";",
    # which CMake would otherwise treat as a field separator.
    if(NOT row MATCHES "^([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|(.*)$")
        message(FATAL_ERROR "Malformed hardware evidence row: ${row}")
    endif()
    set(receipt_id "${CMAKE_MATCH_1}")
    set(receipt_hash "${CMAKE_MATCH_2}")
    set(receipt_status "${CMAKE_MATCH_4}")
    set(receipt_symptom "${CMAKE_MATCH_5}")
    if(NOT receipt_status MATCHES "^(passed|blocked)$")
        message(FATAL_ERROR
            "Hardware evidence status must be passed or blocked: ${row}")
    endif()
    # CMake regexes have no bounded repetition, so check the length separately.
    string(LENGTH "${receipt_hash}" receipt_hash_length)
    if(NOT (receipt_hash STREQUAL "-" OR
            (receipt_hash_length EQUAL 64 AND
             receipt_hash MATCHES "^[0-9a-f]+$")))
        message(FATAL_ERROR
            "Hardware evidence needs a VPK SHA-256 or '-': ${row}")
    endif()
    # A receipt with no symptom cannot be acted on later.
    if(receipt_status STREQUAL "blocked" AND receipt_symptom STREQUAL "")
        message(FATAL_ERROR "Blocked hardware receipt has no symptom: ${row}")
    endif()
    # A passing receipt with no hash would let any future build inherit it.
    if(receipt_status STREQUAL "passed" AND receipt_hash STREQUAL "-")
        message(FATAL_ERROR
            "A passing hardware receipt must name the exact VPK hash: ${row}")
    endif()
    if(receipt_id IN_LIST receipt_ids)
        message(FATAL_ERROR "Duplicate hardware receipt for ${receipt_id}")
    endif()
    list(APPEND receipt_ids "${receipt_id}")
    list(APPEND receipt_states "${receipt_id}=${receipt_status}")
endforeach()

function(receipt_status_of id out)
    foreach(pair IN LISTS receipt_states)
        if(pair STREQUAL "${id}=passed")
            set(${out} "passed" PARENT_SCOPE)
            return()
        elseif(pair STREQUAL "${id}=blocked")
            set(${out} "blocked" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out} "" PARENT_SCOPE)
endfunction()

# --- manifest ----------------------------------------------------------------
file(READ "${GATE_DOC}" gate_doc ENCODING UTF-8)
file(STRINGS "${MANIFEST}" manifest_rows ENCODING UTF-8)
set(manifest_ids)
foreach(row IN LISTS manifest_rows)
    if(row MATCHES "^#" OR row STREQUAL "")
        continue()
    endif()
    if(NOT row MATCHES "^([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|(.*)$")
        message(FATAL_ERROR "Malformed manifest row: ${row}")
    endif()
    set(id "${CMAKE_MATCH_1}")
    set(state "${CMAKE_MATCH_4}")
    list(APPEND manifest_ids "${id}")

    state_label("${state}" label)
    if(label STREQUAL "")
        message(FATAL_ERROR "Unknown manifest state for ${id}: ${state}")
    endif()

    receipt_status_of("${id}" receipt)

    # A passing physical run is the only thing that earns a stronger word than
    # "host-audit passed", and it only applies on top of a clean host audit.
    if(receipt STREQUAL "passed")
        if(NOT state STREQUAL "phase1")
            message(FATAL_ERROR
                "${id} has a passing physical-Vita receipt but the manifest "
                "says '${state}'. A passing receipt requires a clean host audit.")
        endif()
        set(label "hardware verified")
    endif()

    # A recorded hardware failure outranks every host result.
    if(receipt STREQUAL "blocked" AND NOT state STREQUAL "hardware_blocked")
        message(FATAL_ERROR
            "${id} has a blocked physical-Vita receipt but the manifest says "
            "'${state}'. Move the row to hardware_blocked or retire the receipt.")
    endif()
    if(state STREQUAL "hardware_blocked" AND NOT receipt STREQUAL "blocked")
        message(FATAL_ERROR
            "${id} is hardware_blocked with no blocked receipt in ${EVIDENCE}")
    endif()

    # The documentation table must carry the same state, keyed by manifest id.
    if(NOT gate_doc MATCHES "\n\\| ${label} \\| ${id} \\|")
        message(FATAL_ERROR
            "${GATE_DOC} does not list ${id} as '${label}'")
    endif()
endforeach()

foreach(id IN LISTS receipt_ids)
    if(NOT id IN_LIST manifest_ids)
        message(FATAL_ERROR
            "Hardware receipt ${id} is not a manifest title")
    endif()
endforeach()

# No title in this corpus has a passing physical run yet, so the word must not
# appear as a state claim anywhere in the gate documentation.
if(gate_doc MATCHES "\n\\| compatible[* ]*\\|")
    message(FATAL_ERROR
        "${GATE_DOC} still claims a 'compatible' state. Only a passing row in "
        "${EVIDENCE} can support that, and it must be spelled out per title.")
endif()

message(STATUS "compatibility claims agree across manifest, evidence and docs")

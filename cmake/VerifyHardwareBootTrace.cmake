if(NOT DEFINED TRACE OR NOT EXISTS "${TRACE}")
    message(FATAL_ERROR "Hardware boot-trace check requires -DTRACE=<boot-status.txt>")
endif()

file(STRINGS "${TRACE}" trace_lines)

function(require_marker marker)
    list(FIND trace_lines "${marker}" marker_index)
    if(marker_index LESS 0)
        message(FATAL_ERROR "Hardware run did not reach: ${marker}")
    endif()
endfunction()

function(require_before earlier later)
    list(FIND trace_lines "${earlier}" earlier_index)
    list(FIND trace_lines "${later}" later_index)
    if(earlier_index LESS 0 OR later_index LESS 0 OR
       NOT earlier_index LESS later_index)
        message(FATAL_ERROR
            "Hardware milestones are out of order: ${earlier} -> ${later}")
    endif()
endfunction()

foreach(failure_marker
    "fatal-error-written"
    "startup-script-exception"
    "yuri-thread-priority-apply-failed"
    "yuri-timer-thread-policy-failed"
    "vitagl-first-game-frame-invalid"
    "vitagl-first-game-frame-upload-failed"
    "vitagl-first-game-frame-draw-failed"
    "vitagl-first-game-frame-swap-failed")
    list(FIND trace_lines "${failure_marker}" failure_index)
    if(NOT failure_index LESS 0)
        message(FATAL_ERROR "Hardware run reported failure: ${failure_marker}")
    endif()
endforeach()

# These are behavioral gates, not generic initialization breadcrumbs. They
# prove the Vita executed synchronized worker threads, selected the retail
# decryptor, registered the game's native backends, completed its startup TJS,
# presented sustained game frames and actually started decoded audio.
set(required_markers
    "preinit-entered"
    "main-entered"
    "vita-newlib-heap-128m"
    "vita-main-thread-stack-2m"
    "vita-main-thread-policy-ready"
    "vita-threading-self-test-entered"
    "vita-threading-self-test-passed"
    "retail-xp3filter-selected"
    "retail-startup-patch-selected"
    "retail-launch-resolved"
    "vitagl-garbage-collector-configured"
    "vitagl-cached-ram-pool-enabled"
    "vitagl-swap-decoupled-from-engine-tick"
    "yuri-platform-ready"
    "yuri-start-application-entered"
    "retail-project-data-xp3-selected"
    "yuri-msgothic-primary-selected"
    "yuri-msgothic-freetype-rasterizer-selected"
    "yuri-msgothic-pread-stream-ready"
    "yuri-msgothic-pread-cache-ready"
    "yuri-msgothic-freetype-face-applied"
    "yuri-freetype-text-extent-complete"
    "yuri-msgothic-prepared-slot-reused"
    "yuri-freetype-first-glyph-rendered"
    "yuri-storage-preflight-entered"
    "yuri-project-directory-enumerated"
    "yuri-project-xp3-opened"
    "yuri-xp3filter-opened"
    "yuri-patch-opened"
    "yuri-storage-preflight-complete"
    "retail-extrans-ready"
    "retail-layerexsave-ready"
    "retail-windows-plugin-skipped"
    "yuri-startup-storage-preflight-entered"
    "yuri-startup-storage-opened"
    "yuri-startup-storage-preflight-complete"
    "yuri-startup-script-entered"
    "yuri-direct-texture-fallback"
    "yuri-bitmap-tiered-allocator-ready"
    "yuri-bitmap-user-rw-memblocks-ready"
    "yuri-software-static-texture-direct-ready"
    "yuri-render-tasks-hybrid-large"
    "yuri-render-task-pool-ready"
    "yuri-eager-layer-cache-release-enabled"
    "yuri-system-app-id-compat-ready"
    "yuri-timer-thread-entered"
    "yuri-timer-thread-policy-ready"
    "yuri-first-timer-fired"
    "yuri-first-timer-dispatched"
    "retail-vita-afterstartup-executed"
    "yuri-startup-script-complete"
    "yuri-first-window-created"
    "yuri-openal-initialized"
    "yuri-audio-first-buffer-queued"
    "yuri-audio-first-play-started"
    "yuri-software-framebuffer-ready"
    "vitagl-first-game-frame-presented"
    "vitagl-partial-frame-upload-ready"
    "vitagl-duplicate-frame-upload-skipped"
    "vitagl-60-game-frames-presented"
    "vitagl-cursor-overlay-presented"
    "vitagl-first-contentful-frame-presented"
    "yuri-start-application-returned"
    "yuri-event-loop-entered"
    "yuri-texture-recycler-drained"
    "retail-runtime-5s-stable-with-video"
    "retail-runtime-30s-stable-with-video")
foreach(marker IN LISTS required_markers)
    require_marker("${marker}")
endforeach()

set(ordered_markers
    "preinit-entered"
    "main-entered"
    "vita-newlib-heap-128m"
    "vita-main-thread-stack-2m"
    "vita-main-thread-policy-ready"
    "vita-threading-self-test-entered"
    "vita-threading-self-test-passed"
    "retail-xp3filter-selected"
    "retail-startup-patch-selected"
    "retail-launch-resolved"
    "vitagl-garbage-collector-configured"
    "vitagl-cached-ram-pool-enabled"
    "vitagl-swap-decoupled-from-engine-tick"
    "yuri-platform-ready"
    "yuri-start-application-entered"
    "retail-project-data-xp3-selected"
    "yuri-storage-preflight-entered"
    "yuri-project-directory-enumerated"
    "yuri-project-xp3-opened"
    "yuri-xp3filter-opened"
    "yuri-patch-opened"
    "yuri-storage-preflight-complete"
    "retail-windows-plugin-skipped"
    "yuri-startup-storage-preflight-entered"
    "yuri-startup-storage-opened"
    "yuri-startup-storage-preflight-complete"
    "yuri-startup-script-entered"
    "retail-vita-afterstartup-executed"
    "yuri-startup-script-complete"
    "yuri-start-application-returned"
    "yuri-event-loop-entered"
    "yuri-software-framebuffer-ready"
    "vitagl-60-game-frames-presented"
    "retail-runtime-5s-stable-with-video"
    "retail-runtime-30s-stable-with-video")
set(previous_index -1)
foreach(marker IN LISTS ordered_markers)
    list(FIND trace_lines "${marker}" marker_index)
    if(marker_index LESS_EQUAL previous_index)
        message(FATAL_ERROR "Hardware milestones are out of order at: ${marker}")
    endif()
    set(previous_index "${marker_index}")
endforeach()

require_before("yuri-timer-thread-entered"
    "yuri-timer-thread-policy-ready")
require_before("yuri-timer-thread-policy-ready"
    "yuri-first-timer-fired")

require_before("yuri-event-loop-entered"
               "yuri-software-framebuffer-ready")
require_before("retail-vita-afterstartup-executed"
               "yuri-startup-script-complete")
require_before("yuri-software-framebuffer-ready"
               "vitagl-first-game-frame-presented")
require_before("yuri-render-tasks-hybrid-large"
               "yuri-render-task-pool-ready")
# This title's first contentful output is a full 1280x960 background
# transition. The lazy-pool marker therefore proves that the large-only
# policy dispatched instead of merely being selected at initialization. A
# black bootstrap frame may precede it, so do not order this against the first
# software-framebuffer/presentation markers.
require_before("yuri-render-task-pool-ready"
               "vitagl-first-contentful-frame-presented")

message(STATUS "Vita retail hardware run passed all behavioral gates")

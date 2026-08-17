if(NOT DEFINED ELF OR NOT EXISTS "${ELF}")
    message(FATAL_ERROR "Vita ELF contract check has no executable")
endif()
if(NOT DEFINED NM OR NOT EXISTS "${NM}")
    message(FATAL_ERROR "Vita ELF contract check has no nm tool")
endif()

execute_process(
    COMMAND "${NM}" -C "${ELF}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE nm_error)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect Vita ELF symbols: ${nm_error}")
endif()

foreach(required_symbol
    "_newlib_heap_size_user"
    "sceUserMainThreadStackSize"
    "TVPSetXP3FilterScript"
    "FontEx::addFont"
    "TVPLoadPluigins()"
    "TVPSetXP3ArchiveExtractionFilter"
    "krkrvita::FilterHeuristic::analyze"
    "krkrvita::FilterRule::to_tjs"
    "krkrvita::Xp3Archive::open"
    "krkrvita::collect_xp3_filter_samples"
    "TVPCreateAndAddWindow"
    "TVPCreateNativeClass_MenuItem()"
    "TVPCreateNativeClass_KAGParser()"
    "TVPCreateSoundBuffer"
    "GetVideoOverlayObject"
    "GetVideoLayerObject"
    "krkrvita_vitagl_submit_video_frame"
    "avformat_open_input"
    "avcodec_find_decoder"
    "sws_scale"
    "swr_convert"
    "ff_mpegps_demuxer"
    "ff_asf_demuxer"
    "ff_mpeg1video_decoder"
    "ff_mpeg2video_decoder"
    "ff_mp2_decoder"
    "ff_wmv3_decoder"
    "ff_wmav2_decoder"
    "VorbisWaveDecoder::Render"
    "VitaOpenALSoundBuffer"
    "alSourcePlay"
    "alSourceRewind"
    "krkrvita_vitagl_present"
    "TVPGetSoftwareRenderManager()"
    "TVPFreeUnusedLayerCache"
    "tTVPMosaicTransHandler::Process"
    "tTVPTurnTransHandler::Process"
    "tTVPRippleTransHandler::Process"
    "tTVPRotateZoomTransHandler"
    "tTVPWaveTransHandler::Process"
    "tTJSNI_BaseLayer::SaveLayerImage"
    "saveLayerImagePngFunc"
    "saveLayerImageTlg5Func"
    "fstat_dirlist"
    "layerExImage::light"
    "layerExImage::colorize"
    "layerExImage::modulate"
    "layerExImage::noise"
    "layerExImage::generateWhiteNoise"
    "layerExImage::gaussianBlur"
    "ShrinkCopy::layerShrinkCopy"
    "LimitedShrink::layerShrinkCopy"
    "krkrvita_vita_threading_self_test()"
    "krkrvita_yuri_storage_preflight(TJS::tTJSString const&)"
    "krkrvita_yuri_select_project(TJS::tTJSString const&)"
    "krkrvita_yuri_startup_storage_preflight()"
    "tTVPThreadEvent::WaitFor"
    "pthread_cancel"
    "pthread_mutex_init"
    "vglInitExtended"
    "vglUseCachedMem"
    "vglSetupGarbageCollector"
    "vglSwapBuffers"
    "sceCtrlPeekBufferPositive"
    "sceTouchRead")
    string(FIND "${symbols}" "${required_symbol}" offset)
    if(offset LESS 0)
        message(FATAL_ERROR
            "Required runtime implementation was not linked: ${required_symbol}")
    endif()
endforeach()


# VitaSDK's weak default is a fixed 128 MiB heap, which cannot hold Yuri's
# retail engine graph for 1280x960 titles. The override must be a strong
# initialized datum in the executable, not another weak declaration.
string(REGEX MATCH "(^|\n)[0-9A-Fa-f]+ [Dd] _newlib_heap_size_user(\n|$)"
    strong_newlib_heap "${symbols}")
if(NOT strong_newlib_heap)
    message(FATAL_ERROR
        "Vita newlib heap override is not a strong initialized symbol")
endif()

# The retail error/render call chain overflowed VitaSDK's 256 KiB default on
# hardware. Like the heap override, this must be a strong initialized process
# parameter recognized by VitaSDK's startup code.
string(REGEX MATCH "(^|\n)[0-9A-Fa-f]+ [Dd] sceUserMainThreadStackSize(\n|$)"
    strong_main_stack "${symbols}")
if(NOT strong_main_stack)
    message(FATAL_ERROR
        "Vita main-thread stack override is not a strong initialized symbol")
endif()

# libstdc++ uses pthread_cancel as Vita's weak gthread-active proxy. It must be
# a strong definition in the final executable; a weak unresolved entry makes
# std::mutex silently skip initialization and caused the retail watch thread
# to fault in pthread_mutex_unlock on hardware.
string(REGEX MATCH "(^|\n)[0-9A-Fa-f]+ [Tt] pthread_cancel(\n|$)"
    strong_pthread_cancel "${symbols}")
if(NOT strong_pthread_cancel)
    message(FATAL_ERROR
        "Vita libpthread was not linked wholesale: pthread_cancel is not strong")
endif()

foreach(forbidden_symbol
    "SDL_"
    "pibInit"
    "pibTerm"
    "YuriVitaPvfFontRasterizer"
    "scePvf"
    "krkrvita_yuri_get_opengl_texture"
    "TVPRenderManager_OpenGL")
    string(FIND "${symbols}" "${forbidden_symbol}" offset)
    if(NOT offset LESS 0)
        message(FATAL_ERROR
            "Forbidden runtime leaked into Vita ELF: ${forbidden_symbol}")
    endif()
endforeach()

message(STATUS "Vita ELF retail/runtime contracts passed")

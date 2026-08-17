if(NOT DEFINED COMPILE_COMMANDS OR NOT EXISTS "${COMPILE_COMMANDS}")
    message(FATAL_ERROR "Yuri contract check has no compile_commands.json")
endif()
if(NOT DEFINED GENERATED_DIR OR NOT IS_DIRECTORY "${GENERATED_DIR}")
    message(FATAL_ERROR "Yuri contract check has no generated source directory")
endif()
if(NOT DEFINED SOURCE_DIR OR NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "Yuri contract check has no source directory")
endif()

function(require_text file needle description)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Missing generated Yuri source: ${file}")
    endif()
    file(READ "${file}" contents)
    string(FIND "${contents}" "${needle}" offset)
    if(offset LESS 0)
        message(FATAL_ERROR "Missing Yuri backend contract: ${description}")
    endif()
endfunction()

function(forbid_text file needle description)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Missing generated Yuri source: ${file}")
    endif()
    file(READ "${file}" contents)
    string(FIND "${contents}" "${needle}" offset)
    if(NOT offset LESS 0)
        message(FATAL_ERROR "Forbidden Yuri backend contract: ${description}")
    endif()
endfunction()

function(require_text_count file needle expected_count description)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Missing generated Yuri source: ${file}")
    endif()
    file(READ "${file}" contents)
    string(LENGTH "${needle}" needle_length)
    if(needle_length EQUAL 0)
        message(FATAL_ERROR "Empty Yuri backend contract needle: ${description}")
    endif()
    set(actual_count 0)
    set(searching "1")
    while("${searching}" STREQUAL "1")
        string(FIND "${contents}" "${needle}" offset)
        if(offset LESS 0)
            set(searching "0")
        else()
            math(EXPR actual_count "${actual_count} + 1")
            math(EXPR remainder_offset "${offset} + ${needle_length}")
            string(SUBSTRING "${contents}" ${remainder_offset} -1 contents)
        endif()
    endwhile()
    if(NOT actual_count EQUAL expected_count)
        message(FATAL_ERROR
            "Wrong Yuri backend contract count (${actual_count}, expected ${expected_count}): ${description}")
    endif()
endfunction()

function(forbid_text_between file begin_marker end_marker needle description)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Missing generated Yuri source: ${file}")
    endif()
    file(READ "${file}" contents)
    string(FIND "${contents}" "${begin_marker}" begin_offset)
    if(begin_offset LESS 0)
        message(FATAL_ERROR "Missing Yuri backend contract boundary: ${description}")
    endif()
    string(SUBSTRING "${contents}" ${begin_offset} -1 section_tail)
    string(FIND "${section_tail}" "${end_marker}" end_offset)
    if(end_offset LESS 0)
        message(FATAL_ERROR "Missing Yuri backend contract boundary: ${description}")
    endif()
    string(SUBSTRING "${section_tail}" 0 ${end_offset} section)
    string(FIND "${section}" "${needle}" offset)
    if(NOT offset LESS 0)
        message(FATAL_ERROR "Forbidden Yuri backend contract: ${description}")
    endif()
endfunction()

function(require_text_count_between file begin_marker end_marker needle
         expected_count description)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Missing generated Yuri source: ${file}")
    endif()
    file(READ "${file}" contents)
    string(FIND "${contents}" "${begin_marker}" begin_offset)
    if(begin_offset LESS 0)
        message(FATAL_ERROR "Missing Yuri backend contract boundary: ${description}")
    endif()
    string(SUBSTRING "${contents}" ${begin_offset} -1 section_tail)
    string(FIND "${section_tail}" "${end_marker}" end_offset)
    if(end_offset LESS 0)
        message(FATAL_ERROR "Missing Yuri backend contract boundary: ${description}")
    endif()
    string(SUBSTRING "${section_tail}" 0 ${end_offset} section)

    string(LENGTH "${needle}" needle_length)
    if(needle_length EQUAL 0)
        message(FATAL_ERROR "Empty Yuri backend contract needle: ${description}")
    endif()
    set(actual_count 0)
    set(searching "1")
    while("${searching}" STREQUAL "1")
        string(FIND "${section}" "${needle}" offset)
        if(offset LESS 0)
            set(searching "0")
        else()
            math(EXPR actual_count "${actual_count} + 1")
            math(EXPR remainder_offset "${offset} + ${needle_length}")
            string(SUBSTRING "${section}" ${remainder_offset} -1 section)
        endif()
    endwhile()
    if(NOT actual_count EQUAL expected_count)
        message(FATAL_ERROR
            "Wrong Yuri backend contract count (${actual_count}, expected ${expected_count}): ${description}")
    endif()
endfunction()

function(require_text_order_between file begin_marker end_marker first_needle
         second_needle description)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Missing generated Yuri source: ${file}")
    endif()
    file(READ "${file}" contents)
    string(FIND "${contents}" "${begin_marker}" begin_offset)
    if(begin_offset LESS 0)
        message(FATAL_ERROR "Missing Yuri backend contract boundary: ${description}")
    endif()
    string(SUBSTRING "${contents}" ${begin_offset} -1 section_tail)
    string(FIND "${section_tail}" "${end_marker}" end_offset)
    if(end_offset LESS 0)
        message(FATAL_ERROR "Missing Yuri backend contract boundary: ${description}")
    endif()
    string(SUBSTRING "${section_tail}" 0 ${end_offset} section)
    string(FIND "${section}" "${first_needle}" first_offset)
    string(FIND "${section}" "${second_needle}" second_offset)
    if(first_offset LESS 0 OR second_offset LESS 0 OR
       NOT first_offset LESS second_offset)
        message(FATAL_ERROR "Wrong Yuri backend contract order: ${description}")
    endif()
endfunction()

require_text("${GENERATED_DIR}/SysInitImpl.cpp"
    "TVPProgramArguments.push_back(TVPParseCommandLineOne(value))"
    "Vita argv reaches TVPProgramArguments")
require_text("${GENERATED_DIR}/SysInitImpl.cpp"
    "project_last != TJS_W('/')"
    "a directory project does not acquire duplicate path delimiters")
require_text("${GENERATED_DIR}/StorageImpl.cpp"
    "vita_storage_to_native_path"
    "Kirikiri storage URIs use the shared Vita native-path contract")
require_text("${GENERATED_DIR}/StorageImpl.cpp"
    "newname.GetLastChar() != TJS_W(':')"
    "Yuri never inserts a slash after a Vita device colon")
require_text("${GENERATED_DIR}/StorageImpl.cpp"
    "#include \"krkrvita/read_all.hpp\""
    "Vita local files use the documented short-read loop")
require_text("${GENERATED_DIR}/StorageImpl.cpp"
    "krkrvita::read_all_bytes"
    "Vita local-file reads join every positive partial sceIoRead")
require_text("${GENERATED_DIR}/XP3Archive.cpp"
    "#include \"krkrvita/read_all.hpp\""
    "XP3 archive payload reads use the documented short-read loop")
require_text("${GENERATED_DIR}/XP3Archive.cpp"
    "krkrvita::read_all_stream(*instream"
    "compressed XP3 segments are fully read before decompression")
require_text("${GENERATED_DIR}/XP3Archive.cpp"
    "krkrvita::read_all_stream(*Stream"
    "uncompressed XP3 segments are fully read before exposure")
forbid_text("${GENERATED_DIR}/XP3Archive.cpp"
    "instream->Read(indata, insize);"
    "XP3 compressed segments still perform a single unchecked read")
forbid_text("${GENERATED_DIR}/XP3Archive.cpp"
    "Stream->ReadBuffer((tjs_uint8*)buffer + write_size, one_size);"
    "XP3 uncompressed segments still perform a single unchecked read")
require_text("${GENERATED_DIR}/StorageImpl.cpp"
    "krkrvita::virtual_cd_is_present"
    "Vita searchCD treats the mounted project as a virtual disc")
require_text("${GENERATED_DIR}/StorageImpl.cpp"
    "yuri-virtual-cd-search-satisfied"
    "virtual-disc checks are observable on hardware")
forbid_text("${GENERATED_DIR}/StorageImpl.cpp"
    "return sceIoRead(Handle, buffer, read_size);"
    "Vita local-file reads still expose a raw positive short sceIoRead")
require_text("${GENERATED_DIR}/StorageImpl.cpp"
    "#include \"krkrvita/write_all.hpp\""
    "Vita local files use the documented short-write loop")
require_text("${GENERATED_DIR}/StorageImpl.cpp"
    "krkrvita::write_all_bytes"
    "Vita local-file writes complete every positive partial sceIoWrite")
require_text("${GENERATED_DIR}/StorageImpl.cpp"
    "Handle = sceIoOpen(holder, rw, 0666)"
    "Vita local writes use the native seekable stream")
forbid_text("${GENERATED_DIR}/StorageImpl.cpp"
    "MemBuffer = new tTVPMemoryStream()"
    "Vita local files retain Kirikiroid's whole-file Android buffer")
forbid_text("${GENERATED_DIR}/StorageImpl.cpp"
    "free(this)"
    "a Yuri file-stream destructor manually frees its own object")
forbid_text("${GENERATED_DIR}/StorageImpl.cpp"
    "File Writing Error"
    "a Yuri file-stream destructor throws after a buffered flush")
forbid_text_between("${GENERATED_DIR}/StorageImpl.cpp"
    "tTVPLocalFileStream::~tTVPLocalFileStream()"
    "tjs_uint64 TJS_INTF_METHOD tTVPLocalFileStream::Seek"
    "TVPThrowExceptionMessage"
    "the implicitly-noexcept local-file destructor can throw")
require_text("${GENERATED_DIR}/TextStream.cpp"
    "COMPRESSION_BUFFER_SIZE = 64 * 1024"
    "compressed bookmark data uses a bounded streaming output window")
forbid_text("${GENERATED_DIR}/TextStream.cpp"
    "COMPRESSION_BUFFER_SIZE = 1024 * 1024"
    "compressed bookmark data retains Yuri's 1 MiB contiguous save allocation")
require_text("${GENERATED_DIR}/TextStream.cpp"
    "bool zstream_initialized = false;"
    "compressed bookmark setup tracks ownership before constructor failure")
require_text("${GENERATED_DIR}/TextStream.cpp"
    "if(zstream_initialized) deflateEnd(ZStream);"
    "failed compressed bookmark setup releases initialized zlib state")
require_text("${GENERATED_DIR}/TextStream.cpp"
    "BufferedUnits<tjs_uint8, DIRECT_OUTPUT_BUFFER_SIZE> DirectOutputBuffer"
    "built-in Dictionary.saveStruct uses bounded direct-output aggregation")
require_text("${GENERATED_DIR}/TextStream.cpp"
    "DIRECT_OUTPUT_BUFFER_SIZE = 8 * 1024"
    "built-in text persistence uses the fixed 8 KiB Vita buffer")
require_text("${GENERATED_DIR}/TextStream.cpp"
    "DirectOutputBuffer.append("
    "uncompressed TJS text fragments enter the bounded output buffer")
require_text("${GENERATED_DIR}/TextStream.cpp"
    "yuri-text-write-buffered-ready"
    "the built-in TJS text-writer buffer is observable on hardware")
require_text("${GENERATED_DIR}/TextStream.cpp"
    "Vita buffered text write: "
    "large built-in text writes report byte and sink-call counts")
forbid_text("${GENERATED_DIR}/TextStream.cpp"
    "Stream->WriteBuffer(ptr, (tjs_uint)size); // write directly"
    "uncompressed Dictionary.saveStruct still maps every fragment to sceIoWrite")
require_text_order_between("${GENERATED_DIR}/TextStream.cpp"
    "} catch(...) {"
    "\n\t\t}\n\t}"
    "delete[] CompressionBuffer;"
    "delete Stream;"
    "failed compressed bookmark setup releases its buffer before its file stream")
require_text("${GENERATED_DIR}/ScriptMgnIntf.cpp"
    "TVPGetCommandLine(TJS_W(\"-krkrpatch\")"
    "the selected retail patch is loaded by Yuri")
require_text("${GENERATED_DIR}/ScriptMgnIntf.cpp"
    "TJS_W(\"app0:krkrvita/retail-after-startup.tjs\")"
    "the Vita retail adaptation is loaded after game startup")
require_text("${GENERATED_DIR}/ScriptMgnIntf.cpp"
    "retail-vita-afterstartup-executed"
    "successful Vita post-startup execution is observable on hardware")
require_text("${GENERATED_DIR}/ScriptMgnIntf.cpp"
    "krkrvita::is_kag_system_variable_storage"
    "KAG system-variable recovery is restricted to recognized data files")
require_text("${GENERATED_DIR}/ScriptMgnIntf.cpp"
    "krkrvita::quarantine_corrupt_system_variable"
    "unparseable KAG state is preserved before startup recovery")
require_text("${GENERATED_DIR}/ScriptMgnIntf.cpp"
    "catch(const eTJSScriptError &)"
    "KAG state recovery runs only after script evaluation fails")
require_text("${GENERATED_DIR}/ScriptMgnIntf.cpp"
    "yuri-kag-system-variables-recovered"
    "KAG system-variable recovery is observable on hardware")
require_text_order_between("${GENERATED_DIR}/ScriptMgnIntf.cpp"
    "TVPAddLog(TJS_W(\"(info) Startup script ended.\"))"
    "TJS_CONVERT_TO_TJS_EXCEPTION"
    "TVPGetAppPath() + \"AfterStartup.tjs\""
    "TJS_W(\"app0:krkrvita/retail-after-startup.tjs\")"
    "the game AfterStartup hook runs before the Vita retail adaptation")
require_text("${GENERATED_DIR}/xp3filter.cpp"
    "TVPGetCommandLine(TJS_W(\"-xp3filter\")"
    "the selected retail filter is loaded before XP3 access")
require_text("${GENERATED_DIR}/FontImpl.cpp"
    "ux0:data/krkrvita/msgothic.ttc"
    "the external personal-use MS Gothic collection has a stable Vita path")
require_text("${GENERATED_DIR}/FontImpl.cpp"
    "vita_ms_gothic_info.Index = 0"
    "the primary TTC face is specifically non-proportional MS Gothic")
require_text("${GENERATED_DIR}/FontImpl.cpp"
    "vita_ms_gothic_face_count > 0"
    "the required collection is parsed before its stable aliases are registered")
require_text("${GENERATED_DIR}/FontImpl.cpp"
    "TVPDefaultFontName = TJS_W(\"ＭＳ ゴシック\")"
    "MS Gothic is the primary retail default when the external TTC is present")
require_text("${GENERATED_DIR}/FontImpl.cpp"
    "yuri-msgothic-primary-selected"
    "MS Gothic primary selection is observable on hardware")
require_text("${GENERATED_DIR}/FontImpl.cpp"
    "yuri-msgothic-required-font-missing"
    "missing external MS Gothic fails observably")
forbid_text("${GENERATED_DIR}/FontImpl.cpp"
    "ScePvf"
    "the mandatory MS Gothic registry must not contain a PVF fallback")
require_text("${GENERATED_DIR}/FontImpl.cpp"
    "if (info->Path.IsEmpty()) return nullptr;"
    "invalid private-font entries never become empty storage-media requests")
require_text("${GENERATED_DIR}/FontImpl.cpp"
    "krkrvita_create_ms_gothic_pread_stream()"
    "MS Gothic face zero uses the fixed Vita pread stream")
require_text("${GENERATED_DIR}/FontImpl.cpp"
    "info->Index == 0"
    "the fast font stream cannot select another TTC face")
require_text("${GENERATED_DIR}/FontSystem.cpp"
    "return TVPFindFont(name) != nullptr;"
    "retail addFont registrations remain visible after font-system startup")
require_text("${GENERATED_DIR}/FreeTypeFontRasterizer.cpp"
    "TVP_FACE_OPTIONS_FACE_INDEX(selected_info->Index)"
    "FreeType opens the registered face from a TTC instead of always face zero")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "new FreeTypeFontRasterizer()"
    "mandatory MS Gothic directly instantiates Yuri FreeType")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "yuri-msgothic-freetype-rasterizer-selected"
    "the direct FreeType backend switch is observable on hardware")
forbid_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "YuriVitaPvfFontRasterizer"
    "the mandatory MS Gothic build must not construct a PVF rasterizer")
require_text("${GENERATED_DIR}/FreeTypeFontRasterizer.cpp"
    "yuri-msgothic-freetype-face-applied"
    "MS Gothic face zero application is observable inside FreeType")
require_text("${GENERATED_DIR}/FreeTypeFontRasterizer.cpp"
    "yuri-freetype-text-extent-complete"
    "MS Gothic text measurement is observable inside FreeType")
require_text("${GENERATED_DIR}/FreeTypeFontRasterizer.cpp"
    "yuri-freetype-first-glyph-rendered"
    "MS Gothic glyph rendering is observable inside FreeType")
require_text("${GENERATED_DIR}/FontRasterizer.h"
    "GetTextExtentForPrefixCache(tjs_char ch, tjs_int &w, tjs_int &h) { GetTextExtent(ch, w, h); return false; }"
    "non-MS-Gothic rasterizers opt out of growing-line prefix reuse")
require_text("${GENERATED_DIR}/FreeTypeFontRasterizer.h"
    "GetTextExtentCacheState(tjs_uint64 &state) const override"
    "FreeType exposes an exact metric-state key for prefix validation")
require_text("${GENERATED_DIR}/FreeTypeFontRasterizer.cpp"
    "return loaded && Face->GetVitaMsGothicMetricState(state);"
    "only successfully loaded fixed MS Gothic glyphs make a prefix reusable")
require_text("${GENERATED_DIR}/FreeType.h"
    "YuriGlyphMetricsCache<tGlyphMetrics, 256>"
    "MS Gothic metrics reuse is fixed-size and per face")
require_text("${GENERATED_DIR}/FreeType.h"
    "bool SizeInitialized = false"
    "the constructor's nominal height is not mistaken for an initialized FT size")
require_text("${GENERATED_DIR}/FreeType.h"
    "state = (static_cast<tjs_uint64>(Options) << 32)"
    "the prefix key losslessly includes every FreeType option bit")
require_text("${GENERATED_DIR}/FreeType.h"
    "if(!UseVitaMsGothicFastPath || !SizeInitialized) return false;"
    "prefix reuse requires fixed MS Gothic and an initialized pixel size")
require_text("${GENERATED_DIR}/FreeType.cpp"
    "UseVitaMsGothicFastPath = vita_font_info"
    "glyph fast paths are selected from the authoritative fixed-path registry")
require_text("${GENERATED_DIR}/FreeType.cpp"
    "SizeInitialized && Height == height"
    "repeated font application does not invalidate an already initialized size")
require_text("${GENERATED_DIR}/FreeType.cpp"
    "if(SizeInitialized && Height == height) return;\n\tInvalidatePreparedGlyph();\n\tHeight = height;"
    "a real height change invalidates the prepared glyph slot without flushing metrics")
forbid_text("${GENERATED_DIR}/FreeType.cpp"
    "GlyphMetricsCache.clear()"
    "alternating font heights must retain bounded exact-key metrics")
require_text("${GENERATED_DIR}/FreeType.cpp"
    "UseVitaMsGothicFastPath && SizeInitialized &&\n\t\tGlyphMetricsCache.find(cache_key, metrics)"
    "MS Gothic measurement reads cached metrics only after size initialization")
require_text("${GENERATED_DIR}/FreeType.cpp"
    "if(!LoadGlyphSlotFromCharcode(code)) return false;"
    "transient FreeType load failures remain retryable")
forbid_text("${GENERATED_DIR}/FreeType.cpp"
    "GlyphMetricsCache.store(cache_key, missing"
    "transient FreeType load failures must not be negative-cached")
forbid_text("${GENERATED_DIR}/FreeType.cpp"
    "cached_success"
    "the metrics cache contains successful values only")
require_text("${GENERATED_DIR}/FreeType.cpp"
    "if(UseVitaMsGothicFastPath && SizeInitialized)\n\t\tGlyphMetricsCache.store(cache_key, metrics);"
    "a failed FreeType size change can never populate the metrics cache")
require_text("${GENERATED_DIR}/FreeType.cpp"
    "PreparedGlyphKey == prepared_key"
    "measure-to-render reuse requires an exact FreeType load key")
require_text("${GENERATED_DIR}/FreeType.cpp"
    "InvalidatePreparedGlyph();"
    "rendered or externally loaded glyph slots are not reused as prepared data")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_ms_gothic_stream.cpp"
    "sceIoPread"
    "MS Gothic reads are position-independent SceIofilemgr operations")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_ms_gothic_stream.hpp"
    "TJS::tTJSBinaryStream* krkrvita_create_ms_gothic_pread_stream()"
    "the fixed-font stream uses Yuri's namespaced binary-stream ABI")
forbid_text("${SOURCE_DIR}/src/platform/vita/yuri_ms_gothic_stream.cpp"
    "sceIoLseek"
    "MS Gothic stream seeks must remain logical and syscall-free")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_ms_gothic_stream.cpp"
    "SharedMsGothicFile* file_"
    "each FreeType stream holds only the process-shared font backend")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_ms_gothic_stream.cpp"
    "std::unique_ptr<krkrvita::MsGothicPageCache> cache_"
    "the process has at most one lazily allocated MS Gothic page cache")
require_text("${SOURCE_DIR}/include/krkrvita/ms_gothic_fast_path.hpp"
    "ms_gothic_cache_payload_bytes <= 512u * 1024u"
    "MS Gothic's shared page-cache payload is bounded to 512 KiB")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "krkrvita_compact_ms_gothic_pread_cache();"
    "maximum bitmap pressure releases the optional font page cache")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_ms_gothic_stream.cpp"
    "existing_shared_ms_gothic_file()"
    "font cache compaction cannot create an unused backend")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "Bitmap = nullptr;"
    "failed texture construction unwinds through an initialized bitmap pointer")
require_text("${GENERATED_DIR}/LayerBitmapImpl.h"
    "bool CachedTextPrefixReusable = false;"
    "each layer retains only one bounded growing-line prefix state")
require_text("${SOURCE_DIR}/cmake/YuriBackend.cmake"
    "yuri_layer_bitmap_abi_header"
    "the augmented layer-bitmap layout is an explicit object dependency")
require_text("${SOURCE_DIR}/cmake/YuriBackend.cmake"
    "yuri_font_rasterizer_abi_header"
    "the augmented rasterizer vtable is an explicit object dependency")
require_text("${SOURCE_DIR}/cmake/YuriBackend.cmake"
    "yuri_freetype_rasterizer_abi_header"
    "the augmented FreeType vtable declaration is an explicit object dependency")
require_text("${SOURCE_DIR}/include/krkrvita/text_prefix_width.hpp"
    "if (cached_text[i] != text[i]) return {};"
    "text-prefix reuse uses exact UTF code-unit comparison rather than hashes")
require_text("${SOURCE_DIR}/include/krkrvita/text_prefix_width.hpp"
    "if (cached_text[i] == Character{}) return {};"
    "embedded NUL cannot skip beyond Yuri's effective text terminator")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "krkrvita::yuri_find_text_width_prefix("
    "KAG growing lines resume after the existing exact cached string")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "rasterizer->GetTextExtentCacheState(metric_state)"
    "the active MS Gothic metric state is checked before prefix reuse")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "measured_stably = measured_stably && glyph_stable;"
    "a missing or transient glyph load prevents stale prefix reuse")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "yuri-msgothic-text-prefix-reused"
    "growing-line prefix reuse is observable on hardware")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "CachedTextMetricState = 0;\n\t\tCachedTextPrefixReusable = false;"
    "font/global-rasterizer changes invalidate the growing-line prefix")
forbid_text_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "void tTVPNativeBaseBitmap::GetTextSize("
    "tjs_int tTVPNativeBaseBitmap::GetTextWidth("
    "GetTextExtent("
    "growing-line measurement must report glyph success before caching")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "#include \"FreeType.h\""
    "LayerBitmapImpl uses the same generated FreeType overlay as the rasterizer")
forbid_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "#include \"visual/FreeType.h\""
    "LayerBitmapImpl must not bypass the generated FreeType overlay")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "krkrvita_yuri_profile_bitmap_independ"
    "bitmap copy-on-write costs are measured on hardware")
forbid_text_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "void tTVPNativeBaseBitmap::ApplyFont()"
    "void tTVPNativeBaseBitmap::SetFont("
    "Independ();"
    "metadata-only font application must not detach bitmap pixels")
forbid_text_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "bool tTVPNativeBaseBitmap::InternalBlendText("
    "bool tTVPNativeBaseBitmap::InternalDrawText("
    "GetTextureForRender("
    "per-glyph blending must reuse its entry-point-detached destination")
require_text_count_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "bool tTVPNativeBaseBitmap::InternalBlendText("
    "void tTVPNativeBaseBitmap::GetTextSize("
    "InternalBlendText(" 2
    "InternalBlendText has only its definition and private InternalDrawText caller")
require_text_count_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "bool tTVPNativeBaseBitmap::InternalBlendText("
    "void tTVPNativeBaseBitmap::GetTextSize("
    "InternalDrawText(" 7
    "InternalDrawText has only its definition and the three public draw families")
require_text_count_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "void tTVPNativeBaseBitmap::DrawGlyph("
    "void tTVPNativeBaseBitmap::DrawTextSingle("
    "Independ();" 1
    "DrawGlyph detaches its destination exactly once before pixel writes")
require_text_count_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "void tTVPNativeBaseBitmap::DrawTextSingle("
    "struct tTVPCharacterDrawData"
    "Independ();" 1
    "DrawTextSingle detaches its destination exactly once before pixel writes")
require_text_count_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "void tTVPNativeBaseBitmap::DrawTextMultiple("
    "void tTVPNativeBaseBitmap::GetTextSize("
    "Independ();" 1
    "DrawTextMultiple detaches its destination exactly once before pixel writes")
require_text_order_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "void tTVPNativeBaseBitmap::DrawGlyph("
    "void tTVPNativeBaseBitmap::DrawTextSingle("
    "Independ();" "ApplyFont();"
    "DrawGlyph detaches before applying metadata and writing pixels")
require_text_order_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "void tTVPNativeBaseBitmap::DrawTextSingle("
    "struct tTVPCharacterDrawData"
    "Independ();" "ApplyFont();"
    "DrawTextSingle detaches before applying metadata and writing pixels")
require_text_order_between("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "void tTVPNativeBaseBitmap::DrawTextMultiple("
    "void tTVPNativeBaseBitmap::GetTextSize("
    "Independ();" "ApplyFont();"
    "DrawTextMultiple detaches before applying metadata and writing pixels")
require_text("${GENERATED_DIR}/LayerBitmapImpl.cpp"
    "GetTexture(), nullptr, drect"
    "text blending uses the destination detached by its public entry point")
require_text("${GENERATED_DIR}/RenderManager.h"
    "int PresentationRefCount"
    "the Vita presenter has an explicit read-only texture lifetime hold")
require_text("${GENERATED_DIR}/RenderManager.h"
    "yuri_texture_has_single_mutable_owner"
    "presentation holds do not invalidate Yuri mutable ownership")
require_text("${GENERATED_DIR}/RenderManager_software.h"
    "#include \"RenderManager.h\""
    "software renderer nested includes resolve inside the generated ABI overlay")
require_text("${GENERATED_DIR}/ObjectList.h"
    "if(compacted_count == Count) return;"
    "stable Yuri child-list traversals retain their backing arrays")
forbid_text("${GENERATED_DIR}/ObjectList.h"
    "Count = (tjs_int)(d - Objects);\n\t\tCapacity = Count;"
    "unchanged Yuri child lists cannot rebuild on every safe lock")
require_text("${SOURCE_DIR}/cmake/YuriBackend.cmake"
    "APPEND PROPERTY OBJECT_DEPENDS"
    "every Yuri object rebuilds when the generated texture ABI header changes")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "texture->AddPresentationRef()"
    "the Vita window uses the non-COW presentation lifetime contract")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "yuri-presentation-reference-ready"
    "the non-COW presentation lifetime contract is observable on hardware")
forbid_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "texture->AddRef();"
    "the Vita presenter must not make the compositor buffer look shared")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "bitmap_independ_copy_bytes_total="
    "bitmap copy-on-write counters are exported in the hardware snapshot")
require_text("${GENERATED_DIR}/tjs2/tjsInterCodeExec.cpp"
    "s ? s->GetLength() : 0"
    "empty-string length properties cannot dereference a null string object")
require_text("${GENERATED_DIR}/tjs2/tjsInterCodeExec.cpp"
    "valstr ? valstr->GetLength() : 0"
    "empty-string numeric properties cannot dereference a null string object")
require_text("${GENERATED_DIR}/tjs2/tjsHashSearch.h"
    "alignas(ValueT) char Value[sizeof(ValueT)]"
    "script-cache placement storage satisfies the value type's alignment")
require_text("${GENERATED_DIR}/saveStruct.cpp"
    "krkrvita::BufferedUnits<tjs_char, 4096> buffer;"
    "saveStruct aggregates single-character output in a bounded buffer")
require_text_count("${GENERATED_DIR}/saveStruct.cpp"
    "writer.flush();" 5
    "saveStruct flushes every file and memory result before observation")
require_text("${GENERATED_DIR}/saveStruct.cpp"
    "yuri-savestruct-buffered-ready"
    "saveStruct buffering is observable on hardware after registration")
require_text_count("${GENERATED_DIR}/saveStruct.cpp"
    "write(s.c_str());" 2
    "integer and real saveStruct fields retain buffered output order")
require_text_count("${GENERATED_DIR}/saveStruct.cpp"
    "stream->Write(" 1
    "only the saveStruct buffer sink writes the underlying stream")
forbid_text("${GENERATED_DIR}/saveStruct.cpp"
    "stream->Write(&c, sizeof(c));"
    "saveStruct cannot issue one Vita file write per UTF-16 character")
require_text("${GENERATED_DIR}/tjs2/tjsConstArrayData.cpp"
    "dynamic_cast<const tTJSInterCodeContext*>(obj)"
    "bytecode export classifies object constants through the polymorphic hierarchy")
require_text("${GENERATED_DIR}/tjs2/tjsConstArrayData.cpp"
    "context ? block->GetCodeIndex(context) : -1"
    "ordinary Dictionary and Array constants are not treated as code contexts")
require_text_count("${GENERATED_DIR}/tjs2/tjsConstArrayData.cpp"
    "dynamic_cast<const tTJSInterCodeContext*>(obj)" 2
    "both object-constant classification and insertion validate their dynamic type")
forbid_text("${GENERATED_DIR}/tjs2/tjsConstArrayData.cpp"
    "(const tTJSInterCodeContext*)obj"
    "bytecode export must not downcast arbitrary dispatch objects")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA"
    "Yuri's little-endian RGBA framebuffer keeps red and blue channels ordered")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "vitagl-cursor-overlay-presented"
    "the analog pointer has an on-device VitaGL overlay")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "tTVPOnMouseWheelInputEvent"
    "the Vita mapping can emit Kirikiri mouse-wheel events")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "if (button == mbLeft)"
    "right-click mapping does not also emit Kirikiri's primary click")
require_text("${SOURCE_DIR}/include/krkrvita/vita_input_mapping.hpp"
    "vita_input_mapping_version = 4"
    "the global Vita mapping has a migration version")
require_text("${SOURCE_DIR}/include/krkrvita/vita_input_mapping.hpp"
    "{\"cross\", \"mouse_right\"}"
    "Cross uses Kirikiri's secondary pointer click")
require_text("${SOURCE_DIR}/include/krkrvita/vita_input_mapping.hpp"
    "{\"circle\", \"key_enter\"}"
    "Circle uses Kirikiri's Enter action")
require_text("${SOURCE_DIR}/include/krkrvita/vita_input_mapping.hpp"
    "{\"ltrigger\", \"mouse_left\"}"
    "L trigger clicks at the current pointer position")
require_text("${SOURCE_DIR}/include/krkrvita/vita_input_mapping.hpp"
    "{\"rtrigger\", \"key_control\"}"
    "R trigger uses Kirikiri's Control action")
require_text("${SOURCE_DIR}/include/krkrvita/vita_input_mapping.hpp"
    "{\"triangle\", \"mouse_wheel_up\"}"
    "Triangle emits an upward mouse-wheel action")
require_text("${SOURCE_DIR}/include/krkrvita/vita_input_mapping.hpp"
    "{\"square\", \"disabled\"}"
    "Square is unmapped by default")
require_text("${SOURCE_DIR}/include/krkrvita/vita_input_mapping.hpp"
    "{\"start\", \"disabled\"}"
    "Start is unmapped by default")
require_text("${SOURCE_DIR}/include/krkrvita/vita_input_mapping.hpp"
    "{\"select\", \"disabled\"}"
    "Select is unmapped by default")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_input.cpp"
    "configuration.bindings = krkrvita::vita_default_input_bindings();"
    "the Yuri Vita backend consumes the global controller mapping")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_input.cpp"
    "vita_input_binding_is_superseded_default("
    "old Yuri profiles cannot restore superseded controller defaults")
require_text("${SOURCE_DIR}/src/engine/vita/vita_input_bridge.cpp"
    "configuration.bindings = krkrvita::vita_default_input_bindings();"
    "the SDL Vita backend consumes the global controller mapping")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_input.cpp"
    "current_shift_state() | TVP_SS_REPEAT"
    "held Vita controls emit Windows-compatible repeated key-down events")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_openal_mixer.cpp"
    "alSourcei(source_, AL_SAMPLE_OFFSET, 0)"
    "each fresh retail voice starts from OpenAL sample zero")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_openal_mixer.cpp"
    "return playback_.requested();"
    "transient OpenAL state does not discard Kirikiri's playback intent")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_input.cpp"
    "sceTouchRead(SCE_TOUCH_PORT_FRONT"
    "front touch retains Sony's VSYNC sample history at low game frame rates")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "active_layer->GetPointerArea(logical_width, logical_height)"
    "absolute touch uses Kirikiri's rendered paint-box coordinate space")
require_text("${SOURCE_DIR}/include/krkrvita/key_repeat.hpp"
    "hold_delay_us = 500000"
    "Vita keyboard repeat uses Yuri's established Windows hold delay")
require_text("${SOURCE_DIR}/include/krkrvita/key_repeat.hpp"
    "interval_us = 30000"
    "Vita keyboard repeat uses Yuri's established Windows interval")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "TVPGetRenderManager(TJS_W(\"software\"))"
    "Yuri's stable software compositor is selected")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "_createStaticTexture2D = tTVPSoftwareTexture2D::Create"
    "static retail textures retain Yuri's source bitmap without a duplicate working copy")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "yuri-software-static-texture-direct-ready"
    "the direct static-texture backend is observable on hardware")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "yuri-opencv-software-fastpaths-ready"
    "the OpenCV-compatible software fast paths are observable on hardware")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "virtual bool IsBlendTarget() override { return GetName() != \"Copy\"; }"
    "only exact Copy may discard prior destination pixels during COW")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "cv::BoxFilterWorkspace Workspace;"
    "box blur reuses its rolling-sum workspace")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "cv::prepareResizeTaskWorkspace("
    "resize scratch is sized on the owner before worker dispatch")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "cv::resizeRows("
    "large software scaling partitions OpenCV-compatible destination rows")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "!TVPTextureBackingOverlaps(tar, src)"
    "exact nonaliasing Copy scaling can write its final target directly")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "cv::warpAffineRows("
    "affine effects use the allocation-stable row kernel")
require_text_count("${GENERATED_DIR}/RenderManager.cpp"
    "cv::warpPerspectiveRows(" 2
    "triangle and perspective effects use the allocation-stable row kernel")
forbid_text("${GENERATED_DIR}/RenderManager.cpp"
    "cv::resize(src_img, dst_img"
    "software scaling cannot allocate a full OpenCV destination")
forbid_text("${GENERATED_DIR}/RenderManager.cpp"
    "new tTVPSoftwareTexture2D_static(dst_img.ptr(0)"
    "warps cannot wrap a newly allocated full-frame OpenCV buffer")
require_text("${SOURCE_DIR}/src/yuri/compat/opencv2/opencv.hpp"
    "workspace.column_sums.assign("
    "box filtering uses bounded rolling column sums")
require_text("${SOURCE_DIR}/src/yuri/compat/opencv2/opencv.hpp"
    "workspace.y_offsets[y]"
    "nearest resize does not evaluate floating coordinates in its pixel loop")
require_text("${SOURCE_DIR}/src/yuri/compat/opencv2/opencv.hpp"
    "if (!plan.valid)"
    "singular row warps clear direct and persistent destinations")
forbid_text("${GENERATED_DIR}/RenderManager.cpp"
    "_createStaticTexture2D = tTVPSoftwareTexture2D_lz4::Create"
    "Vita's memcpy-only LZ4 compatibility shim must not duplicate static textures")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "PixelFrameLife = 3"
    "the compiled optional compressed-texture implementation retains Yuri's upstream frame lifetime")
forbid_text("${GENERATED_DIR}/RenderManager.cpp"
    "DecodedLines"
    "Vita must not bypass Yuri's eager render-input preparation")
forbid_text("${GENERATED_DIR}/RenderManager.cpp"
    "EnsureLineDecoded"
    "worker scanline access cannot lazily mutate compressed texture state")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "public tTVPCompactEventCallbackIntf"
    "the compiled optional compressed-texture implementation participates in Yuri compaction")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "PixelFrameLife < 3"
    "compaction cannot invalidate a source prepared for the current draw")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);"
    "bitmap allocation failure invokes Yuri's strongest cache compaction")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "iTVPTexture2D::RecycleProcess();"
    "textures released during compaction are destroyed before allocation retry")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "krkrvita::drain_deferred_recycle_batches"
    "allocation recovery drains destructor-enqueued texture batches to a fixed point")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "yuri-texture-recycler-multibatch-drained"
    "multi-generation OOM texture reclamation is observable on hardware")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "yuri-bitmap-oom-recovered"
    "successful allocation recovery is observable on hardware")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "present_texture_->GetScanLineForRead(0)"
    "the software compositor exposes its completed CPU framebuffer")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "yuri-software-framebuffer-ready"
    "the software framebuffer presentation path is observable on hardware")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "surface_updates_.request();"
    "Yuri draw-buffer updates, including same-pointer updates, drive presentation")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "vitagl-duplicate-frame-upload-skipped"
    "unchanged full-resolution software frames are observably suppressed")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "krkrvita_vitagl_redraw()"
    "cursor-only frames reuse the uploaded Kirikiri surface")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "last_presented_texture"
    "VitaGL retains the last software upload for cursor-only redraws")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "PresentationDamageTracker<kPresentationBuffers>"
    "each rotating VitaGL texture retains its own accumulated damage")
require_text("${SOURCE_DIR}/include/krkrvita/presentation_surface_transaction.hpp"
    "ready_count != BufferCount"
    "a partial VitaGL texture set cannot become the active presentation surface")
require_text("${SOURCE_DIR}/include/krkrvita/presentation_surface_transaction.hpp"
    "result.retired = textures_"
    "a successful resize returns the prior valid texture set for retirement")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "SurfaceState::TextureArray candidates{}"
    "VitaGL resize allocates into a separate candidate texture set")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "vglGetTexDataPointer(GL_TEXTURE_2D) != nullptr"
    "the installed VitaGL allocation failure is detected without relying on a missing GL error")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "delete_texture_set(candidates);"
    "failed VitaGL resize candidates are discarded without poisoning retry")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "delete_texture_set(commit.retired);"
    "the prior VitaGL surface is retired only after a successful transaction")
forbid_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "texture_width = width"
    "VitaGL resize cannot commit dimensions before texture allocation succeeds")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4)"
    "partial VitaGL uploads use the compositor framebuffer pitch")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "vitagl-partial-frame-upload-ready"
    "partial framebuffer upload is observable on hardware")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "ux0:data/krkrvita/perf-stats.txt"
    "on-device profiling separates Yuri execution from VitaGL presentation")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "krkrvita_vitagl_uploaded_frames()"
    "performance telemetry distinguishes real uploads from redraws")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "compositor_us_total"
    "performance telemetry separates layer composition from script execution")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "yuri_performance_snapshot_is_safe("
    "performance telemetry waits for visual quiescence instead of stalling an active frame")
forbid_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "if (elapsed >= next_performance_snapshot)"
    "periodic Vita storage telemetry cannot run synchronously during motion")
require_text("${GENERATED_DIR}/LayerManager.cpp"
    "krkrvita_yuri_profile_compositor"
    "every Yuri layer completion reports its dirty-region cost")
require_text("${GENERATED_DIR}/LayerManager.cpp"
    "void tTVPLayerManager::NotifyUpdateRegionFixed()\n{\n\t// called by primary layer after BeforeCompletion() has finalized damage"
    "Yuri captures presenter damage only at the finalized-region hook")
require_text("${GENERATED_DIR}/LayerManager.cpp"
    "krkrvita_yuri_add_frame_damage"
    "every compositor dirty rectangle reaches the Vita presenter")
require_text("${GENERATED_DIR}/LayerManager.cpp"
    "#include \"krkrvita/layer_draw_completion.hpp\""
    "the layer manager uses the shared exact self-completion predicate")
require_text("${GENERATED_DIR}/LayerManager.cpp"
    "krkrvita::is_noop_drawbuffer_completion(\n\t\tDrawBuffer, bmp, destrect, cliprect, type, ltOpaque, opacity)"
    "only an exact opaque DrawBuffer self-copy bypasses Yuri's final Blt")
require_text("${SOURCE_DIR}/include/krkrvita/layer_draw_completion.hpp"
    "destination.right == source.right"
    "the completion guard rejects shifted or differently sized self-copies")
require_text("${SOURCE_DIR}/include/krkrvita/layer_draw_completion.hpp"
    "layer_type == opaque_layer_type && opacity == 255"
    "the completion guard preserves alpha and partial-opacity blend semantics")
forbid_text_between("${GENERATED_DIR}/LayerManager.cpp"
    "void TJS_INTF_METHOD tTVPLayerManager::UpdateToDrawDevice()"
    "void tTVPLayerManager::NotifyUpdateRegionFixed()"
    "krkrvita_yuri_begin_frame_damage"
    "presenter damage cannot be captured before BeforeCompletion finalizes it")
require_text("${SOURCE_DIR}/vendor/yuri/src/core/visual/LayerIntf.cpp"
    "void tTJSNI_BaseLayer::CompleteForWindow(tTVPDrawable *drawable)\n{\n\tBeforeCompletion();\n\n\tif(Manager) Manager->NotifyUpdateRegionFixed();"
    "Yuri calls the damage hook only after BeforeCompletion finalizes the region")
require_text("${SOURCE_DIR}/vendor/yuri/src/core/visual/LayerIntf.cpp"
    "InternalComplete2(Manager->GetUpdateRegionForCompletion(), drawable);"
    "the finalized region is captured before software completion consumes it")
require_text("${GENERATED_DIR}/EventIntf.cpp"
    "// for window content updating\n\t\t\t\tTVPDeliverWindowUpdateEvents();"
    "Vita retains Yuri Android's unconditional queued-window delivery")
require_text("${GENERATED_DIR}/EventIntf.cpp"
    "#include \"krkrvita/event_arguments.hpp\""
    "Vita event delivery uses the allocation-tested argument helper")
require_text_count("${GENERATED_DIR}/EventIntf.cpp"
    "krkrvita::allocate_event_argument_storage<tTJSVariant>(NumArgs)" 2
    "event construction and copying both avoid zero-length variant arrays")
require_text("${GENERATED_DIR}/EventIntf.cpp"
    "krkrvita::with_event_argument_pointers(\n\t\t\tArgs, NumArgs, [this](tTJSVariant **args_ptr)"
    "zero-argument event delivery passes the helper's null argv")
forbid_text("${GENERATED_DIR}/EventIntf.cpp"
    "new tTJSVariant[NumArgs]"
    "zero-argument event construction cannot allocate a variant array")
forbid_text("${GENERATED_DIR}/EventIntf.cpp"
    "new tTJSVariant*[NumArgs]"
    "zero-argument event delivery cannot allocate an argv array")
require_text("${SOURCE_DIR}/include/krkrvita/event_arguments.hpp"
    "return count == 0 ? nullptr : new TArgument[count];"
    "zero event arguments use null storage")
require_text("${SOURCE_DIR}/include/krkrvita/event_arguments.hpp"
    "if(count == 0) {\n        std::forward<TInvoke>(invoke)(nullptr);"
    "zero-argument FuncCall receives a null argv without allocation")
forbid_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "window_update_coalescing"
    "the Vita event loop cannot enable a frame-skipping throttle")
forbid_text("${SOURCE_DIR}/src/platform/vita/yuri_performance.cpp"
    "WindowUpdateCoalescer"
    "compositor telemetry cannot alter Yuri's window-delivery cadence")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "yuri_needs_full_window_exposure"
    "normal Window.update preserves Yuri's existing dirty regions")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_window_layer.cpp"
    "yuri-normal-update-preserved-dirty-region"
    "dirty-region preservation is observable on hardware")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "krkrvita::yuri_frame_delay_us"
    "event dispatch uses one whole-frame deadline after update and presentation")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "vglWaitVblankStart(GL_FALSE)"
    "VitaGL presentation cannot add a vblank stall to Yuri's engine tick")
# sigcheck must stay on the calling thread. TVPCreateStream walks Yuri's media
# manager and auto-path table, TJS reference counts are plain ints, and
# TVPEventQueue is an unlocked vector -- none of it survives a worker thread.
forbid_text("${SOURCE_DIR}/src/engine/retail/yuri_sigcheck_module.cpp"
    "std::thread"
    "sigcheck must not verify on a worker thread")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_sigcheck_module.cpp"
    "deliver_done(owner, handler, verified, error);"
    "sigcheck delivers its result through the event queue")
# A file with no .sig is unsigned, not corrupt. The reference plug-in passes it;
# failing it refuses intact retail titles (see allokmama.exe).
require_text("${SOURCE_DIR}/src/engine/retail/yuri_sigcheck_module.cpp"
    "if (!TVPIsExistentStorage(signature_path)) {"
    "an absent signature is treated as unsigned rather than as a failure")

# System.exeName must be the game executable, not the project directory:
# "ダメダメなボクに舞い降りた全肯定ママ女神" exits at script/first.ks unless
# chopStorageExt(System.exeName) + ".cf" resolves.
require_text("${GENERATED_DIR}/Application.cpp"
    "krkrvita_yuri_project_executable_path(TVPNativeProjectDir)"
    "ExePath resolves the staged Windows executable on Vita")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_storage_preflight.cpp"
    "krkrvita::vita_select_executable_name(names)"
    "the executable is chosen by the tested selection rule")
require_text("${COMPILE_COMMANDS}"
    "core/base/win32/SystemImpl.cpp"
    "System.exeName's implementation is compiled into the Vita backend")

require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "sceKernelGetFreeMemorySize(&info)"
    "VitaGL's application reserve is measured, not assumed")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "application_ram_threshold()"
    "VitaGL's USER_RW argument is treated as an application reserve threshold")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "pending.swap(_toDeleteTextures)"
    "deferred Yuri textures are destroyed in re-entrant-safe frame batches")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "krkrvita_yuri_recycled_texture_count"
    "on-device texture recycling has a behavioral proof counter")
require_text("${GENERATED_DIR}/GraphicsLoaderIntf.cpp"
    "TVPGraphicCache.AddWithHash(searchdata, hash, holder);\n\t\t\tTVPCheckGraphicCacheLimit();"
    "the retail graphics cache is bounded after full-size image insertion")
require_text("${GENERATED_DIR}/BitmapBitsAlloc.cpp"
    "Use USER_RW memblocks for large Bitmap"
    "Yuri reports its Vita bitmap allocation backend unambiguously")
require_text("${GENERATED_DIR}/BitmapBitsAlloc.cpp"
    "krkrvita_yuri_reclaim_bitmap_memory();"
    "decoded Yuri bitmap OOM invokes the shared compaction-and-retry path")
require_text("${GENERATED_DIR}/BitmapBitsAlloc.cpp"
    "memory = krkrvita::vita_bitmap_allocate(size);"
    "decoded Yuri bitmaps retry the tiered application allocator")
require_text("${GENERATED_DIR}/BitmapBitsAlloc.cpp"
    "krkrvita::vita_bitmap_deallocate(mem)"
    "decoded Yuri bitmaps return to the allocator that owns them")
require_text("${GENERATED_DIR}/BitmapBitsAlloc.cpp"
    "yuri-bitmap-tiered-allocator-ready"
    "the active hardware bitmap allocator is observable on Vita")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "krkrvita::vita_bitmap_allocate(allocbytes)"
    "compressed texture expansion uses the tiered bitmap allocator")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "krkrvita::vita_bitmap_deallocate(record->alloc_ptr)"
    "compressed texture expansion returns its owning memblock")
require_text("${GENERATED_DIR}/SysInitImpl.cpp"
    "IndividualConfigManager::GetInstance()->GetValue<std::string>(\"renderer\", \"software\")"
    "the default Vita renderer policy remains Yuri software")
file(READ "${GENERATED_DIR}/SysInitImpl.cpp" sys_init_contents)
string(FIND "${sys_init_contents}" "std::string _val = \"opengl\";"
    forced_opengl_renderer)
if(NOT forced_opengl_renderer LESS 0)
    message(FATAL_ERROR
        "Default Yuri Vita build still forces the experimental OpenGL compositor")
endif()
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "vglUseCachedMem(GL_TRUE)"
    "VitaGL's CPU-written USER_RW pool is cached")
require_text("${SOURCE_DIR}/src/engine/vita/vitagl_presenter.cpp"
    "vglSetupGarbageCollector(127, 0x20000)"
    "VitaGL uses the shipping-port resource collector configuration")
require_text("${SOURCE_DIR}/include/krkrvita/vita_memory_budget.hpp"
    "kVitaNewlibHeapBytes = 128u * 1024u * 1024u"
    "the fixed newlib heap leaves kernel space for bitmap memblocks")
require_text("${SOURCE_DIR}/include/krkrvita/vita_memory_budget.hpp"
    "kVitaGlPoolBytes = 48u * 1024u * 1024u"
    "VitaGL keeps a presenter-sized pool instead of all free USER_RW")
require_text("${SOURCE_DIR}/src/platform/vita/vita_bitmap_allocator.cpp"
    "vita_bitmap_memblock_budget_allows(free_user_memory(), size)"
    "the large-bitmap tier is bounded by free USER_RW, not a fixed ceiling")
require_text("${SOURCE_DIR}/src/platform/vita/vita_bitmap_allocator.cpp"
    "SCE_KERNEL_MEMBLOCK_TYPE_USER_RW"
    "large CPU bitmaps use cached Vita USER_RW memblocks")
require_text("${SOURCE_DIR}/src/platform/vita/vita_bitmap_allocator.cpp"
    "sceKernelFreeMemBlock(uid)"
    "large CPU bitmap storage is independently reclaimable")
require_text("${SOURCE_DIR}/src/platform/vita/vita_bitmap_allocator.cpp"
    "yuri-bitmap-newlib-overflow-fallback"
    "retail layer sets larger than the memblock tier retain newlib capacity")
require_text("${SOURCE_DIR}/src/platform/vita/vita_bitmap_allocator.cpp"
    "void* memory = allocate_malloc(size);"
    "the memblock live-byte budget is a preferred tier rather than a hard OOM limit")
file(READ "${GENERATED_DIR}/GraphicsLoaderIntf.cpp" graphics_loader_contents)
string(REPLACE
    "TVPGraphicCache.AddWithHash(searchdata, hash, holder);\n\t\t\tTVPCheckGraphicCacheLimit();"
    "" graphics_loader_unverified "${graphics_loader_contents}")
string(REPLACE
    "TVPGraphicCache.AddWithHash(item.searchdata, hash, holder);\n\t\t\tTVPCheckGraphicCacheLimit();"
    "" graphics_loader_unverified "${graphics_loader_unverified}")
string(FIND "${graphics_loader_unverified}"
    "TVPGraphicCache.AddWithHash" unbounded_cache_insertion)
if(NOT unbounded_cache_insertion LESS 0)
    message(FATAL_ERROR
        "Not every Yuri graphics-cache insertion enforces its post-insert bound")
endif()
require_text("${GENERATED_DIR}/Application.cpp"
    "krkrvita_boot_trace(\"yuri-startup-script-complete\")"
    "startup completion is observable on hardware")
require_text("${GENERATED_DIR}/Application.cpp"
    "TVPNormalizeStorageName(krkrvita_yuri_select_project(path))"
    "the game directory is resolved to a canonical Kirikiri project")
require_text("${GENERATED_DIR}/Application.cpp"
    "krkrvita_yuri_startup_storage_preflight()"
    "startup.tjs is opened through Yuri after the XP3 filter is registered")
require_text("${GENERATED_DIR}/Application.cpp"
    "TVPFreeUnusedLayerCache = true;"
    "unused Yuri layer caches are released eagerly on the memory-constrained Vita")
forbid_text("${GENERATED_DIR}/Application.cpp"
    "TVPFreeUnusedLayerCache = false;"
    "Vita must not retain unused full-size Yuri layer caches")
require_text("${GENERATED_DIR}/Application.cpp"
    "yuri-eager-layer-cache-release-enabled"
    "the eager layer-cache release policy is observable on hardware")
require_text("${GENERATED_DIR}/Application.cpp"
    "krkrvita::install_system_app_id_compat(*TVPGetScriptEngine())"
    "System.checkAppId compatibility is installed before retail startup")
require_text("${GENERATED_DIR}/Application.cpp"
    "yuri-system-app-id-compat-ready"
    "System.checkAppId compatibility is observable on hardware")
require_text("${GENERATED_DIR}/tjs2/tjsString.h"
    "if(!Ptr) return 0;"
    "empty ttstr is valid in optimized builds")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "return String ? String->operator const tjs_char *() : TJSNullStrPtr;"
    "empty string variants export safely")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "s1 ? s1->operator const tjs_char *() : TJSNullStrPtr"
    "binary concatenation preserves Yuri's null-backed empty strings")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.cpp"
    "s2 ? s2->operator const tjs_char *() : TJSNullStrPtr"
    "compound concatenation preserves Yuri's null-backed empty strings")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.cpp"
    "const tjs_char *p1 = s1 ?"
    "relational comparison preserves Yuri's null-backed empty strings")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "static_cast<tjs_uint64>(count) & 63u"
    "TJS shift counts retain the desktop runtime's modulo-64 behavior")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "return -1 - static_cast<tTVInteger>(~bits);"
    "TJS shift results convert wrapped bit patterns without out-of-range signed casts")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "result |= ~static_cast<tjs_uint64>(0) << (64u - shift);"
    "TJS arithmetic right shift defines sign extension on every target")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "return TJSShiftArithmeticRight(AsInteger(), rhs.AsInteger());"
    "TJS inline arithmetic right shift uses the defined helper")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "return TJSShiftLogicalRight(AsInteger(), count);"
    "TJS inline logical right shift uses the defined helper")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "return TJSShiftLeft(AsInteger(), rhs.AsInteger());"
    "TJS inline left shift uses the defined helper")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.cpp"
    "Integer=TJSShiftArithmeticRight(l, rhs.AsInteger());"
    "TJS VM arithmetic right shift uses the defined helper")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.cpp"
    "Integer=TJSShiftLogicalRight(l, rhs.AsInteger());"
    "TJS VM logical right shift uses the defined helper")
require_text("${GENERATED_DIR}/tjs2/tjsVariant.cpp"
    "Integer=TJSShiftLeft(l, rhs.AsInteger());"
    "TJS VM left shift uses the defined helper")
forbid_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "AsInteger()>>(tjs_int)rhs.AsInteger()"
    "TJS inline arithmetic right shift must not use an unchecked count")
forbid_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "(tjs_uint64)AsInteger()>> count"
    "TJS inline logical right shift must not use an unchecked count")
forbid_text("${GENERATED_DIR}/tjs2/tjsVariant.h"
    "AsInteger()<<(tjs_int)rhs.AsInteger()"
    "TJS inline left shift must not invoke signed-overflow undefined behavior")
forbid_text("${GENERATED_DIR}/tjs2/tjsVariant.cpp"
    "Integer=l>>(tjs_int)rhs.AsInteger();"
    "TJS VM arithmetic right shift must not use an unchecked count")
forbid_text("${GENERATED_DIR}/tjs2/tjsVariant.cpp"
    "Integer=(tjs_int64)((tjs_uint64)l>> (tjs_int)rhs);"
    "TJS VM logical right shift must not use an unchecked count")
forbid_text("${GENERATED_DIR}/tjs2/tjsVariant.cpp"
    "Integer=l<<(tjs_int)rhs.AsInteger();"
    "TJS left shift must not invoke signed-overflow undefined behavior")
require_text("${GENERATED_DIR}/tjs2/tjsConfig.cpp"
    "if (!buf) return;"
    "TJS_free preserves the free(nullptr) contract")
require_text("${GENERATED_DIR}/PluginImpl.cpp"
    "TVPThrowExceptionMessage(TVPCannotLoadPlugin, name)"
    "unsupported retail DLL requests fail instead of becoming silent no-ops")
require_text("${GENERATED_DIR}/PluginImpl.cpp"
    "if(!TVPTryLoadPlugin((i->Path"
    "automatic discovery skips Windows TPMs without weakening Plugins.link")
require_text("${GENERATED_DIR}/PluginImpl.cpp"
    "retail-windows-plugin-skipped"
    "skipped Windows TPM discovery is observable on hardware")
require_text("${GENERATED_DIR}/PluginImpl.cpp"
    "filename.length() >= 4"
    "short plugin filenames cannot underflow the extension check")
require_text("${GENERATED_DIR}/PluginImpl.cpp"
    "KRKRVITA_YURI_INTEGRATED_PLUGIN_MODULES"
    "the Vita loader and host compatibility audit share one plugin inventory")
require_text("${GENERATED_DIR}/PluginImpl.cpp"
    "krkrvita/yuri_plugin_capabilities.hpp"
    "the generated sealed loader consumes the capability inventory")
require_text("${SOURCE_DIR}/include/krkrvita/yuri_plugin_capabilities.hpp"
    "X(\"fstat.dll\")"
    "the compatibility inventory exposes the real fstat implementation")
require_text("${SOURCE_DIR}/include/krkrvita/yuri_plugin_capabilities.hpp"
    "yuri_plugin_has_script_surface"
    "the compatibility inventory distinguishes load-only modules from API surfaces")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_fstat_module.cpp"
    "NCB_ATTACH_FUNCTION(dirlist, Storages, fstat_dirlist)"
    "fstat supplies the Storages.dirlist API requested by KAGEX games")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_fstat_module.cpp"
    "retail-fstat-ready"
    "fstat registration is observable on hardware")
require_text("${COMPILE_COMMANDS}"
    "src/engine/retail/yuri_fstat_module.cpp"
    "the fstat compatibility implementation is compiled into the Vita backend")
require_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "Handle.wait(lk, [this] { return Signaled; })"
    "Yuri thread events retain auto-reset signals and reject spurious wakes")
require_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "_this->_cond.wait(lk, [_this] { return !_this->Suspended; })"
    "suspended Yuri workers cannot lose a resume before entering their wait")
require_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "std::lock_guard<std::mutex> lk(_mutex);"
    "Yuri Resume synchronizes the suspended state with the worker predicate")
require_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "pthread_setschedparam(Handle, SCHED_OTHER, &npri) != 0"
    "all Yuri worker priority requests use Vita pthread's supported policy and check failure")
require_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "vita_pthread_priority_for_yuri_rank(rank)"
    "all seven Yuri worker priorities map monotonically onto Vita pthread priorities")
require_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "yuri-thread-priority-apply-failed"
    "a rejected Vita worker priority is observable instead of silently ignored")
forbid_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "policy = 5/*SCHED_IDLE*/"
    "Vita cannot receive Yuri's unsupported Linux SCHED_IDLE policy")
forbid_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "policy = 3/*SCHED_BATCH*/"
    "Vita cannot receive Yuri's unsupported Linux SCHED_BATCH policy")
forbid_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "policy = 2/*SCHED_RR*/"
    "Vita cannot receive Yuri's unsupported SCHED_RR policy")
require_text("${SOURCE_DIR}/include/krkrvita/vita_thread_policy.hpp"
    "kVitaMainThreadPolicy{160, 0x00010000}"
    "the engine uses Sony's common/default queue so higher Yuri workers can preempt it")
require_text("${SOURCE_DIR}/include/krkrvita/vita_pthread_priority.hpp"
    "kVitaPthreadPriorityMin = 128"
    "the Yuri mapping matches Vita pthread-embedded's documented minimum")
require_text("${SOURCE_DIR}/include/krkrvita/vita_pthread_priority.hpp"
    "kVitaPthreadPriorityDefault = 160"
    "the Yuri normal rank retains Vita pthread-embedded's default")
require_text("${SOURCE_DIR}/include/krkrvita/vita_pthread_priority.hpp"
    "kVitaPthreadPriorityMax = 191"
    "the Yuri mapping matches Vita pthread-embedded's documented maximum")
require_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "TJS_tTVInt_to_str(3, tmp)"
    "Yuri uses the Vita's three application CPU cores")
require_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "std::call_once(log_once"
    "Vita CPU detection is initialized and logged exactly once")
require_text("${GENERATED_DIR}/SysInitImpl.cpp"
    "tjs_int drawThreadNum = 0;"
    "Vita preserves Yuri's automatic draw-thread command-line semantics")
require_text("${GENERATED_DIR}/SysInitImpl.cpp"
    "yuri-render-tasks-hybrid-large"
    "the selected large-only hybrid render policy is observable on hardware")
require_text("${GENERATED_DIR}/SysInitImpl.cpp"
    "else if(drawThreadNum == 1)"
    "explicit -drawthread=1 remains a distinct serial policy")
require_text("${GENERATED_DIR}/SysInitImpl.cpp"
    "yuri-render-tasks-android-serial"
    "an explicit serial draw-thread selection remains observable on hardware")
require_text("${GENERATED_DIR}/SysInitImpl.cpp"
    "select_exact_additive_alpha(TVPAlphaBlend_a, TVPAlphaBlend_a_c)"
    "ltAddAlpha child composition retains the generated scalar pixel contract")
require_text("${GENERATED_DIR}/SysInitImpl.cpp"
    "select_exact_additive_alpha(TVPApplyColorMap_a, TVPApplyColorMap_a_c)"
    "text rasterization into ltAddAlpha layers retains desktop alpha rounding")
require_text("${GENERATED_DIR}/SysInitImpl.cpp"
    "yuri-additive-alpha-scalar-exact-ready"
    "the additive-alpha compatibility policy is observable on hardware")
require_text_order_between("${GENERATED_DIR}/ThreadImpl.cpp"
    "void TVPExecThreadTask(int numThreads, TVP_THREAD_TASK_FUNC func)"
    "//---------------------------------------------------------------------------"
    "if(numThreads <= 1)"
    "krkrvita::RenderTaskPool pool(2"
    "serial and small hybrid work cannot construct or synchronize the pool")
require_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "krkrvita::RenderTaskPool pool(2"
    "large hybrid work uses two persistent policy-checked workers plus its owner")
require_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "yuri-render-task-pool-ready"
    "the hybrid render pool becomes observable only after worker initialization")
require_text("${GENERATED_DIR}/ThreadIntf.h"
    "krkrvita::dispatch_render_tasks("
    "the Vita dispatch boundary checks task count before owning conversion")
forbid_text("${GENERATED_DIR}/ThreadIntf.h"
    "TVPReportSerialThreadTaskPath"
    "the hot serial dispatch path cannot make an out-of-line trace call")
require_text_order_between("${GENERATED_DIR}/ThreadIntf.h"
    "inline void TVPExecThreadTaskVita"
    "#endif"
    "krkrvita::dispatch_render_tasks("
    "std::function<void(int)>("
    "std::function ownership is confined to the dispatcher's parallel adapter")
require_text("${SOURCE_DIR}/include/krkrvita/render_task_dispatch.hpp"
    "if (task_count <= 1)"
    "the serial dispatch primitive invokes the concrete closure directly")
require_text("${SOURCE_DIR}/include/krkrvita/render_task_policy.hpp"
    "hybrid_render_task_pixel_threshold = 512 * 512"
    "small dirty rectangles stay serial below the fixed 262144-pixel threshold")
require_text("${SOURCE_DIR}/include/krkrvita/render_task_policy.hpp"
    "static_cast<float>(pixel_count) < operation_factor * 500.0f"
    "the hybrid policy preserves Yuri's operation-factor threshold")
require_text("${SOURCE_DIR}/include/krkrvita/render_task_policy.hpp"
    "available_threads < row_count"
    "the hybrid task count cannot exceed the primitive height")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "pixelNum, factor, rowCount, TVPGetThreadNum()"
    "adaptive render selection combines the fixed, factor, height, and draw-thread policies")
require_text_count("${GENERATED_DIR}/RenderManager.cpp"
    "GetAdaptiveThreadNum(w * h, THREAD_FACTOR, h)" 6
    "all six templated adaptive rectangle primitives clamp tasks to their height")
require_text_count("${GENERATED_DIR}/RenderManager.cpp"
    "GetAdaptiveThreadNum(w * h, 150, h)" 1
    "FillARGB clamps its adaptive task count to its height")
require_text_count("${GENERATED_DIR}/RenderManager.cpp"
    "GetAdaptiveThreadNum(w * h, 66, h)" 1
    "direct-copy clamps its adaptive task count to its height")
forbid_text("${GENERATED_DIR}/RenderManager.cpp"
    "GetAdaptiveThreadNum(w * h, THREAD_FACTOR)"
    "an adaptive rectangle primitive cannot request empty row jobs")
require_text_count("${GENERATED_DIR}/RenderManager.cpp"
    "TVPExecThreadTaskVita(taskNum," 13
    "all thirteen rectangle, resize, and warp sites inspect count before owning dispatch")
forbid_text("${GENERATED_DIR}/RenderManager.cpp"
    "TVPExecThreadTask(taskNum,"
    "no RenderManager lambda is type-erased before its task count is inspected")
require_text_count("${GENERATED_DIR}/ResampleImage.cpp"
    "TVPExecThreadTaskVita(threadNum," 1
    "the active resampler task site uses allocation-free serial dispatch")
forbid_text("${GENERATED_DIR}/ResampleImage.cpp"
    "TVPExecThreadTask(threadNum,"
    "the serial resampler cannot type-erase its closure before dispatch")
forbid_text("${GENERATED_DIR}/ThreadImpl.cpp"
    "#pragma omp parallel for"
    "a nonfunctional OpenMP pragma cannot masquerade as Vita parallelism")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "TVPTextureBackingOverlaps"
    "render parallelism gates separate wrappers over shared bitmap backing")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "TVPUnsafeTargetAlias"
    "shifted blend aliases retain serial ordering")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "textures[i].first->GetScanLineForRead(0); // prepare pixel data for compressed texture"
    "render sources are materialized on the owner before a parallel barrier")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "if(!krkrvita::RenderTaskPool::in_worker_context())\n\t\t\tBitmap->IsOpaque = false;"
    "only the owner task mutates shared target opacity metadata")
require_text("${GENERATED_DIR}/RenderManager.cpp"
    "overlapping triangles are order-dependent"
    "triangle-index work cannot race overlapping output pixels")
require_text("${GENERATED_DIR}/ResampleImage.cpp"
    "threadNum = 1;"
    "resampling stays serial until its bitmap preparation is worker-safe")
require_text("${GENERATED_DIR}/ThreadImpl.h"
    "bool Signaled = false;"
    "Yuri thread events store their signaled state")
require_text("${GENERATED_DIR}/ThreadImpl.h"
    "#include <mutex>"
    "Yuri synchronization types do not rely on transitive VitaSDK includes")
require_text("${GENERATED_DIR}/ThreadImpl.h"
    "std::atomic<bool> Terminated;"
    "Yuri worker termination is race-free")
require_text("${GENERATED_DIR}/TimerImpl.cpp"
    "krkrvita_boot_trace(\"yuri-timer-thread-policy-ready\")"
    "the timer's actual Vita kernel priority is verified on its own thread")
require_text("${GENERATED_DIR}/TimerImpl.cpp"
    "sceKernelGetThreadCurrentPriority() != expected_native_priority"
    "the timer checks pthread priority inversion reached the Vita kernel")
require_text("${GENERATED_DIR}/TimerImpl.cpp"
    "krkrvita_boot_trace(\"yuri-first-timer-fired\")"
    "the first KAG timer trigger is observable on hardware")
require_text("${GENERATED_DIR}/TimerImpl.cpp"
    "krkrvita_boot_trace(\"yuri-first-timer-dispatched\")"
    "the first KAG timer delivery is observable on hardware")
require_text_count("${GENERATED_DIR}/KAGParser.cpp"
    "krkrvita::vita_kag_should_emit_log(DebugLevel, tkdlSimple)" 3
    "Vita preserves every KAG simple scenario-log gate")
require_text_count("${GENERATED_DIR}/KAGParser.cpp"
    "krkrvita::vita_kag_should_emit_log(DebugLevel, tkdlVerbose)" 4
    "Vita clamps every KAG verbose per-tag log gate")
forbid_text("${GENERATED_DIR}/KAGParser.cpp"
    "if(DebugLevel >= tkdlVerbose)"
    "raw KAG verbose gates bypass the Vita log policy")
require_text("${GENERATED_DIR}/KAGParser.cpp"
    "krkrvita_boot_trace(\"yuri-kag-verbose-log-clamped\")"
    "the KAG verbose-log clamp is observable on hardware")
require_text("${GENERATED_DIR}/KAGParser.cpp"
    "krkrvita::assemble_kag_inline_script<tjs_char>"
    "KAG inline scripts use bounded pre-sized assembly")
forbid_text_between("${GENERATED_DIR}/KAGParser.cpp"
    "bool tTJSNI_KAGParser::SkipCommentOrLabel()"
    "void tTJSNI_KAGParser::PushMacroArgs"
    "script += p;"
    "KAG inline scripts do not append and reallocate once per line")
require_text("${GENERATED_DIR}/KAGParser.cpp"
    "yuri-kag-large-inline-assembly-complete"
    "large KAG inline assembly is observable on hardware")
require_text("${GENERATED_DIR}/KAGParser.cpp"
    "yuri-kag-large-inline-execution-entered"
    "large KAG inline execution entry is observable on hardware")
require_text("${GENERATED_DIR}/KAGParser.cpp"
    "yuri-kag-large-inline-execution-complete"
    "large KAG inline execution completion is observable on hardware")
require_text("${SOURCE_DIR}/include/krkrvita/kag_log_policy.hpp"
    "return requested > 1 ? 1 : requested;"
    "the KAG policy retains simple diagnostics while clamping verbose output")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_pvr.cpp"
    "krkrvita_boot_trace(\"yuri-direct-texture-fallback\")"
    "unsupported direct textures fall back observably to Yuri's bitmap loaders")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_pvr.cpp"
    "return nullptr;"
    "the Yuri loader can decline unsupported direct texture containers")
require_text("${GENERATED_DIR}/extrans/ripple.cpp"
    "TVPRippleTransform_f = TVPRippleTransform_c_f"
    "the extrans ripple handler uses a portable ARM kernel")
require_text("${GENERATED_DIR}/layerExSave/savepng.cpp"
    "NCB_ATTACH_FUNCTION(saveLayerImagePng"
    "the sample's Layer.saveLayerImagePng API is registered")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_extrans_module.cpp"
    "retail-extrans-ready"
    "the sample's extrans registration is observable on hardware")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_extnagano_module.cpp"
    "NCB_MODULE_NAME TJS_W(\"extNagano.dll\")"
    "extNagano registers under its published module name")
foreach(extnagano_transition
    3duniversal blurfade scanline zoomfade rgbfade spin flutter book
    imagewipe honeyturn morphing multiripple)
    require_text("${SOURCE_DIR}/src/engine/retail/yuri_extnagano_module.cpp"
        "TJS_W(\"${extnagano_transition}\")"
        "extNagano registers the documented ${extnagano_transition} transition")
endforeach()
require_text("${SOURCE_DIR}/src/engine/retail/yuri_extnagano_module.cpp"
    "tTVPCrossFadeTransHandlerProvider"
    "extNagano uses Yuri's transition provider ABI for its fallback")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_extnagano_module.cpp"
    "retail-extnagano-crossfade-fallback-ready"
    "extNagano fallback registration is observable on hardware")
require_text("${COMPILE_COMMANDS}"
    "src/engine/retail/yuri_extnagano_module.cpp"
    "the extNagano compatibility module is compiled into the Vita backend")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_krflash_module.cpp"
    "NCB_MODULE_NAME TJS_W(\"krflash.dll\")"
    "krflash registers under its Windows module name")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_krflash_module.cpp"
    "retail-krflash-load-only-ready"
    "krflash load-only fallback is observable on hardware")
require_text("${COMPILE_COMMANDS}"
    "src/engine/retail/yuri_krflash_module.cpp"
    "the krflash compatibility module is compiled into the Vita backend")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_gfxeffect_module.cpp"
    "NCB_MODULE_NAME TJS_W(\"gfxEffect.dll\")"
    "gfxEffect registers under its Windows module name")
foreach(gfxeffect_symbol
    init cycle updateWarpMap updateCoolMap applyBlue applyRed applyWhite
    applyCustom clearFireSeed setFireSeed setFireSeedPos setCustomColorTable
    seedLayer targetLayer seedX seedY randomSeed forceH forceV boundRangeH
    boundRangeV scalingCoeff numOfBlurForCoolMap textureFilterType coolRange
    coolStrength coolParticleDensityDenominator coolParticleDensityNumerator
    edgeSmoothing)
    require_text("${SOURCE_DIR}/src/engine/retail/yuri_gfxeffect_module.cpp"
        "${gfxeffect_symbol}"
        "gfxEffect preserves the documented gfxFire ${gfxeffect_symbol} API")
endforeach()
require_text("${SOURCE_DIR}/src/engine/retail/yuri_gfxeffect_module.cpp"
    "retail-gfxeffect-fallback-ready"
    "gfxEffect fallback registration is observable on hardware")
require_text("${COMPILE_COMMANDS}"
    "src/engine/retail/yuri_gfxeffect_module.cpp"
    "the gfxEffect compatibility module is compiled into the Vita backend")
require_text("${SOURCE_DIR}/include/krkrvita/motionplayer_surface.hpp"
    "global.Motion.ResourceManager = KrkrVitaMotionResourceManager"
    "motionplayer exposes the ResourceManager script class")
require_text("${SOURCE_DIR}/include/krkrvita/motionplayer_surface.hpp"
    "global.Motion.Player = KrkrVitaMotionPlayer"
    "motionplayer exposes the Player script class")
require_text("${SOURCE_DIR}/include/krkrvita/motionplayer_surface.hpp"
    "global.Motion.SeparateLayerAdaptor = KrkrVitaSeparateLayerAdaptor"
    "motionplayer exposes the separate-layer script class")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_motionplayer_module.cpp"
    "motionplayer_surface_script"
    "motionplayer registers the tested shared TJS surface")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_motionplayer_module.cpp"
    "retail-motionplayer-surface-ready"
    "motionplayer registration is observable on hardware")
require_text("${COMPILE_COMMANDS}"
    "src/engine/retail/yuri_motionplayer_module.cpp"
    "the motionplayer compatibility surface is compiled into the Vita backend")
require_text("${SOURCE_DIR}/include/krkrvita/layerexdraw_surface.hpp"
    "global.Layer.drawImageAffine"
    "layerExDraw exposes the Layer affine-copy script surface")
require_text("${SOURCE_DIR}/include/krkrvita/layerexdraw_surface.hpp"
    "global.GdiPlus.Image = KrkrVitaGdiPlusImage"
    "layerExDraw exposes the GdiPlus image script surface")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_layerexdraw_module.cpp"
    "layerexdraw_surface_script"
    "layerExDraw registers the tested shared TJS surface")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_layerexdraw_module.cpp"
    "retail-layerexdraw-surface-ready"
    "layerExDraw registration is observable on hardware")
require_text("${COMPILE_COMMANDS}"
    "src/engine/retail/yuri_layerexdraw_module.cpp"
    "the layerExDraw compatibility surface is compiled into the Vita backend")
# scriptsEx is the vendored upstream implementation, not a script surface.
# A TJS fallback cannot answer getObjectCount at all, because TJS2 dictionaries
# expose no "count" member, so require the real ncbind registration.
require_text("${SOURCE_DIR}/third_party/scriptsEx/scriptsEx.cpp"
    "NCB_ATTACH_CLASS(ScriptsAdd, Scripts)"
    "scriptsEx attaches to Kirikiri's built-in Scripts class")
require_text("${SOURCE_DIR}/third_party/scriptsEx/scriptsEx.cpp"
    "RawCallback(TJS_W(\"getObjectCount\"), &ScriptsAdd::getCount"
    "scriptsEx registers the native startup object-count member")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_scriptsex_module.cpp"
    "retail-scriptsex-surface-ready"
    "scriptsEx registration is observable on hardware")
require_text("${COMPILE_COMMANDS}"
    "third_party/scriptsEx/scriptsEx.cpp"
    "the upstream scriptsEx implementation is compiled into the Vita backend")
require_text("${COMPILE_COMMANDS}"
    "src/engine/retail/yuri_scriptsex_module.cpp"
    "the scriptsEx boot trace is compiled into the Vita backend")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_layerexsave_probe.cpp"
    "retail-layerexsave-ready"
    "the sample's layerExSave registration is observable on hardware")
require_text("${GENERATED_DIR}/layerExImage/LayerExImage.h"
    "class layerExImage : public layerExBase_GL"
    "layerExImage uses Yuri's software-renderer pixel adapter")
require_text("${GENERATED_DIR}/layerExImage/LayerExImage.h"
    "layerExImage(DispatchT obj) : layerExBase_GL(obj)"
    "layerExImage constructs the selected Yuri layer adapter")
require_text("${GENERATED_DIR}/layerExImage/LayerExImage.cpp"
    "layerExBase_GL::reset();"
    "layerExImage refreshes its writable software pixel buffer per call")
require_text("${GENERATED_DIR}/layerExImage/LayerExImage.cpp"
    "typedef struct tagRGBQUAD"
    "layerExImage retains the published BGRA colour layout on Vita")
require_text("${GENERATED_DIR}/layerExImage/Main.cpp"
    "NCB_MODULE_NAME TJS_W(\"layerExImage.dll\")"
    "Plugins.link resolves layerExImage through the internal module registry")
require_text("${GENERATED_DIR}/layerExImage/Main.cpp"
    "retail-layereximage-ready"
    "layerExImage method registration is observable on hardware")
foreach(layereximage_method
    light colorize modulate noise generateWhiteNoise gaussianBlur)
    require_text("${GENERATED_DIR}/layerExImage/Main.cpp"
        "NCB_METHOD(${layereximage_method})"
        "layerExImage registers its published Layer.${layereximage_method} API")
endforeach()

# Squirrel is a real runtime dependency of KAGEX titles (not an optional
# Windows cosmetic module). Keep the generated Vita VM and the UTF-16 bridge
# tied to the authoritative source so a future source refresh cannot silently
# fall back to 32-bit wchar_t or an unpatched Windows wide-C call.
require_text("${GENERATED_DIR}/squirrel/include/squirrel.h"
    "typedef char16_t SQChar;"
    "Squirrel keeps its published UTF-16 SQChar ABI on Vita")
require_text("${GENERATED_DIR}/squirrel/include/squirrel.h"
    "yuri_squirrel_compat.hpp"
    "Squirrel public headers include the Vita UTF-16 compatibility bridge")
forbid_text("${GENERATED_DIR}/squirrel/include/squirrel.h"
    "typedef wchar_t SQChar"
    "the Vita Squirrel ABI must not expose host wchar_t")
foreach(squirrel_wrapper
    krkrvita_Main.cpp krkrvita_sqfile.cpp krkrvita_sqstdio.cpp
    krkrvita_sqtjsobj.cpp krkrvita_sqwrapper.cpp)
    require_text("${GENERATED_DIR}/squirrel/${squirrel_wrapper}"
        "combase.h"
        "Squirrel wrapper ${squirrel_wrapper} uses Yuri's IStream boundary")
endforeach()
require_text("${GENERATED_DIR}/squirrel/krkrvita_Main.cpp"
    "NCB_MODULE_NAME TJS_W(\"squirrel.dll\")"
    "Squirrel registers under the published squirrel.dll module name")
require_text("${GENERATED_DIR}/squirrel/krkrvita_Main.cpp"
    "retail-squirrel-ready"
    "Squirrel registration is observable on physical hardware")
require_text("${GENERATED_DIR}/squirrel/krkrvita_sqstdio.cpp"
    "TVPCreateIStream(filename"
    "Squirrel file streams use Yuri's UTF-16-aware IStream boundary")
require_text("${GENERATED_DIR}/squirrel/vm/14_sqstdstring.cpp"
    "krkrvita::squirrel::strtok16"
    "Squirrel string tokenization does not call 32-bit wchar_t wcstok")
require_text("${GENERATED_DIR}/squirrel/vm/15_sqstdsystem.cpp"
    "krkrvita::squirrel::getenv16"
    "Squirrel system helpers use the Vita UTF-16 environment bridge")
require_text("${COMPILE_COMMANDS}"
    "generated/yuri/squirrel/krkrvita_Main.cpp"
    "the generated Squirrel module is compiled into the Vita backend")
require_text("${COMPILE_COMMANDS}"
    "generated/yuri/squirrel/vm/15_sqstdstring.cpp"
    "the generated Squirrel standard string library is compiled into the Vita backend")
require_text("${COMPILE_COMMANDS}"
    "generated/yuri/squirrel/vm/28_sqvm.cpp"
    "the generated Squirrel VM core is compiled into the Vita backend")
forbid_text("${COMPILE_COMMANDS}"
    "src/plugins/win32/squirrel/Main.cpp"
    "the vendor Windows Squirrel wrapper must not replace the Vita overlay")

# PSD is a shared KAGEX dependency (Torikago and Noble Works both load it
# during startup).  Keep the maintained parser/class boundary generated and
# ensure the static registration is the published psd.dll module, not the
# similarly named psbfile.dll compatibility module.
require_text("${GENERATED_DIR}/psdfile/psdclass.h"
    "NCB_MODULE_NAME TJS_W(\"psd.dll\")"
    "PSD registers under the published psd.dll module name")
require_text("${GENERATED_DIR}/psdfile/main.cpp"
    "krkrvita_psd_create_binary_stream(stream)"
    "PSD layer images use the Vita IStream-to-binary-stream bridge")
require_text("${GENERATED_DIR}/psdfile/main.cpp"
    "retail-psd-ready"
    "PSD registration is observable on physical hardware")
require_text("${GENERATED_DIR}/psdfile/psdclass_loadstream.cpp"
    "TVPCreateIStream(filename"
    "PSD input uses Yuri's storage-backed IStream boundary")
require_text("${COMPILE_COMMANDS}"
    "generated/yuri/psdfile/main.cpp"
    "the generated PSD module is compiled into the Vita backend")
require_text("${COMPILE_COMMANDS}"
    "generated/yuri/psdfile/psdparse/psdparse.cpp"
    "the maintained PSD parser is compiled into the Vita backend")
forbid_text("${COMPILE_COMMANDS}"
    "/cpp/plugins/psdfile/main.cpp"
    "the fetched vendor PSD main must not replace the generated overlay")

require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
    "NCB_MODULE_NAME TJS_W(\"shrinkCopy.dll\")"
    "shrinkCopy is registered under its published module name")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
    "NCB_ATTACH_FUNCTION(shrinkCopy, Layer"
    "shrinkCopy installs the published Layer.shrinkCopy API")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
    "NCB_ATTACH_FUNCTION(shrinkCopyFast, Layer"
    "shrinkCopy installs the published Layer.shrinkCopyFast API")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
	"if (horzCount > SIZE_MAX - vertCount) return 0;"
	"shrinkCopy checks coefficient-count addition before allocation")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
	"if (!ret) return 0;"
	"shrinkCopy handles coefficient allocation failure before pointer arithmetic")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
	"const size_t rowCount = static_cast<size_t>(stepy < sih ? stepy : sih);"
	"shrinkCopyFast bounds its temporary storage to rows it can consume")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
	"if (rowBytes && rowCount > SIZE_MAX / rowBytes)"
	"shrinkCopyFast checks temporary-buffer multiplication")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
	"if (bufferBytes > static_cast<size_t>(LONG_MAX))"
	"shrinkCopyFast keeps its signed row offsets representable")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
	"siw / stepx + (siw % stepx != 0)"
	"shrinkCopyFast computes ceiling division without signed overflow")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
	"const size_t entryCount = horzCount + vertCount;"
	"shrinkCopy allocates only its horizontal and vertical coefficient tables")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp"
    "retail-shrinkcopy-ready"
    "shrinkCopy registration is observable on hardware")
require_text("${SOURCE_DIR}/include/krkrvita/yuri_plugin_capabilities.hpp"
    "X(\"sqlite3.dll\")"
    "the sealed loader exposes the exact SQLite compatibility module")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_sqlite_module.cpp"
    "NCB_MODULE_NAME TJS_W(\"sqlite3.dll\")"
    "the official-compatible SQLite adapter registers the requested module")
require_text("${SOURCE_DIR}/src/engine/retail/yuri_sqlite_module.cpp"
    "retail-sqlite3-ready"
    "SQLite registration is observable on hardware")
require_text("${COMPILE_COMMANDS}"
    "src/engine/retail/yuri_sqlite_module.cpp"
    "the SQLite compatibility implementation is compiled into the Vita backend")
require_text("${GENERATED_DIR}/PluginImpl.cpp"
    "TJS_W(\"krmovie.dll\")"
    "the Yuri core movie-loader alias is accepted during KAG startup")
require_text("${GENERATED_DIR}/PluginImpl.cpp"
    "retail-krmovie-core-alias-ready"
    "the core movie-loader alias is observable on hardware")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_video_overlay.cpp"
    "avformat_open_input"
    "the Vita movie backend opens Yuri IStream content with FFmpeg")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_video_overlay.cpp"
    "GetVideoLayerObject"
    "the Vita movie backend implements Yuri's software-layer movie mode")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_video_overlay.cpp"
    "krkrvita_vitagl_submit_video_frame"
    "overlay movies hand CPU RGBA to the final framebuffer presenter")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_video_overlay.cpp"
    "yuri-ffmpeg-movie-backend-ready"
    "the FFmpeg movie backend is observable on physical hardware")
forbid_text("${SOURCE_DIR}/src/platform/vita/yuri_video_overlay.cpp"
    "Vita video overlay backend is unavailable"
    "the linked movie ABI still contains the unavailable implementation")
require_text("${SOURCE_DIR}/src/engine/vita/vita_launch.cpp"
    "if(select_filter && !embedded.filter.empty())"
    "exact manifest filter selection precedes game-directory fallback")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "retail-runtime-30s-stable-with-video"
    "sustained on-device startup/rendering is observable")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "iTVPTexture2D::RecycleProcess()"
    "the frontend-free Vita loop fulfills Yuri's per-frame texture lifetime contract")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "yuri-texture-recycler-drained"
    "actual deferred texture destruction is observable on hardware")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_openal_mixer.cpp"
    "yuri-audio-first-play-started"
    "actual on-device audio playback is observable")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "vita-threading-self-test-passed"
    "Vita pthread and libstdc++ synchronization is tested before Yuri starts")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "static_cast<unsigned int>(krkrvita::kVitaNewlibHeapBytes)"
    "Yuri's TJS/STL and decoded bitmaps use the shared Vita memory budget")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_main.cpp"
    "sceUserMainThreadStackSize = 2u * 1024u * 1024u"
    "Kirikiri's nested script/render path has an explicit Vita main stack")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_threading_self_test.cpp"
    "event.WaitFor(250)"
    "Yuri's signal-before-wait event semantics are tested on hardware")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_threading_self_test.cpp"
    "SuspendedThreadProbe probe"
    "Yuri's immediate suspended-thread resume race is tested on hardware")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_storage_preflight.cpp"
    "yuri-project-xp3-opened"
    "Yuri opens the selected retail storage through its own media layer")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_storage_preflight.cpp"
    "retail-project-data-xp3-selected"
    "standard Kirikiri data.xp3 project discovery is present")
require_text("${SOURCE_DIR}/src/platform/vita/yuri_storage_preflight.cpp"
    "require_openable(TJS_W(\"startup.tjs\")"
    "the on-device gate opens startup.tjs through the selected archive")

file(READ "${COMPILE_COMMANDS}" compile_commands)
string(JSON compile_command_count LENGTH "${compile_commands}")
math(EXPR compile_command_last "${compile_command_count} - 1")
foreach(compile_command_index RANGE 0 ${compile_command_last})
    string(JSON compile_source GET "${compile_commands}"
        ${compile_command_index} file)
    string(FIND "${compile_source}" "/vendor/yuri/" vendor_yuri_offset)
    string(FIND "${compile_source}" "/generated/yuri/" generated_yuri_offset)
    if(NOT vendor_yuri_offset LESS 0 OR NOT generated_yuri_offset LESS 0)
        string(JSON compile_command GET "${compile_commands}"
            ${compile_command_index} command)
        string(FIND "${compile_command}" "-I${GENERATED_DIR}"
            generated_include_offset)
        string(FIND "${compile_command}"
            "-I${SOURCE_DIR}/vendor/yuri/src/core"
            vendor_include_offset)
        if(generated_include_offset LESS 0 OR vendor_include_offset LESS 0 OR
           NOT generated_include_offset LESS vendor_include_offset)
            message(FATAL_ERROR
                "Generated Yuri ABI overlays do not precede vendor headers: ${compile_source}")
        endif()
    endif()
endforeach()
foreach(required_source
    "generated/yuri/EventIntf.cpp"
    "vendor/yuri/src/plugins/addFont.cpp"
    "generated/yuri/GraphicsLoaderIntf.cpp"
    "generated/yuri/LayerBitmapIntf.cpp"
    "generated/yuri/LayerBitmapImpl.cpp"
    "generated/yuri/LayerIntf.cpp"
    "generated/yuri/RenderManager.cpp"
    "generated/yuri/TransIntf.cpp"
    "generated/yuri/xp3filter.cpp"
    "generated/yuri/ThreadImpl.cpp"
    "generated/yuri/LayerManager.cpp"
    "generated/yuri/TimerImpl.cpp"
    "generated/yuri/KAGParser.cpp"
    "generated/yuri/extrans/wave.cpp"
    "generated/yuri/extrans/mosaic.cpp"
    "generated/yuri/extrans/turn.cpp"
    "generated/yuri/extrans/turntrans_table.cpp"
    "generated/yuri/extrans/rotatetrans.cpp"
    "generated/yuri/extrans/ripple.cpp"
    "src/engine/retail/yuri_extrans_module.cpp"
    "generated/yuri/layerExSave/savepng.cpp"
    "generated/yuri/layerExSave/savetlg5.cpp"
    "generated/yuri/layerExSave/utils.cpp"
    "src/engine/retail/yuri_layerexsave_probe.cpp"
    "generated/yuri/layerExImage/LayerExImage.cpp"
    "generated/yuri/layerExImage/Main.cpp"
    "src/engine/retail/yuri_shrink_copy.cpp"
    "src/engine/retail/yuri_motionplayer_module.cpp"
    "src/engine/retail/yuri_layerexdraw_module.cpp"
    "third_party/scriptsEx/scriptsEx.cpp"
    "src/engine/retail/yuri_scriptsex_module.cpp"
    "src/platform/vita/yuri_threading_self_test.cpp"
    "src/platform/vita/yuri_performance.cpp"
    "src/platform/vita/yuri_storage_preflight.cpp"
    "src/platform/vita/yuri_openal_mixer.cpp"
    "src/platform/vita/yuri_video_overlay.cpp"
    "src/platform/vita/yuri_pvr.cpp"
    "src/platform/vita/yuri_window_layer.cpp"
    "src/engine/vita/vitagl_presenter.cpp")
    string(FIND "${compile_commands}" "${required_source}" offset)
    if(offset LESS 0)
        message(FATAL_ERROR
            "Required Yuri Vita source is not compiled: ${required_source}")
    endif()
endforeach()

foreach(forbidden_source
    "vendor/yuri/src/core/base/EventIntf.cpp"
    "vendor/krkrsdl2"
    "/environ/android/"
    "/environ/ui/"
    "src/platform/vita/yuri_pvf_font_rasterizer.cpp"
    "generated/yuri/RenderManager_ogl.cpp"
    "vendor/yuri/src/core/visual/GraphicsLoaderIntf.cpp"
    "vendor/yuri/src/core/visual/LayerBitmapIntf.cpp"
    "vendor/yuri/src/core/visual/win32/LayerBitmapImpl.cpp"
    "vendor/yuri/src/core/visual/LayerIntf.cpp"
    "vendor/yuri/src/core/visual/RenderManager.cpp"
    "vendor/yuri/src/core/visual/TransIntf.cpp"
    "vendor/yuri/src/core/visual/LoadPVRv3.cpp"
    "layerExMovie.cpp"
    "PIB")
    string(FIND "${compile_commands}" "${forbidden_source}" offset)
    if(NOT offset LESS 0)
        message(FATAL_ERROR
            "Frontend or forbidden renderer leaked into Yuri Vita: ${forbidden_source}")
    endif()
endforeach()

message(STATUS "Yuri generated-source and backend-boundary contracts passed")

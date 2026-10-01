function(krkrvita_set_yuri_backend_target_defaults target)
    set(yuri_core "${KRKRVITA_YURI_SOURCE_DIR}/src/core")
    set(yuri_texture_abi_header
        "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/RenderManager.h")
    set(yuri_freetype_abi_header
        "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/FreeType.h")
    set(yuri_object_list_header
        "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/ObjectList.h")
    set(yuri_thread_dispatch_header
        "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/ThreadIntf.h")
    set(yuri_layer_bitmap_abi_header
        "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/LayerBitmapImpl.h")
    set(yuri_font_rasterizer_abi_header
        "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/FontRasterizer.h")
    set(yuri_freetype_rasterizer_abi_header
        "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/FreeTypeFontRasterizer.h")
    if(NOT EXISTS "${yuri_texture_abi_header}")
        message(FATAL_ERROR
            "Yuri generated texture ABI header is missing for ${target}")
    endif()
    if(NOT EXISTS "${yuri_freetype_abi_header}")
        message(FATAL_ERROR
            "Yuri generated FreeType ABI header is missing for ${target}")
    endif()
    if(NOT EXISTS "${yuri_object_list_header}")
        message(FATAL_ERROR
            "Yuri generated ObjectList header is missing for ${target}")
    endif()
    if(NOT EXISTS "${yuri_thread_dispatch_header}")
        message(FATAL_ERROR
            "Yuri generated thread-dispatch header is missing for ${target}")
    endif()
    foreach(yuri_text_abi_header
            "${yuri_layer_bitmap_abi_header}"
            "${yuri_font_rasterizer_abi_header}"
            "${yuri_freetype_rasterizer_abi_header}")
        if(NOT EXISTS "${yuri_text_abi_header}")
            message(FATAL_ERROR
                "Yuri generated text ABI header is missing for ${target}: ${yuri_text_abi_header}")
        endif()
    endforeach()
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
    )
    target_include_directories(${target} BEFORE PRIVATE
        # Generated Vita replacements must win over Yuri's source headers;
        # ThreadImpl.h changes object layout and therefore has to be identical
        # in every archive that embeds a tTVPThread or tTVPThreadEvent.
        "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri"
        include
        src/platform/vita
        "${KRKRVITA_YURI_TJS_GENERATED_DIR}"
        "${yuri_core}"
        "${yuri_core}/base"
        "${yuri_core}/base/7zip"
        "${yuri_core}/base/7zip/C"
        "${yuri_core}/base/7zip/CPP"
        "${yuri_core}/base/win32"
        "${yuri_core}/environ"
        "${yuri_core}/environ/win32"
        "${yuri_core}/extension"
        "${yuri_core}/msg"
        "${yuri_core}/msg/win32"
        "${yuri_core}/movie"
        "${yuri_core}/movie/ffmpeg"
        "${yuri_core}/sound"
        "${yuri_core}/sound/win32"
        "${yuri_core}/tjs2"
        "${yuri_core}/utils"
        "${yuri_core}/utils/encoding"
        "${yuri_core}/utils/minizip"
        "${yuri_core}/utils/win32"
        "${yuri_core}/visual"
        "${yuri_core}/visual/ARM"
        "${yuri_core}/visual/gl"
        "${yuri_core}/visual/ogl"
        "${yuri_core}/visual/win32"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/yuri/compat"
        "${VITASDK}/arm-vita-eabi/include/freetype2"
        "${KRKRVITA_YURI_SOURCE_DIR}/src/plugins"
        "${KRKRVITA_YURI_SOURCE_DIR}/src/plugins/ncbind"
    )
    target_compile_definitions(${target} PRIVATE
        TJS_TEXT_OUT_CRLF
        USE_UNICODE_FSTRING
        _7ZIP_ST
    )
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/yuri/tjs_compat.hpp>
        -Wno-deprecated-declarations
        -Wno-implicit-fallthrough
    )

    # A newly generated header can shadow Yuri's source RenderManager.h without
    # appearing in dependency files produced by objects compiled before that
    # header existed.  RenderManager.h contains iTVPTexture2D's object layout
    # and inline ownership methods, so even one stale object would mix ABIs.
    # Make the generated header an explicit prerequisite of every Yuri object;
    # this also invalidates the complete backend whenever that ABI is patched.
    get_target_property(yuri_target_sources ${target} SOURCES)
    if(NOT yuri_target_sources)
        message(FATAL_ERROR "Yuri target ${target} has no source objects")
    endif()
    set_property(SOURCE ${yuri_target_sources} APPEND PROPERTY OBJECT_DEPENDS
        "${yuri_texture_abi_header}"
        "${yuri_freetype_abi_header}"
        "${yuri_object_list_header}"
        "${yuri_thread_dispatch_header}"
        "${yuri_layer_bitmap_abi_header}"
        "${yuri_font_rasterizer_abi_header}"
        "${yuri_freetype_rasterizer_abi_header}")
endfunction()

function(krkrvita_add_yuri_backend_targets)
    if(NOT VITA)
        message(FATAL_ERROR "The Yuri Vita backend targets require the Vita toolchain")
    endif()

    set(yuri_core "${KRKRVITA_YURI_SOURCE_DIR}/src/core")

    # VitaSDK's generic FFmpeg package deliberately omits the MPEG-1/2 and
    # WMV decoders used by retail KiriKiri movies.  Keep the compatibility
    # build in a project-local prefix: this avoids silently changing every
    # other VitaSDK consumer while ensuring headers and archives are from the
    # same FFmpeg release.
    set(KRKRVITA_VITA_FFMPEG_PREFIX
        "${CMAKE_CURRENT_SOURCE_DIR}/.cache/ffmpeg-vita-8.1.1"
        CACHE PATH "FFmpeg prefix with Kirikiri Vita movie codecs")
    foreach(yuri_ffmpeg_component avformat avcodec avutil swresample swscale)
        set(yuri_ffmpeg_archive
            "${KRKRVITA_VITA_FFMPEG_PREFIX}/lib/lib${yuri_ffmpeg_component}.a")
        if(NOT EXISTS "${yuri_ffmpeg_archive}")
            message(FATAL_ERROR
                "Kirikiri Vita FFmpeg archive is missing: ${yuri_ffmpeg_archive}. "
                "Run scripts/build-vita-ffmpeg.sh first.")
        endif()
        if(NOT TARGET krkrvita-vita-${yuri_ffmpeg_component})
            add_library(krkrvita-vita-${yuri_ffmpeg_component} STATIC
                IMPORTED GLOBAL)
            set_target_properties(krkrvita-vita-${yuri_ffmpeg_component}
                PROPERTIES
                    IMPORTED_LOCATION "${yuri_ffmpeg_archive}"
                    INTERFACE_INCLUDE_DIRECTORIES
                        "${KRKRVITA_VITA_FFMPEG_PREFIX}/include")
        endif()
    endforeach()

    # Keep Yuri's archive/storage-facing base as a coherent source unit. The
    # implementation side of its OS contracts will live in the Vita platform
    # target; no krkrz base objects are allowed to satisfy this archive.
    file(GLOB yuri_base_sources CONFIGURE_DEPENDS
        "${yuri_core}/base/*.cpp"
        "${yuri_core}/base/win32/*.cpp"
    )
    list(REMOVE_ITEM yuri_base_sources
        "${yuri_core}/base/win32/FuncStubs.cpp"
        "${yuri_core}/base/win32/SusieArchive.cpp"
    )

    # Vita newlib's POSIX off_t/lseek ABI is 32-bit. Route Yuri's local stream
    # through SceIofilemgr so retail XP3 archives retain 64-bit file offsets.
    set(yuri_storage_impl "${yuri_core}/base/win32/StorageImpl.cpp")
    file(READ "${yuri_storage_impl}" yuri_storage_impl_text)
    string(REPLACE "#include <fcntl.h>"
        "#include <fcntl.h>\n#include <atomic>\n#include <psp2/io/fcntl.h>\n#include \"krkrvita/read_all.hpp\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"krkrvita/vita_storage_path.hpp\"\n#include \"krkrvita/virtual_cd.hpp\"\n#include \"krkrvita/write_all.hpp\""
        yuri_storage_impl_patched "${yuri_storage_impl_text}")
    string(REPLACE "O_RDONLY" "SCE_O_RDONLY"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    string(REPLACE "O_RDWR" "SCE_O_RDWR"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    string(REPLACE "O_CREAT" "SCE_O_CREAT"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    string(REPLACE "O_TRUNC" "SCE_O_TRUNC"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    string(REPLACE "O_APPEND" "SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    string(REPLACE "Handle = open(" "Handle = sceIoOpen("
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    string(REPLACE "read(Handle," "sceIoRead(Handle,"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    string(REPLACE "write(Handle," "sceIoWrite(Handle,"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    string(REPLACE "close(Handle)" "sceIoClose(Handle)"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    string(REPLACE "lseek64(Handle," "sceIoLseek(Handle,"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")

    # Desktop KAGEX titles occasionally use Storages.searchCD as a startup
    # license check. The Vita project directory is already the mounted game
    # volume, so expose it as a virtual CD result instead of making these
    # titles shut down on the empty non-Windows stub. Preserve the desktop
    # empty-label/not-found behavior and return the actual project path for
    # callers that use the result as a storage prefix.
    set(yuri_search_cd_old [=[
	// search CD which has specified volume label name.
	// return drive letter ( such as 'A' or 'B' )
	// return empty string if not found.
#if 0
]=])
    set(yuri_search_cd_new [=[
	// search CD which has specified volume label name.
	// On Vita, the selected project is the mounted virtual game volume.
	if(name.IsEmpty()) return ttstr();
	const ttstr project_path = TVPGetAppPath();
	if(!krkrvita::virtual_cd_is_present(name.AsStdString(),
		project_path.AsStdString())) return ttstr();
	static std::atomic<bool> virtual_cd_reported(false);
	if(!virtual_cd_reported.exchange(true, std::memory_order_relaxed))
		krkrvita_boot_trace("yuri-virtual-cd-search-satisfied");
	return project_path;
#if 0
]=])
    string(REPLACE "${yuri_search_cd_old}" "${yuri_search_cd_new}"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    if(yuri_storage_impl_patched STREQUAL yuri_storage_impl_text)
        message(FATAL_ERROR "Yuri Vita virtual-CD search patch no longer applies")
    endif()

    # Kirikiroid's Android local-file stream buffers every newly written file
    # in a reallocating memory stream, then flushes it from the destructor.
    # On Vita that makes a multi-megabyte save thumbnail consume another
    # complete-file allocation and, on write failure, throws through an
    # implicitly-noexcept tTJSBinaryStream destructor.  Use SceIofilemgr as a
    # real seekable stream instead.  WriteBuffer will report a short/error
    # write at the call site, where the normal script exception path can catch
    # it, and the destructor remains non-throwing.
    set(yuri_vita_buffered_write_marker [=[
		MemBuffer = new tTVPMemoryStream();
		return;
	}
]=])
    set(yuri_vita_direct_write_marker [=[
	}
]=])
    string(REPLACE "${yuri_vita_buffered_write_marker}"
        "${yuri_vita_direct_write_marker}"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")

    set(yuri_vita_failed_open_fallback [=[
	if (Handle < 0) {
		if (access == TJS_BS_APPEND || access == TJS_BS_UPDATE) {
			// use whole file writing
			Handle = sceIoOpen(holder, SCE_O_RDONLY, 0666);
			if (Handle >= 0) {
				tjs_uint64 size = GetSize();
				if (size < 4 * 1024 * 1024) { // only support file size <= 4M
					MemBuffer = new tTVPMemoryStream();
					MemBuffer->SetSize(size);
					sceIoRead(Handle, MemBuffer->GetInternalBuffer(), size);
		}
				sceIoClose(Handle);
				Handle = -1;
			}
		}
		if (!MemBuffer)
		TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
	}
]=])
    set(yuri_vita_failed_open_direct [=[
	if (Handle < 0) {
		TVPThrowExceptionMessage(TVPCannotOpenStorage, origname);
	}
]=])
    string(REPLACE "${yuri_vita_failed_open_fallback}"
        "${yuri_vita_failed_open_direct}"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")

    set(yuri_vita_throwing_file_destructor [=[
bool TVPWriteDataToFile(const ttstr &filepath, const void *data, unsigned int len);
tTVPLocalFileStream::~tTVPLocalFileStream()
{
    if(MemBuffer) {
		if (!TVPWriteDataToFile(FileName, MemBuffer->GetInternalBuffer(), MemBuffer->GetSize())) {
			delete MemBuffer;
			ttstr filename(FileName);
			FileName.~tTJSString();
			free(this);
			TVPThrowExceptionMessage(TJS_W("File Writing Error: %1"), filename);
		}
		delete MemBuffer;
    }
    if (Handle >= 0) {
		sceIoClose(Handle);
	}
]=])
    set(yuri_vita_nothrow_file_destructor [=[
tTVPLocalFileStream::~tTVPLocalFileStream()
{
	delete MemBuffer;
	MemBuffer = nullptr;
	if (Handle >= 0) {
		sceIoClose(Handle);
		Handle = -1;
	}
]=])
    string(REPLACE "${yuri_vita_throwing_file_destructor}"
        "${yuri_vita_nothrow_file_destructor}"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")

    set(yuri_vita_single_write [=[
    return sceIoWrite(Handle, buffer, write_size);
]=])

    # SceIofilemgr is allowed to complete a regular-file request with a
    # positive short count.  Yuri's UTF-16 TextStream performs one Read for
    # the entire file; exposing such a short count silently turns valid saved
    # dictionaries into truncated TJS and reports a misleading syntax error.
    # Join positive pieces here while retaining ordinary EOF/error prefix
    # semantics for every other tTJSBinaryStream caller.
    set(yuri_vita_single_read [=[
    return sceIoRead(Handle, buffer, read_size);
]=])
    set(yuri_vita_complete_read [=[
	auto *bytes = static_cast<unsigned char *>(buffer);
	return static_cast<tjs_uint>(krkrvita::read_all_bytes(
		static_cast<std::uint32_t>(read_size),
		[this, bytes](std::uint32_t offset, std::uint32_t remaining) {
			return sceIoRead(Handle, bytes + offset, remaining);
		}));
]=])
    string(REPLACE "${yuri_vita_single_read}"
        "${yuri_vita_complete_read}"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")

    set(yuri_vita_complete_write [=[
	const auto *bytes = static_cast<const unsigned char *>(buffer);
	return static_cast<tjs_uint>(krkrvita::write_all_bytes(
		static_cast<std::uint32_t>(write_size),
		[this, bytes](std::uint32_t offset, std::uint32_t remaining) {
			return sceIoWrite(Handle, bytes + offset, remaining);
		}));
]=])
    string(REPLACE "${yuri_vita_single_write}"
        "${yuri_vita_complete_write}"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    foreach(yuri_vita_direct_stream_contract
        "krkrvita/read_all.hpp"
        "krkrvita/vita_storage_path.hpp"
        "krkrvita/write_all.hpp"
        "TVPThrowExceptionMessage(TVPCannotOpenStorage, origname)"
        "krkrvita::read_all_bytes"
        "krkrvita::write_all_bytes"
        "MemBuffer = nullptr")
        string(FIND "${yuri_storage_impl_patched}"
            "${yuri_vita_direct_stream_contract}"
            yuri_vita_direct_stream_offset)
        if(yuri_vita_direct_stream_offset LESS 0)
            message(FATAL_ERROR
                "Yuri Vita direct file-stream patch no longer applies: ${yuri_vita_direct_stream_contract}")
        endif()
    endforeach()
    foreach(yuri_vita_buffered_stream_forbidden
        "MemBuffer = new tTVPMemoryStream()"
        "free(this)"
        "File Writing Error")
        string(FIND "${yuri_storage_impl_patched}"
            "${yuri_vita_buffered_stream_forbidden}"
            yuri_vita_buffered_stream_offset)
        if(NOT yuri_vita_buffered_stream_offset LESS 0)
            message(FATAL_ERROR
                "Yuri Vita buffered file-stream path remains: ${yuri_vita_buffered_stream_forbidden}")
        endif()
    endforeach()
    set(yuri_vita_native_path_marker
        "\tif(namelen == 0) return;\n#ifdef WIN32")
    set(yuri_vita_native_path_code
        "\tif(namelen == 0) return;\n#ifdef __vita__\n\tconst std::u16string vita_storage_name = krkrvita::vita_native_to_storage_path(\n\t\tstd::u16string_view(name.c_str(), name.GetLen()));\n\tif (vita_storage_name != std::u16string(name.c_str(), name.GetLen())) {\n\t\tname = ttstr(vita_storage_name);\n\t\treturn;\n\t}\n#endif\n#ifdef WIN32")
    string(REPLACE "${yuri_vita_native_path_marker}"
        "${yuri_vita_native_path_code}"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    set(yuri_vita_local_path_marker
        "    if(!TJS_strncmp(ptr, TJS_W(\"./\"), 2)) {\n        ptr += 2;  // skip \"./\"\n        newname.Clear();\n    }")
    set(yuri_vita_local_path_code
        "    if(!TJS_strncmp(ptr, TJS_W(\"./\"), 2)) {\n        ptr += 2;  // skip \"./\"\n        newname.Clear();\n    }\n#ifdef __vita__\n    const std::u16string vita_native_name = krkrvita::vita_storage_to_native_path(\n        std::u16string_view(name.c_str(), name.GetLen()));\n    ptr = vita_native_name.c_str();\n    const std::size_t vita_device_prefix =\n        krkrvita::vita_device_prefix_length(vita_native_name);\n    if (vita_device_prefix) {\n        newname = ttstr(vita_native_name.data(), vita_device_prefix);\n        ptr += vita_device_prefix;\n        while (*ptr == TJS_W('/')) ++ptr;\n    }\n#endif")
    string(REPLACE "${yuri_vita_local_path_marker}"
        "${yuri_vita_local_path_code}"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    string(REPLACE
        "\t\tnewname += \"/\";\n        if ((dirp = opendir( tTJSNarrowStringHolder(newname.c_str()) ))) {"
        "#ifdef __vita__\n\t\tif (newname.GetLastChar() != TJS_W(':')) newname += TJS_W(\"/\");\n#else\n\t\tnewname += \"/\";\n#endif\n        if ((dirp = opendir( tTJSNarrowStringHolder(newname.c_str()) ))) {"
        yuri_storage_impl_patched "${yuri_storage_impl_patched}")
    if(yuri_storage_impl_patched STREQUAL yuri_storage_impl_text)
        message(FATAL_ERROR "Yuri Vita SceIofilemgr storage patch no longer applies")
    endif()

    # UtilStreams.cpp contains both the engine's memory/partial stream classes
    # and Kirikiroid's archive-extraction utility used by its Cocos frontend.
    # Keep the engine portion verbatim and stop at that frontend-only utility.
    set(yuri_util_streams "${yuri_core}/base/UtilStreams.cpp")
    file(READ "${yuri_util_streams}" yuri_util_streams_text)
    set(yuri_unpack_marker "extern \"C\" {\n#include \"libarchive/archive.h\"")
    string(FIND "${yuri_util_streams_text}" "${yuri_unpack_marker}"
        yuri_unpack_offset)
    if(yuri_unpack_offset LESS 0)
        message(FATAL_ERROR "Yuri UtilStreams frontend boundary moved")
    endif()
    string(SUBSTRING "${yuri_util_streams_text}" 0 ${yuri_unpack_offset}
        yuri_engine_util_streams_text)
    set(yuri_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri")
    file(MAKE_DIRECTORY "${yuri_generated_dir}")

    # KAG timers and many layer/window notifications deliver TJS events with no
    # arguments. Upstream allocates both a zero-length variant array when the
    # event is created and a zero-length argv array when it is delivered. Keep
    # the ordinary nonempty ownership/copy semantics, but make the established
    # FuncCall(..., 0, nullptr, ...) representation allocation-free.
    set(yuri_event_intf "${yuri_core}/base/EventIntf.cpp")
    file(READ "${yuri_event_intf}" yuri_event_intf_text)
    set(yuri_event_storage_anchor
        "Args = new tTJSVariant[NumArgs];")
    set(yuri_event_storage_scan "${yuri_event_intf_text}")
    set(yuri_event_storage_anchor_count 0)
    string(LENGTH "${yuri_event_storage_anchor}"
        yuri_event_storage_anchor_length)
    set(yuri_event_storage_scanning TRUE)
    while(yuri_event_storage_scanning)
        string(FIND "${yuri_event_storage_scan}"
            "${yuri_event_storage_anchor}" yuri_event_storage_anchor_offset)
        if(yuri_event_storage_anchor_offset LESS 0)
            set(yuri_event_storage_scanning FALSE)
        else()
            math(EXPR yuri_event_storage_anchor_count
                "${yuri_event_storage_anchor_count} + 1")
            math(EXPR yuri_event_storage_remainder_offset
                "${yuri_event_storage_anchor_offset} + ${yuri_event_storage_anchor_length}")
            string(SUBSTRING "${yuri_event_storage_scan}"
                ${yuri_event_storage_remainder_offset} -1
                yuri_event_storage_scan)
        endif()
    endwhile()
    if(NOT yuri_event_storage_anchor_count EQUAL 2)
        message(FATAL_ERROR
            "Yuri event argument-storage anchors moved (expected two)")
    endif()
    string(REPLACE
        "#include \"SystemImpl.h\""
        "#include \"SystemImpl.h\"\n#include \"krkrvita/event_arguments.hpp\""
        yuri_event_intf_patched "${yuri_event_intf_text}")
    if(yuri_event_intf_patched STREQUAL yuri_event_intf_text)
        message(FATAL_ERROR
            "Yuri event argument helper include patch no longer applies")
    endif()
    string(REPLACE
        "Args = new tTJSVariant[NumArgs];"
        "Args = krkrvita::allocate_event_argument_storage<tTJSVariant>(NumArgs);"
        yuri_event_intf_patched "${yuri_event_intf_patched}")
    set(yuri_event_delivery_old [=[
		tTJSVariant **ArgsPtr = new tTJSVariant*[NumArgs];
		for(tjs_uint i=0; i<NumArgs; i++)
			ArgsPtr[i] = Args + i;
		try
		{
			Target->FuncCall(0, EventName.c_str(), EventName.GetHint(),
				NULL, NumArgs, ArgsPtr,
				Target);
		}
		catch(...)
		{
			delete [] ArgsPtr;
			throw;
		}
		delete [] ArgsPtr;
]=])
    set(yuri_event_delivery_fast [=[
		krkrvita::with_event_argument_pointers(
			Args, NumArgs, [this](tTJSVariant **args_ptr)
			{
				Target->FuncCall(0, EventName.c_str(), EventName.GetHint(),
					NULL, NumArgs, args_ptr,
					Target);
			});
]=])
    set(yuri_event_intf_before_delivery "${yuri_event_intf_patched}")
    string(REPLACE "${yuri_event_delivery_old}"
        "${yuri_event_delivery_fast}"
        yuri_event_intf_patched "${yuri_event_intf_patched}")
    if(yuri_event_intf_patched STREQUAL yuri_event_intf_before_delivery)
        message(FATAL_ERROR
            "Yuri zero-argument event delivery patch no longer applies")
    endif()
    foreach(yuri_event_contract
        "krkrvita/event_arguments.hpp"
        "krkrvita::allocate_event_argument_storage<tTJSVariant>(NumArgs)"
        "krkrvita::with_event_argument_pointers("
        "Args, NumArgs, [this](tTJSVariant **args_ptr)")
        string(FIND "${yuri_event_intf_patched}"
            "${yuri_event_contract}" yuri_event_contract_offset)
        if(yuri_event_contract_offset LESS 0)
            message(FATAL_ERROR
                "Yuri zero-argument event contract is incomplete: ${yuri_event_contract}")
        endif()
    endforeach()
    foreach(yuri_event_allocating_empty_path
        "new tTJSVariant[NumArgs]"
        "new tTJSVariant*[NumArgs]")
        string(FIND "${yuri_event_intf_patched}"
            "${yuri_event_allocating_empty_path}"
            yuri_event_allocating_empty_offset)
        if(NOT yuri_event_allocating_empty_offset LESS 0)
            message(FATAL_ERROR
                "Yuri generated event source retains zero-length allocation: ${yuri_event_allocating_empty_path}")
        endif()
    endforeach()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/EventIntf.cpp"
        CONTENT "${yuri_event_intf_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_base_sources "${yuri_event_intf}")
    list(APPEND yuri_base_sources "${yuri_generated_dir}/EventIntf.cpp")

    # KAG's HistoryLayer measures `currentLine += ch` through its layer-backed
    # Font object for every displayed character. Keep the existing one-string
    # bitmap cache, but remember whether its width came entirely from stable
    # metrics and the exact FreeType metric state that produced it. The
    # generated header precedes Yuri's source include directories everywhere,
    # so every archive agrees on the augmented bitmap layout.
    set(yuri_layer_bitmap_header
        "${yuri_core}/visual/win32/LayerBitmapImpl.h")
    file(READ "${yuri_layer_bitmap_header}" yuri_layer_bitmap_header_text)
    string(REPLACE
        "\tttstr CachedText;"
        "\tttstr CachedText;\n\ttjs_uint64 CachedTextMetricState = 0;\n\tbool CachedTextPrefixReusable = false;"
        yuri_layer_bitmap_header_patched
        "${yuri_layer_bitmap_header_text}")
    if(yuri_layer_bitmap_header_patched STREQUAL yuri_layer_bitmap_header_text)
        message(FATAL_ERROR
            "Yuri text-prefix bitmap-layout patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/LayerBitmapImpl.h"
        CONTENT "${yuri_layer_bitmap_header_patched}"
        @ONLY NEWLINE_STYLE UNIX)

    # The generic rasterizer defaults to opting out of prefix reuse. The fixed
    # MS Gothic FreeType implementation below opts in only after a successful
    # glyph load and exposes a lossless options/height state key. This preserves
    # retry semantics and prevents mixing widths across rasterizer state.
    set(yuri_font_rasterizer_header
        "${yuri_core}/visual/FontRasterizer.h")
    file(READ "${yuri_font_rasterizer_header}"
        yuri_font_rasterizer_header_text)
    string(REPLACE
        "\tvirtual void GetTextExtent(tjs_char ch, tjs_int &w, tjs_int &h) = 0;"
        "\tvirtual void GetTextExtent(tjs_char ch, tjs_int &w, tjs_int &h) = 0;\n\tvirtual bool GetTextExtentCacheState(tjs_uint64 &state) const { (void)state; return false; }\n\tvirtual bool GetTextExtentForPrefixCache(tjs_char ch, tjs_int &w, tjs_int &h) { GetTextExtent(ch, w, h); return false; }"
        yuri_font_rasterizer_header_patched
        "${yuri_font_rasterizer_header_text}")
    if(yuri_font_rasterizer_header_patched STREQUAL
       yuri_font_rasterizer_header_text)
        message(FATAL_ERROR
            "Yuri text-prefix rasterizer interface patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/FontRasterizer.h"
        CONTENT "${yuri_font_rasterizer_header_patched}"
        @ONLY NEWLINE_STYLE UNIX)

    set(yuri_freetype_rasterizer_header
        "${yuri_core}/visual/FreeTypeFontRasterizer.h")
    file(READ "${yuri_freetype_rasterizer_header}"
        yuri_freetype_rasterizer_header_text)
    string(REPLACE
        "\tvoid GetTextExtent(tjs_char ch, tjs_int &w, tjs_int &h);"
        "\tvoid GetTextExtent(tjs_char ch, tjs_int &w, tjs_int &h);\n\tbool GetTextExtentCacheState(tjs_uint64 &state) const override;\n\tbool GetTextExtentForPrefixCache(tjs_char ch, tjs_int &w, tjs_int &h) override;"
        yuri_freetype_rasterizer_header_patched
        "${yuri_freetype_rasterizer_header_text}")
    if(yuri_freetype_rasterizer_header_patched STREQUAL
       yuri_freetype_rasterizer_header_text)
        message(FATAL_ERROR
            "Yuri text-prefix FreeType interface patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/FreeTypeFontRasterizer.h"
        CONTENT "${yuri_freetype_rasterizer_header_patched}"
        @ONLY NEWLINE_STYLE UNIX)

    # Yuri accepts each row task as const std::function<void(int)>&. At an
    # ordinary call site the lambda-to-std::function conversion happens before
    # TVPExecThreadTask can see that the Vita default selected one task; large
    # captures therefore allocate even though no worker is used. Shadow the
    # public header with a templated dispatch boundary. It runs the concrete
    # closure directly for zero/one task and performs Yuri's original owning
    # conversion only inside the explicit multi-task branch.
    set(yuri_thread_intf_header "${yuri_core}/utils/ThreadIntf.h")
    file(READ "${yuri_thread_intf_header}" yuri_thread_intf_text)
    string(REPLACE
        "#include <functional>"
        "#include <functional>\n#include <utility>\n#include \"krkrvita/render_task_dispatch.hpp\""
        yuri_thread_intf_patched "${yuri_thread_intf_text}")
    set(yuri_thread_dispatch_declaration [=[
TJS_EXP_FUNC_DEF(void, TVPExecThreadTask, (int numThreads, TVP_THREAD_TASK_FUNC func));
]=])
    set(yuri_thread_dispatch_vita [=[
TJS_EXP_FUNC_DEF(void, TVPExecThreadTask, (int numThreads, TVP_THREAD_TASK_FUNC func));

template <typename TTask>
inline void TVPExecThreadTaskVita(int numThreads, TTask&& func)
{
	krkrvita::dispatch_render_tasks(
		numThreads, std::forward<TTask>(func),
		[](int parallelThreads, auto&& parallelFunc) {
			TVPExecThreadTask(parallelThreads,
				std::function<void(int)>(
					std::forward<decltype(parallelFunc)>(parallelFunc)));
		});
}
]=])
    string(REPLACE "${yuri_thread_dispatch_declaration}"
        "${yuri_thread_dispatch_vita}"
        yuri_thread_intf_patched_2 "${yuri_thread_intf_patched}")
    if(yuri_thread_intf_patched_2 STREQUAL yuri_thread_intf_text)
        message(FATAL_ERROR
            "Yuri zero-allocation serial dispatch header patch no longer applies")
    endif()
    foreach(yuri_thread_dispatch_contract
        "krkrvita/render_task_dispatch.hpp"
        "TVPExecThreadTaskVita"
        "krkrvita::dispatch_render_tasks"
        "std::function<void(int)>")
        string(FIND "${yuri_thread_intf_patched_2}"
            "${yuri_thread_dispatch_contract}"
            yuri_thread_dispatch_contract_offset)
        if(yuri_thread_dispatch_contract_offset LESS 0)
            message(FATAL_ERROR
                "Yuri serial dispatch contract no longer applies: ${yuri_thread_dispatch_contract}")
        endif()
    endforeach()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/ThreadIntf.h"
        CONTENT "${yuri_thread_intf_patched_2}"
        @ONLY NEWLINE_STYLE UNIX)

    # The Vita presenter keeps Yuri's persistent software draw buffer alive
    # between completion and presentation.  That lifetime hold is read-only
    # and runs on the same main thread as the compositor; counting it as an
    # ordinary sharing reference makes IsIndependent() false and forces a
    # complete 1280x960 copy before every compositor update.  Give the pinned
    # Yuri texture ABI an explicit presentation-reference count.  It remains
    # part of the lifetime RefCount, but is excluded from mutable-owner COW
    # decisions.  Generate the header so every Yuri archive sees one layout.
    set(yuri_render_manager_header
        "${yuri_core}/visual/RenderManager.h")
    file(READ "${yuri_render_manager_header}"
        yuri_render_manager_header_text)
    string(REPLACE
        "#include \"ComplexRect.h\""
        "#include \"ComplexRect.h\"\n#include \"krkrvita/presentation_reference.hpp\"\n#include <cstdlib>"
        yuri_render_manager_header_patched
        "${yuri_render_manager_header_text}")
    set(yuri_texture_reference_old [=[
protected:
	int RefCount;
	tjs_int Width; // actual width
	tjs_int Height; // actual height
	//int Flags, TexWidth, TexHeight, ActualWidth, ActualHeight;
	iTVPTexture2D(tjs_int w, tjs_int h) : Width(w), Height(h), RefCount(1) {}
public:
	virtual ~iTVPTexture2D() {};
	void AddRef() { ++RefCount; }
	virtual void Release();
]=])
    set(yuri_texture_reference_vita [=[
protected:
	int RefCount;
	int PresentationRefCount;
	tjs_int Width; // actual width
	tjs_int Height; // actual height
	//int Flags, TexWidth, TexHeight, ActualWidth, ActualHeight;
	iTVPTexture2D(tjs_int w, tjs_int h)
		: RefCount(1), PresentationRefCount(0), Width(w), Height(h) {}
public:
	virtual ~iTVPTexture2D() {};
	void AddRef() { ++RefCount; }
	void AddPresentationRef() { ++RefCount; ++PresentationRefCount; }
	void ReleasePresentationRef() {
		if(PresentationRefCount <= 0) std::abort();
		--PresentationRefCount;
		Release();
	}
	virtual void Release();
]=])
    string(REPLACE "${yuri_texture_reference_old}"
        "${yuri_texture_reference_vita}"
        yuri_render_manager_header_patched_2
        "${yuri_render_manager_header_patched}")
    string(REPLACE
        "\tvirtual tjs_int GetPitch() const { return 0x100000; }\n\tbool IsIndependent() const { return RefCount == 1; }"
        "\tvirtual tjs_int GetPitch() const { return 0x100000; }\n\tbool IsIndependent() const {\n\t\treturn krkrvita::yuri_texture_has_single_mutable_owner(\n\t\t\tRefCount, PresentationRefCount);\n\t}"
        yuri_render_manager_header_patched_3
        "${yuri_render_manager_header_patched_2}")
    if(yuri_render_manager_header_patched_3 STREQUAL
       yuri_render_manager_header_text)
        message(FATAL_ERROR
            "Yuri Vita presentation-reference header patch no longer applies")
    endif()
    foreach(yuri_presentation_reference_contract
        "int PresentationRefCount"
        "AddPresentationRef()"
        "ReleasePresentationRef()"
        "yuri_texture_has_single_mutable_owner")
        string(FIND "${yuri_render_manager_header_patched_3}"
            "${yuri_presentation_reference_contract}"
            yuri_presentation_reference_offset)
        if(yuri_presentation_reference_offset LESS 0)
            message(FATAL_ERROR
                "Yuri presentation-reference contract no longer applies: ${yuri_presentation_reference_contract}")
        endif()
    endforeach()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/RenderManager.h"
        CONTENT "${yuri_render_manager_header_patched_3}"
        @ONLY NEWLINE_STYLE UNIX)

    # Keep the glyph optimization state inside each FreeType face.  The key
    # includes every input that controls FT_Load_Glyph, and a rendered slot is
    # never retained.  This generated header precedes Yuri's source include
    # directory for every archive so there is one object layout.
    set(yuri_freetype_header "${yuri_core}/visual/FreeType.h")
    file(READ "${yuri_freetype_header}" yuri_freetype_header_text)
    string(REPLACE
        "#include \"FreeTypeFace.h\""
        "#include \"FreeTypeFace.h\"\n#include \"krkrvita/ms_gothic_fast_path.hpp\""
        yuri_freetype_header_patched "${yuri_freetype_header_text}")
    string(REPLACE
        "\ttjs_int Height;\t\t//!<"
        "\tkrkrvita::YuriGlyphMetricsCache<tGlyphMetrics, 256> GlyphMetricsCache;\n\tkrkrvita::YuriPreparedGlyphKey PreparedGlyphKey{};\n\tbool PreparedGlyphValid = false;\n\tbool UseVitaMsGothicFastPath = false;\n\tbool SizeInitialized = false;\n\ttjs_int Height;\t\t//!<"
        yuri_freetype_header_patched_2 "${yuri_freetype_header_patched}")
    set(yuri_freetype_option_methods [=[
	void SetOption( tjs_uint32 opt ) {
		Options |= opt;
	}
	void ClearOption( tjs_uint32 opt ) {
		Options &= ~opt;
	}
]=])
    set(yuri_freetype_option_methods_vita [=[
	void SetOption( tjs_uint32 opt ) {
		const tjs_uint32 changed = Options | opt;
		if(changed != Options) PreparedGlyphValid = false;
		Options = changed;
	}
	void ClearOption( tjs_uint32 opt ) {
		const tjs_uint32 changed = Options & ~opt;
		if(changed != Options) PreparedGlyphValid = false;
		Options = changed;
	}
]=])
    string(REPLACE "${yuri_freetype_option_methods}"
        "${yuri_freetype_option_methods_vita}"
        yuri_freetype_header_patched_3 "${yuri_freetype_header_patched_2}")
    string(REPLACE
        "private:\n\tbool LoadGlyphSlotFromCharcode(tjs_char code);"
        "private:\n\tvoid InvalidatePreparedGlyph() { PreparedGlyphValid = false; }\n\tbool LoadGlyphSlotFromCharcode(tjs_char code);"
        yuri_freetype_header_patched_4 "${yuri_freetype_header_patched_3}")
    string(REPLACE
        "\ttjs_int GetHeight() { return Height; }\n\tvoid SetHeight(int height);"
        "\ttjs_int GetHeight() { return Height; }\n\tbool GetVitaMsGothicMetricState(tjs_uint64 &state) const {\n\t\tif(!UseVitaMsGothicFastPath || !SizeInitialized) return false;\n\t\tstate = (static_cast<tjs_uint64>(Options) << 32) |\n\t\t\tstatic_cast<tjs_uint32>(Height);\n\t\treturn true;\n\t}\n\tvoid SetHeight(int height);"
        yuri_freetype_header_patched_5 "${yuri_freetype_header_patched_4}")
    foreach(yuri_freetype_header_contract
        "ms_gothic_fast_path.hpp"
        "GlyphMetricsCache"
        "PreparedGlyphKey"
        "UseVitaMsGothicFastPath"
        "SizeInitialized"
        "GetVitaMsGothicMetricState"
        "changed != Options"
        "InvalidatePreparedGlyph")
        string(FIND "${yuri_freetype_header_patched_5}"
            "${yuri_freetype_header_contract}" yuri_freetype_header_offset)
        if(yuri_freetype_header_offset LESS 0)
            message(FATAL_ERROR
                "Yuri MS Gothic face-cache header patch no longer applies: ${yuri_freetype_header_contract}")
        endif()
    endforeach()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/FreeType.h"
        CONTENT "${yuri_freetype_header_patched_5}"
        @ONLY NEWLINE_STYLE UNIX)
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/StorageImpl.cpp"
        CONTENT "${yuri_storage_impl_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_base_sources "${yuri_storage_impl}")
    list(APPEND yuri_base_sources "${yuri_generated_dir}/StorageImpl.cpp")
    list(APPEND yuri_base_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vita/yuri_7z_libarchive.cpp")

    # The stock XP3 reader assumes that every underlying Read() fills the
    # complete request. SceIofilemgr may legally return a positive short
    # count even for a regular file. The local Vita stream joins those pieces,
    # but archive streams can also be wrapped by other binary-stream layers;
    # harden the two XP3 payload reads at their boundary as well. This keeps a
    # partially-filled compressed segment or SQLite page from being passed to
    # zlib/SQLite as if it were complete.
    set(yuri_xp3_archive "${yuri_core}/base/XP3Archive.cpp")
    file(READ "${yuri_xp3_archive}" yuri_xp3_archive_text)
    string(REPLACE
        "#include \"XP3Archive.h\""
        "#include \"XP3Archive.h\"\n#include \"krkrvita/read_all.hpp\""
        yuri_xp3_archive_patched "${yuri_xp3_archive_text}")
    if(yuri_xp3_archive_patched STREQUAL yuri_xp3_archive_text)
        message(FATAL_ERROR "Yuri XP3 read-all include patch no longer applies")
    endif()
    string(REPLACE
        "\t\t\tinstream->Read(indata, insize);"
        "\t\t\tif (krkrvita::read_all_stream(*instream, indata, static_cast<std::uint32_t>(insize)) != static_cast<std::uint32_t>(insize))\n\t\t\t\tTVPThrowExceptionMessage(TVPUncompressionFailed);"
        yuri_xp3_archive_patched "${yuri_xp3_archive_patched}")
    string(REPLACE
        "\t\t\tStream->ReadBuffer((tjs_uint8*)buffer + write_size, one_size);"
        "\t\t\tif (krkrvita::read_all_stream(*Stream, (tjs_uint8*)buffer + write_size, one_size) != one_size)\n\t\t\t\tTVPThrowExceptionMessage(TJS_W(\"XP3 read error\"));"
        yuri_xp3_archive_patched "${yuri_xp3_archive_patched}")
    foreach(yuri_xp3_read_contract
        "krkrvita/read_all.hpp"
        "krkrvita::read_all_stream(*instream"
        "krkrvita::read_all_stream(*Stream")
        string(FIND "${yuri_xp3_archive_patched}" "${yuri_xp3_read_contract}"
            yuri_xp3_read_contract_offset)
        if(yuri_xp3_read_contract_offset LESS 0)
            message(FATAL_ERROR
                "Yuri XP3 read-all patch no longer applies: ${yuri_xp3_read_contract}")
        endif()
    endforeach()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/XP3Archive.cpp"
        CONTENT "${yuri_xp3_archive_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_base_sources "${yuri_xp3_archive}")
    list(APPEND yuri_base_sources "${yuri_generated_dir}/XP3Archive.cpp")

    # Keep Yuri's patch execution phase, but allow the retail resolver to
    # point at a patch bundle outside the game directory. After the game's
    # startup and its own optional AfterStartup.tjs have completed, execute a
    # second fixed Vita-only adaptation from app0. This keeps platform policy
    # out of the proprietary game archives and the pinned compatibility patch.
    set(yuri_script_manager "${yuri_core}/base/ScriptMgnIntf.cpp")
    file(READ "${yuri_script_manager}" yuri_script_manager_text)
    string(REPLACE
        "        ttstr patch = TVPGetAppPath() + \"patch.tjs\";"
        "        ttstr patch;\n        tTJSVariant patch_option;\n        if (TVPGetCommandLine(TJS_W(\"-krkrpatch\"), &patch_option))\n            patch = ttstr(patch_option);\n        else\n            patch = TVPGetAppPath() + TJS_W(\"patch.tjs\");"
        yuri_script_manager_patched "${yuri_script_manager_text}")
    if(yuri_script_manager_patched STREQUAL yuri_script_manager_text)
        message(FATAL_ERROR "Yuri external retail patch path patch no longer applies")
    endif()
    set(yuri_script_manager_before_vita_hook
        "${yuri_script_manager_patched}")
    string(REPLACE
        "#include \"Application.h\""
        "#include \"Application.h\"\n#include \"krkrvita/kag_system_variables.hpp\"\n#include \"krkrvita/retail_bootstrap.hpp\""
        yuri_script_manager_patched
        "${yuri_script_manager_patched}")
    if(yuri_script_manager_patched STREQUAL
       yuri_script_manager_before_vita_hook)
        message(FATAL_ERROR "Yuri Vita post-startup include patch no longer applies")
    endif()
    set(yuri_after_startup_old [=[
			try {
				ttstr patch = TVPGetAppPath() + "AfterStartup.tjs";
				if (TVPIsExistentStorageNoSearch(patch))
					TVPExecuteStorage(patch);
			}
			catch (...) {}
]=])
    set(yuri_after_startup_vita [=[
			try {
				ttstr patch = TVPGetAppPath() + "AfterStartup.tjs";
				if (TVPIsExistentStorageNoSearch(patch))
					TVPExecuteStorage(patch);
			}
			catch (...) {}
			try {
				const ttstr vita_patch =
					TJS_W("app0:krkrvita/retail-after-startup.tjs");
				if (TVPIsExistentStorageNoSearch(vita_patch)) {
					TVPExecuteStorage(vita_patch);
					krkrvita_boot_trace("retail-vita-afterstartup-executed");
				}
			}
			catch (...) {}
]=])
    set(yuri_script_manager_before_vita_hook
        "${yuri_script_manager_patched}")
    string(REPLACE "${yuri_after_startup_old}"
        "${yuri_after_startup_vita}"
        yuri_script_manager_patched
        "${yuri_script_manager_patched}")
    if(yuri_script_manager_patched STREQUAL
       yuri_script_manager_before_vita_hook)
        message(FATAL_ERROR "Yuri Vita post-startup script hook no longer applies")
    endif()

    # KAG's datasc.ksd and datasu.ksd are reconstructable preference/state
    # dictionaries. Older Vita builds could leave either file truncated; KAG
    # then aborts startup while evaluating the persisted expression. Recover
    # only a direct child of the active data path with the exact sc/su suffix,
    # preserve it under a unique quarantine name, and return the empty
    # dictionary used on a first launch. Other scripts and saves still throw.
    set(yuri_kag_state_recovery [=[
static bool TVPRecoverKagSystemVariables(
	const ttstr &place, const ttstr &shortname, const ttstr &buffer,
	iTJSDispatch2 *context, tTJSVariant *result)
{
	static_assert(sizeof(tjs_char) == sizeof(char16_t),
		"Vita KAG state recovery requires UTF-16 storage names");
	const std::u16string_view normalized_place(
		reinterpret_cast<const char16_t *>(place.c_str()), place.GetLen());
	const std::u16string_view normalized_data_path(
		reinterpret_cast<const char16_t *>(TVPDataPath.c_str()),
		TVPDataPath.GetLen());
	if(!krkrvita::is_kag_system_variable_storage(
			normalized_place, normalized_data_path))
		return false;

	ttstr local_name = TVPGetLocallyAccessibleName(place);
	if(local_name.IsEmpty()) return false;
	tTJSNarrowStringHolder native_holder(local_name.c_str());
	if(!native_holder.Buf || !*native_holder.Buf) return false;
	const std::string native_path(native_holder.Buf);
	const std::string backup =
		krkrvita::quarantine_corrupt_system_variable(
			native_path,
			[](std::string_view candidate) {
				return TVPCheckStartupPath(std::string(candidate));
			},
			[](std::string_view from, std::string_view to) {
				return TVPRenameFile(std::string(from), std::string(to));
			});
	if(backup.empty()) return false;

	TVPAddImportantLog(
		ttstr(TJS_W("(info) Quarantined unparseable KAG system variables: ")) +
		place + TJS_W(" (UTF-16 units: ") +
		ttstr(static_cast<tjs_int64>(buffer.GetLen())) + TJS_W(")"));
	krkrvita_boot_trace("yuri-kag-system-variables-recovered");
	TVPScriptEngine->EvalExpression(
		TJS_W("%[]"), result, context, &shortname);
	return true;
}

]=])
    set(yuri_execute_storage_anchor [=[
//---------------------------------------------------------------------------
void TVPExecuteStorage(const ttstr &name, tTJSVariant *result, bool isexpression,
]=])
    set(yuri_execute_storage_replacement
        "//---------------------------------------------------------------------------\n${yuri_kag_state_recovery}void TVPExecuteStorage(const ttstr &name, tTJSVariant *result, bool isexpression,\n")
    set(yuri_script_manager_before_kag_recovery
        "${yuri_script_manager_patched}")
    string(REPLACE "${yuri_execute_storage_anchor}"
        "${yuri_execute_storage_replacement}"
        yuri_script_manager_patched "${yuri_script_manager_patched}")
    if(yuri_script_manager_patched STREQUAL
       yuri_script_manager_before_kag_recovery)
        message(FATAL_ERROR
            "Yuri KAG system-variable recovery helper patch no longer applies")
    endif()

    set(yuri_eval_storage_old [=[
		else
			TVPScriptEngine->EvalExpression(buffer, result, context,
				&shortname);
]=])
    set(yuri_eval_storage_new [=[
		else
		{
			try {
				TVPScriptEngine->EvalExpression(buffer, result, context,
					&shortname);
			} catch(const eTJSScriptError &) {
				if(!TVPRecoverKagSystemVariables(
						place, shortname, buffer, context, result))
					throw;
			}
		}
]=])
    set(yuri_script_manager_before_kag_eval
        "${yuri_script_manager_patched}")
    string(REPLACE "${yuri_eval_storage_old}" "${yuri_eval_storage_new}"
        yuri_script_manager_patched "${yuri_script_manager_patched}")
    if(yuri_script_manager_patched STREQUAL
       yuri_script_manager_before_kag_eval)
        message(FATAL_ERROR
            "Yuri KAG system-variable eval recovery patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/ScriptMgnIntf.cpp"
        CONTENT "${yuri_script_manager_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_base_sources "${yuri_script_manager}")
    list(APPEND yuri_base_sources
        "${yuri_generated_dir}/ScriptMgnIntf.cpp")

    # This destructor deliberately reports final zlib/write failures through
    # iTJSTextWriteStream::Destruct(). C++11 otherwise makes the destructor
    # implicitly noexcept and turns the intended exception into terminate().
    # The retail bookmark path uses compressed Dictionary.saveStruct after its
    # thumbnail is written. Yuri's 1 MiB zlib output window is an unnecessary
    # contiguous newlib allocation at that peak: deflate output is independent
    # of avail_out chunking, so use the conventional 64 KiB streaming window.
    set(yuri_text_stream "${yuri_core}/base/TextStream.cpp")
    file(READ "${yuri_text_stream}" yuri_text_stream_text)
    string(REPLACE
        "#include \"CharacterSet.h\""
        "#include \"CharacterSet.h\"\n#include <atomic>\n#include \"krkrvita/buffered_units.hpp\"\n#include \"krkrvita/retail_bootstrap.hpp\""
        yuri_text_stream_text "${yuri_text_stream_text}")
    string(REPLACE
        "\t~tTVPTextWriteStream()"
        "\t~tTVPTextWriteStream() noexcept(false)"
        yuri_text_stream_patched "${yuri_text_stream_text}")
    if(yuri_text_stream_patched STREQUAL yuri_text_stream_text)
        message(FATAL_ERROR "Yuri text writer exception patch no longer applies")
    endif()
    set(yuri_text_stream_before_buffer "${yuri_text_stream_patched}")
    string(REPLACE
        "static const tjs_uint COMPRESSION_BUFFER_SIZE = 1024 * 1024;"
        "static const tjs_uint COMPRESSION_BUFFER_SIZE = 64 * 1024;"
        yuri_text_stream_patched "${yuri_text_stream_patched}")
    if(yuri_text_stream_patched STREQUAL yuri_text_stream_before_buffer)
        message(FATAL_ERROR "Yuri Vita save compression-buffer patch no longer applies")
    endif()
    set(yuri_text_stream_compression_setup_old [=[
		if(CryptMode == 2)
		{
			// allocate and initialize zlib straem
			ZStream = new z_stream_s();
			ZStream->zalloc = Z_NULL;
			ZStream->zfree = Z_NULL;
			ZStream->opaque = Z_NULL;
			if (deflateInit(ZStream, CompressionLevel) != Z_OK) {
				CompressionFailed = true;
				TVPThrowExceptionMessage(TVPCompressionFailed);
			}

			CompressionBuffer = new tjs_nchar[COMPRESSION_BUFFER_SIZE];

			ZStream->next_in = NULL;
			ZStream->avail_in = 0;
			ZStream->next_out = reinterpret_cast<Bytef*>( CompressionBuffer );
			ZStream->avail_out = COMPRESSION_BUFFER_SIZE;

			// Compression Size (write dummy)
			CompressionSizePosition = static_cast<tjs_uint>(Stream->GetPosition());
			WriteI64LE((tjs_uint64)0);
			WriteI64LE((tjs_uint64)0);
		}
]=])
    set(yuri_text_stream_compression_setup_vita [=[
		if(CryptMode == 2)
		{
			bool zstream_initialized = false;
			try {
				// allocate and initialize zlib straem
				ZStream = new z_stream_s();
				ZStream->zalloc = Z_NULL;
				ZStream->zfree = Z_NULL;
				ZStream->opaque = Z_NULL;
				if (deflateInit(ZStream, CompressionLevel) != Z_OK) {
					CompressionFailed = true;
					TVPThrowExceptionMessage(TVPCompressionFailed);
				}
				zstream_initialized = true;

				CompressionBuffer = new tjs_nchar[COMPRESSION_BUFFER_SIZE];

				ZStream->next_in = NULL;
				ZStream->avail_in = 0;
				ZStream->next_out = reinterpret_cast<Bytef*>( CompressionBuffer );
				ZStream->avail_out = COMPRESSION_BUFFER_SIZE;

				// Compression Size (write dummy)
				CompressionSizePosition = static_cast<tjs_uint>(Stream->GetPosition());
				WriteI64LE((tjs_uint64)0);
				WriteI64LE((tjs_uint64)0);
			} catch(...) {
				if(zstream_initialized) deflateEnd(ZStream);
				delete ZStream;
				ZStream = NULL;
				delete[] CompressionBuffer;
				CompressionBuffer = NULL;
				delete Stream;
				Stream = NULL;
				throw;
			}
		}
]=])
    set(yuri_text_stream_before_compression_setup
        "${yuri_text_stream_patched}")
    string(REPLACE "${yuri_text_stream_compression_setup_old}"
        "${yuri_text_stream_compression_setup_vita}"
        yuri_text_stream_patched "${yuri_text_stream_patched}")
    if(yuri_text_stream_patched STREQUAL
       yuri_text_stream_before_compression_setup)
        message(FATAL_ERROR "Yuri Vita save constructor-cleanup patch no longer applies")
    endif()

    # TJS's built-in Dictionary.saveStruct writes each punctuation, key and
    # value fragment separately through iTJSTextWriteStream. Android's local
    # stream absorbs that in memory, but Vita's seekable local stream maps each
    # fragment to sceIoWrite. Keep the direct local-file lifetime contract and
    # aggregate only uncompressed/simple-crypt text payloads in a fixed 8 KiB
    # buffer. Compressed mode already has its independent zlib output window.
    set(yuri_text_stream_direct_fields_old [=[
	bool CompressionFailed;

public:
]=])
    set(yuri_text_stream_direct_fields_new [=[
	bool CompressionFailed;

	static const size_t DIRECT_OUTPUT_BUFFER_SIZE = 8 * 1024;
	krkrvita::BufferedUnits<tjs_uint8, DIRECT_OUTPUT_BUFFER_SIZE> DirectOutputBuffer;
	tjs_uint64 DirectOutputBytes;
	tjs_uint DirectOutputWrites;

	void FlushDirectOutput()
	{
		auto sink = [this](const tjs_uint8 *data, size_t count) {
			Stream->WriteBuffer(data, static_cast<tjs_uint>(count));
			DirectOutputWrites++;
		};
		DirectOutputBuffer.flush_to(sink);
	}

public:
]=])
    set(yuri_text_stream_before_direct_fields "${yuri_text_stream_patched}")
    string(REPLACE "${yuri_text_stream_direct_fields_old}"
        "${yuri_text_stream_direct_fields_new}"
        yuri_text_stream_patched "${yuri_text_stream_patched}")
    if(yuri_text_stream_patched STREQUAL yuri_text_stream_before_direct_fields)
        message(FATAL_ERROR "Yuri Vita text-output buffer fields patch no longer applies")
    endif()

    string(REPLACE
        "\t\tCompressionFailed = false;"
        "\t\tCompressionFailed = false;\n\t\tDirectOutputBytes = 0;\n\t\tDirectOutputWrites = 0;"
        yuri_text_stream_patched "${yuri_text_stream_patched}")

    set(yuri_text_stream_direct_destructor_old [=[
		if(Stream) delete Stream;
	}

	void WriteI64LE(tjs_uint64 v)
]=])
    set(yuri_text_stream_direct_destructor_new [=[
		if(Stream && CryptMode != 2)
		{
			try {
				FlushDirectOutput();
			} catch(...) {
				delete Stream;
				Stream = NULL;
				throw;
			}
			if(DirectOutputBytes >= 4096)
			{
				TVPAddImportantLog(
					ttstr(TJS_W("(info) Vita buffered text write: ")) +
					ttstr(static_cast<tjs_int64>(DirectOutputBytes)) +
					TJS_W(" bytes in ") +
					ttstr(static_cast<tjs_int64>(DirectOutputWrites)) +
					TJS_W(" writes"));
			}
		}
		if(Stream) delete Stream;
	}

	void WriteI64LE(tjs_uint64 v)
]=])
    set(yuri_text_stream_before_direct_destructor "${yuri_text_stream_patched}")
    string(REPLACE "${yuri_text_stream_direct_destructor_old}"
        "${yuri_text_stream_direct_destructor_new}"
        yuri_text_stream_patched "${yuri_text_stream_patched}")
    if(yuri_text_stream_patched STREQUAL
       yuri_text_stream_before_direct_destructor)
        message(FATAL_ERROR "Yuri Vita text-output flush patch no longer applies")
    endif()

    set(yuri_text_stream_direct_write_old [=[
		else
		{
			Stream->WriteBuffer(ptr, (tjs_uint)size); // write directly
		}
]=])
    set(yuri_text_stream_direct_write_new [=[
		else
		{
			static std::atomic<bool> ready_reported(false);
			if(!ready_reported.exchange(true, std::memory_order_relaxed))
				krkrvita_boot_trace("yuri-text-write-buffered-ready");
			DirectOutputBytes += static_cast<tjs_uint64>(size);
			auto sink = [this](const tjs_uint8 *data, size_t count) {
				Stream->WriteBuffer(data, static_cast<tjs_uint>(count));
				DirectOutputWrites++;
			};
			DirectOutputBuffer.append(
				static_cast<const tjs_uint8 *>(ptr), size, sink);
		}
]=])
    set(yuri_text_stream_before_direct_write "${yuri_text_stream_patched}")
    string(REPLACE "${yuri_text_stream_direct_write_old}"
        "${yuri_text_stream_direct_write_new}"
        yuri_text_stream_patched "${yuri_text_stream_patched}")
    if(yuri_text_stream_patched STREQUAL yuri_text_stream_before_direct_write)
        message(FATAL_ERROR "Yuri Vita text-output aggregation patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/TextStream.cpp"
        CONTENT "${yuri_text_stream_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_base_sources "${yuri_text_stream}")
    list(APPEND yuri_base_sources "${yuri_generated_dir}/TextStream.cpp")
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/UtilStreams.cpp"
        CONTENT "${yuri_engine_util_streams_text}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_base_sources "${yuri_util_streams}")
    list(APPEND yuri_base_sources "${yuri_generated_dir}/UtilStreams.cpp")

    # Yuri's Android launcher injects preferences through its frontend and
    # therefore compiles the desktop argv ingestion out.  The Vita backend has
    # no such frontend: the retail resolver passes the selected filter and
    # startup patch as ordinary process arguments.  Restore a bounded,
    # conventional argv parser so those options actually reach
    # TVPGetCommandLine before archives are opened.
    set(yuri_sysinit_impl "${yuri_core}/base/win32/SysInitImpl.cpp")
    file(READ "${yuri_sysinit_impl}" yuri_sysinit_impl_text)
    string(REPLACE
        "#include \"SysInitImpl.h\""
        "#include \"SysInitImpl.h\"\n#include \"tvpgl.h\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"krkrvita/yuri_additive_alpha_policy.hpp\""
        yuri_sysinit_impl_text "${yuri_sysinit_impl_text}")
    set(yuri_argv_start "static void PushAllCommandlineArguments()\n{")
    set(yuri_argv_end
        "//---------------------------------------------------------------------------\nstatic void PushConfigFileOptions")
    string(FIND "${yuri_sysinit_impl_text}" "${yuri_argv_start}"
        yuri_argv_start_offset)
    string(FIND "${yuri_sysinit_impl_text}" "${yuri_argv_end}"
        yuri_argv_end_offset)
    if(yuri_argv_start_offset LESS 0 OR yuri_argv_end_offset LESS 0 OR
       yuri_argv_end_offset LESS_EQUAL yuri_argv_start_offset)
        message(FATAL_ERROR "Yuri command-line ingestion boundary moved")
    endif()
    string(SUBSTRING "${yuri_sysinit_impl_text}" 0
        ${yuri_argv_start_offset} yuri_sysinit_prefix)
    string(SUBSTRING "${yuri_sysinit_impl_text}" ${yuri_argv_end_offset} -1
        yuri_sysinit_suffix)
    set(yuri_vita_argv_function
        "static void PushAllCommandlineArguments()\n{\n\tbool parse_options = true;\n\ttjs_int file_argument_count = 0;\n\tfor(tjs_int i = 1; i < _argc; ++i)\n\t{\n\t\tif(!_argv[i]) continue;\n\t\tif(parse_options && _argv[i][0] == '-' && _argv[i][1] == '-' && _argv[i][2] == 0)\n\t\t{\n\t\t\tparse_options = false;\n\t\t\tcontinue;\n\t\t}\n\t\tif(parse_options && _argv[i][0] == '-' && _argv[i][1] != 0)\n\t\t{\n\t\t\tttstr value(_argv[i]);\n\t\t\tTVPProgramArguments.push_back(TVPParseCommandLineOne(value));\n\t\t\tcontinue;\n\t\t}\n\t\tttstr argument = TJS_W(\"-arg\") + ttstr(file_argument_count++) +\n\t\t\tTJS_W(\"=\") + ttstr(_argv[i]);\n\t\tTVPProgramArguments.push_back(argument);\n\t}\n}\n")
    set(yuri_sysinit_impl_patched
        "${yuri_sysinit_prefix}${yuri_vita_argv_function}${yuri_sysinit_suffix}")
    set(yuri_project_delimiter_old [=[
	if (TVPIsExistentStorageNoSearchNoNormalize(TVPProjectDir)) {
		TVPProjectDir += TVPArchiveDelimiter;
	} else {
		TVPProjectDir += TJS_W("/");
	}
]=])
    set(yuri_project_delimiter_new [=[
	const tjs_char project_last = TVPProjectDir.GetLastChar();
	if (project_last != TJS_W('/') && project_last != TVPArchiveDelimiter) {
		if (TVPIsExistentStorageNoSearchNoNormalize(TVPProjectDir))
			TVPProjectDir += TVPArchiveDelimiter;
		else
			TVPProjectDir += TJS_W("/");
	}
]=])
    string(REPLACE "${yuri_project_delimiter_old}"
        "${yuri_project_delimiter_new}"
        yuri_sysinit_impl_patched "${yuri_sysinit_impl_patched}")
    if(yuri_sysinit_impl_patched STREQUAL
       "${yuri_sysinit_prefix}${yuri_vita_argv_function}${yuri_sysinit_suffix}")
        message(FATAL_ERROR "Yuri project-directory delimiter patch no longer applies")
    endif()
    set(yuri_software_policy
        "std::string _val = IndividualConfigManager::GetInstance()->GetValue<std::string>(\"renderer\", \"software\");")
    if(KRKRVITA_ENABLE_OPENGL_COMPOSITOR)
        string(REPLACE "${yuri_software_policy}"
            "std::string _val = \"opengl\";"
            yuri_sysinit_impl_renderer "${yuri_sysinit_impl_patched}")
        if(yuri_sysinit_impl_renderer STREQUAL yuri_sysinit_impl_patched)
            message(FATAL_ERROR "Yuri Vita OpenGL graphics policy patch no longer applies")
        endif()
        set(yuri_sysinit_impl_patched "${yuri_sysinit_impl_renderer}")
    endif()
    # Keep Yuri's public -drawthread semantics: zero/auto resolves to the
    # platform core count, one remains explicitly serial, and numeric values
    # retain their existing cap. RenderManager applies the Vita-specific
    # large-primitive threshold before it asks this policy for multiple cores.
    string(FIND "${yuri_sysinit_impl_patched}"
        "        tjs_int drawThreadNum = 0;"
        yuri_sysinit_auto_renderer_offset)
    if(yuri_sysinit_auto_renderer_offset LESS 0)
        message(FATAL_ERROR
            "Yuri Vita automatic software-renderer default moved")
    endif()
    set(yuri_draw_thread_assignment
        "        TVPDrawThreadNum = drawThreadNum;")
    set(yuri_draw_thread_assignment_vita
        "        TVPDrawThreadNum = drawThreadNum;\n        if(drawThreadNum == 0)\n          krkrvita_boot_trace(\"yuri-render-tasks-hybrid-large\");\n        else if(drawThreadNum == 1)\n          krkrvita_boot_trace(\"yuri-render-tasks-android-serial\");")
    string(REPLACE "${yuri_draw_thread_assignment}"
        "${yuri_draw_thread_assignment_vita}"
        yuri_sysinit_render_policy "${yuri_sysinit_impl_patched}")
    if(yuri_sysinit_render_policy STREQUAL yuri_sysinit_impl_patched)
        message(FATAL_ERROR
            "Yuri render-task policy trace patch no longer applies")
    endif()
    set(yuri_sysinit_impl_patched "${yuri_sysinit_render_policy}")
    set(yuri_additive_alpha_init [=[
	TVPGL_ASM_Init();
	krkrvita::select_exact_additive_alpha(TVPAlphaBlend_a, TVPAlphaBlend_a_c);
	krkrvita::select_exact_additive_alpha(TVPAlphaBlend_ao, TVPAlphaBlend_ao_c);
	krkrvita::select_exact_additive_alpha(TVPAdditiveAlphaBlend_a, TVPAdditiveAlphaBlend_a_c);
	krkrvita::select_exact_additive_alpha(TVPAdditiveAlphaBlend_ao, TVPAdditiveAlphaBlend_ao_c);
	krkrvita::select_exact_additive_alpha(TVPApplyColorMap_a, TVPApplyColorMap_a_c);
	krkrvita::select_exact_additive_alpha(TVPApplyColorMap65_a, TVPApplyColorMap65_a_c);
	krkrvita::select_exact_additive_alpha(TVPApplyColorMap_ao, TVPApplyColorMap_ao_c);
	krkrvita::select_exact_additive_alpha(TVPApplyColorMap65_ao, TVPApplyColorMap65_ao_c);
	krkrvita::select_exact_additive_alpha(TVPConstColorAlphaBlend_a, TVPConstColorAlphaBlend_a_c);
	krkrvita::select_exact_additive_alpha(TVPConvertAlphaToAdditiveAlpha, TVPConvertAlphaToAdditiveAlpha_c);
	krkrvita::select_exact_additive_alpha(TVPConvertAdditiveAlphaToAlpha, TVPConvertAdditiveAlphaToAlpha_c);
	krkrvita_boot_trace("yuri-additive-alpha-scalar-exact-ready");
]=])
    string(REPLACE "\tTVPGL_ASM_Init();" "${yuri_additive_alpha_init}"
        yuri_sysinit_alpha_exact "${yuri_sysinit_impl_patched}")
    if(yuri_sysinit_alpha_exact STREQUAL yuri_sysinit_impl_patched)
        message(FATAL_ERROR
            "Yuri additive-alpha scalar compatibility patch no longer applies")
    endif()
    set(yuri_sysinit_impl_patched "${yuri_sysinit_alpha_exact}")
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/SysInitImpl.cpp"
        CONTENT "${yuri_sysinit_impl_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_base_sources "${yuri_sysinit_impl}")
    list(APPEND yuri_base_sources "${yuri_generated_dir}/SysInitImpl.cpp")

    # Yuri intentionally seals dynamic DLL loading, but its old implementation
    # reported success even when no equivalent internal module existed. That
    # hides missing retail APIs until a later script member access. Generate an
    # explicit compatibility table for APIs provided by the core and reject
    # everything else at Plugins.link(). Also avoid reading four bytes before
    # a short filename while scanning optional .tpm modules.
    set(yuri_plugin_impl "${yuri_core}/base/win32/PluginImpl.cpp")
    file(READ "${yuri_plugin_impl}" yuri_plugin_impl_text)
    set(yuri_sealed_plugin_loader [=[
void TVPLoadPlugin(const ttstr & name)
{
	bool success = TVPLoadInternalPlugin(name);
    return; // seal all plugins
]=])
    set(yuri_checked_plugin_loader [=[
static bool TVPIsIntegratedPlugin(const ttstr &name)
{
	const ttstr module = TVPExtractStorageName(name).AsLowerCase();
	static const tjs_char *const integrated[] = {
#define KRKRVITA_YURI_TJS_PLUGIN(name) TJS_W(name),
		KRKRVITA_YURI_INTEGRATED_PLUGIN_MODULES(KRKRVITA_YURI_TJS_PLUGIN)
#undef KRKRVITA_YURI_TJS_PLUGIN
	};
	for(const auto *candidate : integrated)
		if(module == candidate) return true;
	return false;
}

static bool TVPTryLoadPlugin(const ttstr &name)
{
	const ttstr module = TVPExtractStorageName(name).AsLowerCase();
	if(TVPRegisteredPlugins.find(module) != TVPRegisteredPlugins.end()) return true;
	if(TVPLoadInternalPlugin(module)) return true;
	if(TVPIsIntegratedPlugin(module))
	{
		TVPRegisteredPlugins.insert(module);
		if(module == TJS_W("krmovie.dll"))
			krkrvita_boot_trace("retail-krmovie-core-alias-ready");
		return true;
	}
	return false;
}

void TVPLoadPlugin(const ttstr & name)
{
	if(TVPTryLoadPlugin(name)) return;
	TVPThrowExceptionMessage(TVPCannotLoadPlugin, name);
]=])
    string(REPLACE "${yuri_sealed_plugin_loader}"
        "${yuri_checked_plugin_loader}"
        yuri_plugin_impl_patched "${yuri_plugin_impl_text}")
    if(yuri_plugin_impl_patched STREQUAL yuri_plugin_impl_text)
        message(FATAL_ERROR "Yuri sealed-plugin loader patch no longer applies")
    endif()
    string(REPLACE
        "if (!strcasecmp(filename.c_str() + filename.length() - 4, \".tpm\")) {"
        "if (filename.length() >= 4 && !strcasecmp(filename.c_str() + filename.length() - 4, \".tpm\")) {"
        yuri_plugin_impl_safe "${yuri_plugin_impl_patched}")
    if(yuri_plugin_impl_safe STREQUAL yuri_plugin_impl_patched)
        message(FATAL_ERROR "Yuri short plugin-filename patch no longer applies")
    endif()
    string(REPLACE
        "#include \"PluginImpl.h\""
        "#include \"PluginImpl.h\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"krkrvita/yuri_plugin_capabilities.hpp\""
        yuri_plugin_impl_safe "${yuri_plugin_impl_safe}")
    string(REPLACE
        "\t\tTVPLoadPlugin((i->Path + \"/\" + i->Name).c_str());"
        "\t\tif(!TVPTryLoadPlugin((i->Path + \"/\" + i->Name).c_str()))\n\t\t{\n\t\t\tTVPAddImportantLog(ttstr(TJS_W(\"(info) Skipping Windows-only plugin \")) + ttstr(i->Name.c_str()));\n\t\t\tkrkrvita_boot_trace(\"retail-windows-plugin-skipped\");\n\t\t}"
        yuri_plugin_impl_safe "${yuri_plugin_impl_safe}")
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/PluginImpl.cpp"
        CONTENT "${yuri_plugin_impl_safe}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_base_sources "${yuri_plugin_impl}")
    list(APPEND yuri_base_sources "${yuri_generated_dir}/PluginImpl.cpp")

    # Yuri downloads p7zip 16.02 outside its source tree. Vita's eventual 7z
    # codec will implement the same tTVPArchive contract with VitaSDK's
    # libarchive instead of importing that Android build dependency.
    list(REMOVE_ITEM yuri_base_sources "${yuri_core}/base/7zArchive.cpp")
    add_library(krkrvita-yuri-base STATIC EXCLUDE_FROM_ALL
        ${yuri_base_sources}
    )
    krkrvita_set_yuri_backend_target_defaults(krkrvita-yuri-base)
    target_link_libraries(krkrvita-yuri-base PUBLIC krkrvita-yuri-tjs)

    file(GLOB yuri_utils_sources CONFIGURE_DEPENDS
        "${yuri_core}/utils/*.c"
        "${yuri_core}/utils/*.cpp"
        "${yuri_core}/utils/encoding/*.c"
        "${yuri_core}/utils/win32/*.cpp"
    )

    # KAG's stock Conductor requests tkdlVerbose even in retail games.  The
    # native parser consequently logs every scenario line, completed macro and
    # call-stack change.  Vita's console sink opens, appends and closes
    # engine.log for each fragment, so a large macro library can perform
    # thousands of synchronous ux0 operations before its first visible frame.
    # Keep tkdlSimple scenario diagnostics, errors and explicit Debug notices;
    # clamp only the parser's verbose gates in this Vita-only source overlay.
    set(yuri_kag_parser "${yuri_core}/utils/KAGParser.cpp")
    file(READ "${yuri_kag_parser}" yuri_kag_parser_text)
    string(REPLACE
        "#include \"EventIntf.h\""
        "#include \"EventIntf.h\"\n#include \"krkrvita/kag_inline_script.hpp\"\n#include \"krkrvita/kag_log_policy.hpp\"\n#include \"krkrvita/retail_bootstrap.hpp\""
        yuri_kag_parser_patched "${yuri_kag_parser_text}")
    if(yuri_kag_parser_patched STREQUAL yuri_kag_parser_text)
        message(FATAL_ERROR "Yuri KAG log-policy include patch no longer applies")
    endif()
    set(yuri_kag_inline_old [=[
			ttstr script;
			CurLine++;

			tjs_int script_start = CurLine;

			for(;CurLine < LineCount; CurLine++)
			{
				p = Lines[CurLine].Start;
				if((p[0] == TJS_W('[') &&
					(!TJS_strcmp(p, TJS_W("[endscript]")) ||
					 !TJS_strcmp(p, TJS_W("[endscript]\\")) ))||
				  (p[0] == TJS_W('@') &&
					(!TJS_strcmp(p, TJS_W("@endscript")) ) ) )
				{
					break;
				}

				if(ExcludeLevel == -1)
				{
					script += p;
					script += TJS_W("\r\n");
				}
			}

			if(CurLine == LineCount)
				 TVPThrowExceptionMessage(TVPKAGInlineScriptNotEnd);
]=])
    set(yuri_kag_inline_new [=[
			CurLine++;

			tjs_int script_start = CurLine;

			for(;CurLine < LineCount; CurLine++)
			{
				p = Lines[CurLine].Start;
				if((p[0] == TJS_W('[') &&
					(!TJS_strcmp(p, TJS_W("[endscript]")) ||
					 !TJS_strcmp(p, TJS_W("[endscript]\\")) ))||
				  (p[0] == TJS_W('@') &&
					(!TJS_strcmp(p, TJS_W("@endscript")) ) ) )
				{
					break;
				}
			}

			if(CurLine == LineCount)
				 TVPThrowExceptionMessage(TVPKAGInlineScriptNotEnd);

			std::basic_string<tjs_char> script_buffer;
			const bool large_inline_script = CurLine - script_start >= 128;
			if(ExcludeLevel == -1)
			{
				if(large_inline_script)
					krkrvita_boot_trace("yuri-kag-large-inline-assembly-entered");
				script_buffer = krkrvita::assemble_kag_inline_script<tjs_char>(
					static_cast<std::size_t>(script_start),
					static_cast<std::size_t>(CurLine),
					[this](std::size_t line) { return Lines[line].Start; });
				if(large_inline_script)
					krkrvita_boot_trace("yuri-kag-large-inline-assembly-complete");
			}
			ttstr script(script_buffer);
]=])
    string(REPLACE "${yuri_kag_inline_old}" "${yuri_kag_inline_new}"
        yuri_kag_parser_patched_inline "${yuri_kag_parser_patched}")
    if(yuri_kag_parser_patched_inline STREQUAL yuri_kag_parser_patched)
        message(FATAL_ERROR "Yuri KAG inline-script assembly patch no longer applies")
    endif()
    set(yuri_kag_callback_old [=[
					Owner->FuncCall(0, onScript_name.c_str(), onScript_name.GetHint(),
						NULL, 3, pparam, Owner);
]=])
    set(yuri_kag_callback_new [=[
					if(large_inline_script)
						krkrvita_boot_trace("yuri-kag-large-inline-execution-entered");
					Owner->FuncCall(0, onScript_name.c_str(), onScript_name.GetHint(),
						NULL, 3, pparam, Owner);
					if(large_inline_script)
						krkrvita_boot_trace("yuri-kag-large-inline-execution-complete");
]=])
    string(REPLACE "${yuri_kag_callback_old}" "${yuri_kag_callback_new}"
        yuri_kag_parser_patched_execution "${yuri_kag_parser_patched_inline}")
    if(yuri_kag_parser_patched_execution STREQUAL yuri_kag_parser_patched_inline)
        message(FATAL_ERROR "Yuri KAG inline execution marker patch no longer applies")
    endif()
    string(REPLACE
        "if(DebugLevel >= tkdlSimple)"
        "if(krkrvita::vita_kag_should_emit_log(DebugLevel, tkdlSimple))"
        yuri_kag_parser_patched_2 "${yuri_kag_parser_patched_execution}")
    string(REPLACE
        "if(DebugLevel >= tkdlVerbose)"
        "if(krkrvita::vita_kag_should_emit_log(DebugLevel, tkdlVerbose))"
        yuri_kag_parser_patched_3 "${yuri_kag_parser_patched_2}")
    if(yuri_kag_parser_patched_3 STREQUAL yuri_kag_parser_patched)
        message(FATAL_ERROR "Yuri KAG verbose-log clamp patch no longer applies")
    endif()
    set(yuri_kag_constructor [=[
tTJSNI_KAGParser::tTJSNI_KAGParser()
{
]=])
    set(yuri_kag_constructor_with_policy [=[
tTJSNI_KAGParser::tTJSNI_KAGParser()
{
	static const bool policy_reported = [] {
		krkrvita_boot_trace("yuri-kag-debug-log-disabled");
		return true;
	}();
	(void)policy_reported;
]=])
    string(REPLACE "${yuri_kag_constructor}"
        "${yuri_kag_constructor_with_policy}"
        yuri_kag_parser_patched_4 "${yuri_kag_parser_patched_3}")
    if(yuri_kag_parser_patched_4 STREQUAL yuri_kag_parser_patched_3)
        message(FATAL_ERROR "Yuri KAG log-policy marker patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/KAGParser.cpp"
        CONTENT "${yuri_kag_parser_patched_4}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_utils_sources "${yuri_kag_parser}")
    list(APPEND yuri_utils_sources "${yuri_generated_dir}/KAGParser.cpp")

    # Yuri's condition-variable wrapper dropped the persistent signaled bit
    # from Kirikiri's auto-reset event. Notifications issued just before a
    # waiter slept were lost, and spurious wakes were accepted as signals.
    # Generate the same public class with the original event semantics.
    set(yuri_thread_impl_header
        "${yuri_core}/utils/win32/ThreadImpl.h")
    file(READ "${yuri_thread_impl_header}" yuri_thread_header_text)
    string(REPLACE
        "\tstd::mutex Mutex;\n\npublic:"
        "\tstd::mutex Mutex;\n\tbool Signaled = false;\n\npublic:"
        yuri_thread_header_patched "${yuri_thread_header_text}")
    if(yuri_thread_header_patched STREQUAL yuri_thread_header_text)
        message(FATAL_ERROR "Yuri thread-event state patch no longer applies")
    endif()
    string(REPLACE
        "#include <condition_variable>"
        "#include <condition_variable>\n#include <atomic>\n#include <mutex>"
        yuri_thread_header_patched_2 "${yuri_thread_header_patched}")
    if(yuri_thread_header_patched_2 STREQUAL yuri_thread_header_patched)
        message(FATAL_ERROR "Yuri thread mutex-header patch no longer applies")
    endif()
    string(REPLACE
        "\tbool Terminated;"
        "\tstd::atomic<bool> Terminated;"
        yuri_thread_header_patched_3 "${yuri_thread_header_patched_2}")
    if(yuri_thread_header_patched_3 STREQUAL yuri_thread_header_patched_2)
        message(FATAL_ERROR "Yuri thread termination-state patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/ThreadImpl.h"
        CONTENT "${yuri_thread_header_patched_3}"
        @ONLY NEWLINE_STYLE UNIX)

    set(yuri_thread_impl "${yuri_core}/utils/win32/ThreadImpl.cpp")
    file(READ "${yuri_thread_impl}" yuri_thread_impl_text)
    string(REPLACE
        "#include <thread>"
        "#include <thread>\n#include <sched.h>\n#include \"krkrvita/render_task_pool.hpp\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"krkrvita/vita_pthread_priority.hpp\"\n#include \"krkrvita/vita_thread_policy.hpp\""
        yuri_thread_impl_text "${yuri_thread_impl_text}")
    set(yuri_thread_priority_old [=[
tTVPThreadPriority tTVPThread::GetPriority()
{
	sched_param npri = { 0 };
	int policy = 0;
	pthread_getschedparam(Handle, &policy, &npri);

	switch (policy)
	{
	case 5/*SCHED_IDLE*/: return ttpIdle;
	case 3/*SCHED_BATCH*/: return (npri.sched_priority == 0) ? ttpLowest : ttpLower;
	case 0/*SCHED_NORMAL*/:
		switch (npri.sched_priority) {
		case 0: return ttpNormal;
		case 1: return ttpHigher;
		case 2: return ttpHighest;
		}
		break;
	case 2/*SCHED_RR*/: return ttpTimeCritical;
	}

	return ttpNormal;
}
//---------------------------------------------------------------------------
void tTVPThread::SetPriority(tTVPThreadPriority pri)
{
	sched_param npri = { 0 }; //SCHED_NORMAL
	int policy = 0;
	switch (pri)
	{
	case ttpIdle:			policy = 5/*SCHED_IDLE*/;			break;
	case ttpLowest:			policy = 3/*SCHED_BATCH*/;	npri.sched_priority = 0;		break;
	case ttpLower:			policy = 3/*SCHED_BATCH*/;	npri.sched_priority = 1;        break;
	case ttpNormal:			policy = 0/*SCHED_NORMAL*/;		break;
	case ttpHigher:			policy = 0/*SCHED_NORMAL*/;	npri.sched_priority = 1;    break;
	case ttpHighest:		policy = 0/*SCHED_NORMAL*/;	npri.sched_priority = 2;	break;
	case ttpTimeCritical:	policy = 2/*SCHED_RR*/;	    break;
	}
	pthread_setschedparam(Handle, policy, &npri);
}
]=])
    set(yuri_thread_priority_vita [=[
namespace {
bool TVPGetVitaPthreadPriorityBounds(int &minimum, int &maximum)
{
	minimum = sched_get_priority_min(SCHED_OTHER);
	maximum = sched_get_priority_max(SCHED_OTHER);
	return minimum == krkrvita::kVitaPthreadPriorityMin &&
		maximum == krkrvita::kVitaPthreadPriorityMax;
}
}

tTVPThreadPriority tTVPThread::GetPriority()
{
	sched_param npri = { 0 };
	int policy = 0;
	int minimum = 0;
	int maximum = 0;
	if(pthread_getschedparam(Handle, &policy, &npri) != 0 ||
		policy != SCHED_OTHER ||
		!TVPGetVitaPthreadPriorityBounds(minimum, maximum))
		return ttpNormal;
	return static_cast<tTVPThreadPriority>(
		krkrvita::vita_yuri_rank_for_pthread_priority(npri.sched_priority));
}
//---------------------------------------------------------------------------
void tTVPThread::SetPriority(tTVPThreadPriority pri)
{
	const int rank = static_cast<int>(pri);
	int minimum = 0;
	int maximum = 0;
	if(rank < 0 || rank >= krkrvita::kYuriThreadPriorityCount ||
		!TVPGetVitaPthreadPriorityBounds(minimum, maximum))
	{
		krkrvita_boot_trace("yuri-thread-priority-apply-failed");
		TVPThrowInternalError;
	}
	sched_param npri = { 0 };
	npri.sched_priority =
		krkrvita::vita_pthread_priority_for_yuri_rank(rank);
	if(pthread_setschedparam(Handle, SCHED_OTHER, &npri) != 0)
	{
		krkrvita_boot_trace("yuri-thread-priority-apply-failed");
		TVPThrowInternalError;
	}
}
]=])
    string(REPLACE "${yuri_thread_priority_old}"
        "${yuri_thread_priority_vita}"
        yuri_thread_impl_priority_patched "${yuri_thread_impl_text}")
    if(yuri_thread_impl_priority_patched STREQUAL yuri_thread_impl_text)
        message(FATAL_ERROR "Yuri Vita pthread-priority patch no longer applies")
    endif()
    set(yuri_thread_impl_text "${yuri_thread_impl_priority_patched}")
    set(yuri_thread_event_old [=[
void tTVPThreadEvent::Set()
{
	std::unique_lock<std::mutex> lk(Mutex);
	Handle.notify_one();
}
//---------------------------------------------------------------------------
void tTVPThreadEvent::WaitFor(tjs_uint timeout)
{
	// wait for event;
	// returns true if the event is set, otherwise (when timed out) returns false.

	std::unique_lock<std::mutex> lk(Mutex);
	if (timeout != 0) {
		Handle.wait_for(lk, std::chrono::milliseconds(timeout));
	} else {
		Handle.wait(lk);
	}
#if 0
	DWORD state = WaitForSingleObject(Handle, timeout == 0 ? INFINITE : timeout);

	if(state == WAIT_OBJECT_0) return true;
	return false;
#endif
}
]=])
    set(yuri_thread_event_new [=[
void tTVPThreadEvent::Set()
{
	{
		std::lock_guard<std::mutex> lk(Mutex);
		Signaled = true;
	}
	Handle.notify_one();
}
//---------------------------------------------------------------------------
void tTVPThreadEvent::WaitFor(tjs_uint timeout)
{
	// Auto-reset event: Set remains observable until exactly one waiter
	// consumes it. This matches Kirikiri's Win32/modern portable backends.
	std::unique_lock<std::mutex> lk(Mutex);
	if(timeout != 0)
		Handle.wait_for(lk, std::chrono::milliseconds(timeout),
			[this] { return Signaled; });
	else
		Handle.wait(lk, [this] { return Signaled; });
	Signaled = false;
}
]=])
    string(REPLACE "${yuri_thread_event_old}" "${yuri_thread_event_new}"
        yuri_thread_impl_patched "${yuri_thread_impl_text}")
    if(yuri_thread_impl_patched STREQUAL yuri_thread_impl_text)
        message(FATAL_ERROR "Yuri thread-event implementation patch no longer applies")
    endif()
    set(yuri_processor_count_old [=[
static tjs_int GetProcesserNum(void)
{
  static tjs_int processor_num = 0;
  if (! processor_num) {
	  processor_num = std::thread::hardware_concurrency();
	tjs_char tmp[34];
	TVPAddLog(ttstr(TJS_W("Detected CPU core(s): ")) + TJS_tTVInt_to_str(processor_num, tmp));
  }
  return processor_num;
}
]=])
    set(yuri_processor_count_vita [=[
static tjs_int GetProcesserNum(void)
{
	// The Vita exposes three application/user cores. VitaSDK intentionally
	// implements std::thread::hardware_concurrency() as zero, which Yuri also
	// used as its "not initialized" sentinel and consequently logged forever.
	static std::once_flag log_once;
	std::call_once(log_once, [] {
		tjs_char tmp[34];
		TVPAddLog(ttstr(TJS_W("Detected CPU core(s): ")) + TJS_tTVInt_to_str(3, tmp));
	});
	return 3;
}
]=])
    string(REPLACE "${yuri_processor_count_old}" "${yuri_processor_count_vita}"
        yuri_thread_impl_patched_2 "${yuri_thread_impl_patched}")
    if(yuri_thread_impl_patched_2 STREQUAL yuri_thread_impl_patched)
        message(FATAL_ERROR "Yuri Vita processor-count patch no longer applies")
    endif()
    set(yuri_thread_start_old [=[
	tTVPThread* _this = ((tTVPThread*)arg);
	if (_this->Suspended) {
		std::unique_lock<std::mutex> lk(_this->_mutex);
		_this->_cond.wait(lk);
	}
]=])
    set(yuri_thread_start_vita [=[
	tTVPThread* _this = ((tTVPThread*)arg);
	{
		std::unique_lock<std::mutex> lk(_this->_mutex);
		_this->_cond.wait(lk, [_this] { return !_this->Suspended; });
	}
]=])
    string(REPLACE "${yuri_thread_start_old}" "${yuri_thread_start_vita}"
        yuri_thread_impl_patched_3 "${yuri_thread_impl_patched_2}")
    if(yuri_thread_impl_patched_3 STREQUAL yuri_thread_impl_patched_2)
        message(FATAL_ERROR "Yuri suspended-thread start patch no longer applies")
    endif()
    set(yuri_thread_resume_old [=[
void tTVPThread::Resume()
{
	Suspended = false;
	_cond.notify_one();
	//while((tjs_int32)ResumeThread(Handle) > 1) ;
}
]=])
    set(yuri_thread_resume_vita [=[
void tTVPThread::Resume()
{
	{
		std::lock_guard<std::mutex> lk(_mutex);
		Suspended = false;
	}
	_cond.notify_one();
	//while((tjs_int32)ResumeThread(Handle) > 1) ;
}
]=])
    string(REPLACE "${yuri_thread_resume_old}" "${yuri_thread_resume_vita}"
        yuri_thread_impl_patched_4 "${yuri_thread_impl_patched_3}")
    if(yuri_thread_impl_patched_4 STREQUAL yuri_thread_impl_patched_3)
        message(FATAL_ERROR "Yuri suspended-thread resume patch no longer applies")
    endif()

    # Retain a policy-checked persistent pool for large primitives, but do not
    # construct it until the hybrid selector actually requests multiple jobs.
    # Small dirty rectangles and explicit -drawthread=1 remain inline.
    set(yuri_serial_render_tasks [=[
void TVPExecThreadTask(int numThreads, TVP_THREAD_TASK_FUNC func)
{
  if (numThreads == 1) {
    func(0);
    return;
  }
#if !defined(USING_THREADPOOL11)
#pragma omp parallel for schedule(static)
  for (int i = 0; i < numThreads; ++i)
	  func(i);
#else
  static threadpool11::Pool pool;
  std::vector<std::future<void>> futures;
  for (int i = 0; i < numThreads; ++i) {
	  futures.emplace_back(pool.postWork<void>(std::bind(func, i)));
  }
  for (auto& it : futures)
	  it.get();
#endif
#if 0
  ThreadInfo *threadInfo;
  threadInfo = TVPThreadList[TVPThreadTaskCount++];
  threadInfo->lpStartAddress = func;
  threadInfo->lpParameter = param;
  InterlockedIncrement(&TVPRunningThreadCount);
  while (ResumeThread(threadInfo->thread) == 0)
    Sleep(0);
#endif
}
]=])
    set(yuri_vita_serial_render_tasks [=[
extern "C" void krkrvita_boot_trace(const char *message);

void TVPExecThreadTask(int numThreads, TVP_THREAD_TASK_FUNC func)
{
	if(numThreads <= 1) {
		if(numThreads == 1) func(0);
		return;
	}
	static krkrvita::RenderTaskPool pool(2, [](int worker_index) {
		krkrvita::apply_vita_render_worker_policy(worker_index);
	});
	static bool reported = false;
	if(!reported) {
		krkrvita_boot_trace("yuri-render-task-pool-ready");
		reported = true;
	}
	pool.run(numThreads, func);
}
]=])
    string(REPLACE "${yuri_serial_render_tasks}" "${yuri_vita_serial_render_tasks}"
        yuri_thread_impl_patched_5 "${yuri_thread_impl_patched_4}")
    if(yuri_thread_impl_patched_5 STREQUAL yuri_thread_impl_patched_4)
        message(FATAL_ERROR "Yuri Vita render-task pool patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/ThreadImpl.cpp"
        CONTENT "${yuri_thread_impl_patched_5}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_utils_sources "${yuri_thread_impl}")
    list(APPEND yuri_utils_sources "${yuri_generated_dir}/ThreadImpl.cpp")

    # KAG's first scenario yields on a 200 ms Timer. Mark the actual worker
    # trigger and main-thread dispatch boundaries so a hardware trace proves
    # scenario time is advancing instead of merely proving the event loop spins.
    set(yuri_timer_impl "${yuri_core}/utils/win32/TimerImpl.cpp")
    file(READ "${yuri_timer_impl}" yuri_timer_impl_text)
    string(REPLACE
        "#include \"UserEvent.h\""
        "#include \"UserEvent.h\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"krkrvita/vita_pthread_priority.hpp\"\n#include <psp2/kernel/threadmgr.h>"
        yuri_timer_impl_patched "${yuri_timer_impl_text}")
    string(REPLACE
        "void tTVPTimerThread::Execute()\n{\n\twhile(!GetTerminated())"
        "void tTVPTimerThread::Execute()\n{\n\tkrkrvita_boot_trace(\"yuri-timer-thread-entered\");\n\tconst tTVPThreadPriority requested_priority =\n\t\tTVPLimitTimerCapacity ? ttpNormal : ttpHighest;\n\tconst int expected_native_priority =\n\t\tkrkrvita::vita_native_priority_for_pthread_priority(\n\t\t\tkrkrvita::vita_pthread_priority_for_yuri_rank(\n\t\t\t\tstatic_cast<int>(requested_priority)));\n\tif(GetPriority() != requested_priority ||\n\t\tsceKernelGetThreadCurrentPriority() != expected_native_priority)\n\t{\n\t\tkrkrvita_boot_trace(\"yuri-timer-thread-policy-failed\");\n\t\tTVPThrowInternalError;\n\t}\n\tkrkrvita_boot_trace(\"yuri-timer-thread-policy-ready\");\n\tbool first_timer_fired = true;\n\twhile(!GetTerminated())"
        yuri_timer_impl_patched_2 "${yuri_timer_impl_patched}")
    string(REPLACE
        "\t\t\tif(any_triggered)\n\t\t\t{\n\t\t\t\t// triggered; post notification message to the UtilWindow"
        "\t\t\tif(any_triggered)\n\t\t\t{\n\t\t\t\tif(first_timer_fired) {\n\t\t\t\t\tkrkrvita_boot_trace(\"yuri-first-timer-fired\");\n\t\t\t\t\tfirst_timer_fired = false;\n\t\t\t\t}\n\t\t\t\t// triggered; post notification message to the UtilWindow"
        yuri_timer_impl_patched_3 "${yuri_timer_impl_patched_2}")
    string(REPLACE
        "\tif( ev.Message == TVP_EV_TIMER_THREAD && !GetTerminated())\n\t{\n\t\t// pending events occur"
        "\tif( ev.Message == TVP_EV_TIMER_THREAD && !GetTerminated())\n\t{\n\t\tstatic bool first_timer_dispatched = true;\n\t\tif(first_timer_dispatched) {\n\t\t\tkrkrvita_boot_trace(\"yuri-first-timer-dispatched\");\n\t\t\tfirst_timer_dispatched = false;\n\t\t}\n\t\t// pending events occur"
        yuri_timer_impl_patched_4 "${yuri_timer_impl_patched_3}")
    if(yuri_timer_impl_patched_4 STREQUAL yuri_timer_impl_text)
        message(FATAL_ERROR "Yuri KAG timer instrumentation patch no longer applies")
    endif()
    foreach(yuri_timer_marker
        yuri-timer-thread-entered
        yuri-timer-thread-policy-ready
        yuri-timer-thread-policy-failed
        yuri-first-timer-fired
        yuri-first-timer-dispatched)
        string(FIND "${yuri_timer_impl_patched_4}"
            "krkrvita_boot_trace(\"${yuri_timer_marker}\")"
            yuri_timer_marker_offset)
        if(yuri_timer_marker_offset LESS 0)
            message(FATAL_ERROR
                "Yuri KAG timer marker no longer applies: ${yuri_timer_marker}")
        endif()
    endforeach()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/TimerImpl.cpp"
        CONTENT "${yuri_timer_impl_patched_4}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_utils_sources "${yuri_timer_impl}")
    list(APPEND yuri_utils_sources "${yuri_generated_dir}/TimerImpl.cpp")
    add_library(krkrvita-yuri-utils STATIC EXCLUDE_FROM_ALL
        ${yuri_utils_sources}
    )
    krkrvita_set_yuri_backend_target_defaults(krkrvita-yuri-utils)
    target_link_libraries(krkrvita-yuri-utils PUBLIC
        krkrvita-yuri-base
        krkrvita-yuri-tjs
    )

    file(GLOB yuri_extension_sources CONFIGURE_DEPENDS
        "${yuri_core}/extension/*.cpp"
    )
    add_library(krkrvita-yuri-extension STATIC EXCLUDE_FROM_ALL
        ${yuri_extension_sources}
        "${yuri_core}/msg/MsgIntf.cpp"
        "${yuri_core}/msg/win32/MsgImpl.cpp"
        "${yuri_core}/msg/win32/OptionsDesc.cpp"
    )
    krkrvita_set_yuri_backend_target_defaults(krkrvita-yuri-extension)
    target_link_libraries(krkrvita-yuri-extension PUBLIC
        krkrvita-yuri-utils
        krkrvita-yuri-base
        krkrvita-yuri-tjs
    )

    file(GLOB yuri_sound_sources CONFIGURE_DEPENDS
        "${yuri_core}/sound/*.cpp"
        "${yuri_core}/sound/win32/*.c"
        "${yuri_core}/sound/win32/*.cpp"
    )
    list(REMOVE_ITEM yuri_sound_sources
        "${yuri_core}/sound/FFWaveDecoder.cpp"
        "${yuri_core}/sound/WaveFormatConverter_SSE.cpp"
        "${yuri_core}/sound/xmmlib.cpp"
        "${yuri_core}/sound/win32/WaveMixer.cpp"
    )
    list(APPEND yuri_sound_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vita/yuri_ffwave_decoder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vita/yuri_openal_mixer.cpp"
    )
    add_library(krkrvita-yuri-sound STATIC EXCLUDE_FROM_ALL
        ${yuri_sound_sources}
    )
    krkrvita_set_yuri_backend_target_defaults(krkrvita-yuri-sound)
    # opusfile.h includes the rest of libopus with unqualified names (for
    # example <opus_multistream.h>), matching pkg-config's public include
    # flags. VitaSDK installs those headers in include/opus.
    target_include_directories(krkrvita-yuri-sound PRIVATE
        "${VITASDK}/arm-vita-eabi/include/opus"
    )
    target_link_libraries(krkrvita-yuri-sound PUBLIC
        krkrvita-yuri-extension
        krkrvita-yuri-utils
        krkrvita-yuri-base
        krkrvita-yuri-tjs
        openal
        krkrvita-vita-avformat
        krkrvita-vita-avcodec
        krkrvita-vita-avutil
    )

    # Keep Yuri's layer, bitmap, transition and image-codec units together.
    # The default is Yuri's software compositor feeding the narrow VitaGL
    # screen presenter; no Cocos scene graph is linked.
    file(GLOB yuri_visual_sources CONFIGURE_DEPENDS
        "${yuri_core}/visual/*.cpp"
        "${yuri_core}/visual/gl/*.cpp"
        "${yuri_core}/visual/win32/*.cpp"
    )

    # Quoted includes search a source file's own directory before -I paths.
    # These upstream units therefore cannot consume the generated texture ABI
    # while compiled in-place: each would silently bind to Yuri's original
    # RenderManager.h.  Overlay every active same-directory include site into
    # generated/yuri, and overlay RenderManager_software.h so any nested quote
    # include resolves to the same generated ABI as well.
    set(yuri_render_manager_overlay_sources
        "${yuri_core}/visual/LayerBitmapIntf.cpp"
        "${yuri_core}/visual/LayerIntf.cpp"
        "${yuri_core}/visual/TransIntf.cpp")
    set(yuri_render_manager_overlay_outputs)
    foreach(yuri_render_manager_overlay_source
            IN LISTS yuri_render_manager_overlay_sources)
        get_filename_component(yuri_render_manager_overlay_name
            "${yuri_render_manager_overlay_source}" NAME)
        set(yuri_render_manager_overlay_output
            "${yuri_generated_dir}/${yuri_render_manager_overlay_name}")
        configure_file("${yuri_render_manager_overlay_source}"
            "${yuri_render_manager_overlay_output}" COPYONLY)
        list(APPEND yuri_render_manager_overlay_outputs
            "${yuri_render_manager_overlay_output}")
    endforeach()
    configure_file("${yuri_core}/visual/RenderManager_software.h"
        "${yuri_generated_dir}/RenderManager_software.h" COPYONLY)
    # Yuri widened Kirikiri's opaque-area optimisation in
    # QueryUpdateExcludeRect from `DisplayType == ltOpaque` to also trust
    # `MainImage->IsOpaque()`. That flag is a raw bool on tTVPBitmap: it is set
    # true only when a decoded image had no alpha channel, and the CPU drawing
    # paths in LayerBitmapIntf.cpp (Fill, ColorRect, Blt) never clear it. A
    # stale true makes the compositor treat an alpha layer as fully covering
    # and skip drawing everything beneath it.
    #
    # The affected title uses a full-screen ltAddAlpha message layer created
    # with `@position frame="" opacity=0`, and its message box is the same kind
    # of layer at opacity 128. Mistaking either for opaque erases the art
    # underneath and leaves only that layer's own text and link highlights,
    # which is exactly the reported black title screen and black message box.
    #
    # Trust the bitmap flag only for layer types that do not composite through
    # an alpha channel. Being wrong in this direction costs a little redundant
    # drawing; being wrong the other way erases the frame.
    set(yuri_layer_intf_overlay "${yuri_generated_dir}/LayerIntf.cpp")
    file(READ "${yuri_layer_intf_overlay}" yuri_layer_intf_text)
    set(yuri_layer_intf_exclude
        "if (parentvisible && (DisplayType == ltOpaque || (MainImage && MainImage->IsOpaque())) && Opacity == 255)")
    set(yuri_layer_intf_exclude_vita
        "if (parentvisible && (DisplayType == ltOpaque || (MainImage && MainImage->IsOpaque() && !TVPIsTypeUsingAlphaChannel(DisplayType))) && Opacity == 255)")
    string(REPLACE "${yuri_layer_intf_exclude}" "${yuri_layer_intf_exclude_vita}"
        yuri_layer_intf_guarded "${yuri_layer_intf_text}")
    if(yuri_layer_intf_guarded STREQUAL yuri_layer_intf_text)
        message(FATAL_ERROR
            "Yuri opaque-exclude alpha-channel guard patch no longer applies")
    endif()

    # An alpha-bearing layer must not reuse stale pixels from the shared
    # compositor surface when its opaque-exclusion rectangle covers a region.
    set(yuri_layer_intf_copyself_skip "\t\t\t\t;// nothing to do")
    set(yuri_layer_intf_copyself_fixed
        "\t\t\t\tif(TVPIsTypeUsingAlphaChannel(DisplayType))\n\t\t\t\t\tCopySelfForRect(dest, destx, desty, r);\n\t\t\t\t;// nothing to do")
    string(REPLACE "${yuri_layer_intf_copyself_skip}"
        "${yuri_layer_intf_copyself_fixed}"
        yuri_layer_intf_fixed "${yuri_layer_intf_guarded}")
    if(yuri_layer_intf_fixed STREQUAL yuri_layer_intf_guarded)
        message(FATAL_ERROR "Yuri CopySelf alpha-layer patch no longer applies")
    endif()
    file(WRITE "${yuri_layer_intf_overlay}" "${yuri_layer_intf_fixed}")

    list(REMOVE_ITEM yuri_visual_sources
        ${yuri_render_manager_overlay_sources})
    list(APPEND yuri_visual_sources
        ${yuri_render_manager_overlay_outputs})


    # Capture finalized compositor damage for partial screen uploads without
    # adding timing or profiling instrumentation to the release build.
    set(yuri_layer_manager "${yuri_core}/visual/LayerManager.cpp")
    file(READ "${yuri_layer_manager}" yuri_layer_manager_text)
    string(REPLACE
        "#include \"LayerManager.h\""
        "#include \"LayerManager.h\"\n#include \"krkrvita/layer_draw_completion.hpp\"\n#include \"krkrvita/yuri_frame_damage.hpp\""
        yuri_layer_manager_profiled "${yuri_layer_manager_text}")
    set(yuri_layer_manager_profiled_2 "${yuri_layer_manager_profiled}")

    # BeforeCompletion() may run onPaint handlers and transitions which add to
    # UpdateRegion. CompleteForWindow() deliberately calls this hook only
    # after that work and immediately before InternalComplete2() consumes and
    # clears the region, so this is the one authoritative presenter snapshot.
    set(yuri_layer_damage_unprofiled [=[
void tTVPLayerManager::NotifyUpdateRegionFixed()
{
	// called by primary layer, notifying final update region is fixed
//	Window->NotifyUpdateRegionFixed(UpdateRegion);
}
]=])
    set(yuri_layer_damage_finalized [=[
void tTVPLayerManager::NotifyUpdateRegionFixed()
{
	// called by primary layer after BeforeCompletion() has finalized damage
	tjs_int damage_width = 0;
	tjs_int damage_height = 0;
	if(GetPrimaryLayerSize(damage_width, damage_height))
		krkrvita_yuri_begin_frame_damage(damage_width, damage_height);
	else
		krkrvita_yuri_begin_frame_damage(0, 0);
	tTVPComplexRect::tIterator iterator = UpdateRegion.GetIterator();
	while(iterator.Step()) {
		const tTVPRect rect(*iterator);
		krkrvita_yuri_add_frame_damage(
			rect.left, rect.top, rect.right, rect.bottom);
	}
//	Window->NotifyUpdateRegionFixed(UpdateRegion);
}
]=])
    string(REPLACE "${yuri_layer_damage_unprofiled}"
        "${yuri_layer_damage_finalized}" yuri_layer_manager_profiled_3
        "${yuri_layer_manager_profiled_2}")
    if(yuri_layer_manager_profiled_3 STREQUAL yuri_layer_manager_profiled_2)
        message(FATAL_ERROR "Yuri finalized damage capture patch no longer applies")
    endif()

    # An opaque primary with visible children composites directly into the
    # manager's persistent DrawBuffer. Yuri then reports that same buffer and
    # rectangle back to DrawCompleted, whose unconditional Blt copies every
    # completed pixel onto itself. Preserve shifted self-copy and blend
    # semantics while eliminating only the exact opaque-copy no-op.
    set(yuri_layer_draw_completed_blt [=[
	DrawBuffer->Blt(destrect.left, destrect.top, bmp, cliprect, type, opacity, HoldAlpha);
]=])
    set(yuri_layer_draw_completed_blt_guarded [=[
	if(krkrvita::is_noop_drawbuffer_completion(
		DrawBuffer, bmp, destrect, cliprect, type, ltOpaque, opacity))
		return;
	DrawBuffer->Blt(destrect.left, destrect.top, bmp, cliprect, type, opacity, HoldAlpha);
]=])
    string(REPLACE "${yuri_layer_draw_completed_blt}"
        "${yuri_layer_draw_completed_blt_guarded}"
        yuri_layer_manager_profiled_4 "${yuri_layer_manager_profiled_3}")
    if(yuri_layer_manager_profiled_4 STREQUAL yuri_layer_manager_profiled_3)
        message(FATAL_ERROR
            "Yuri draw-buffer completion no-op patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/LayerManager.cpp"
        CONTENT "${yuri_layer_manager_profiled_4}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_layer_manager}")
    list(APPEND yuri_visual_sources "${yuri_generated_dir}/LayerManager.cpp")

    # Yuri deliberately routes every decoded layer bitmap through
    # iTVPMemoryAllocator. Keep its metadata, sentinels and critical-section
    # semantics intact. Small allocations stay on newlib; large CPU surfaces
    # use cached USER_RW memblocks kept outside VitaGL's pools so a fragmented
    # general heap cannot randomly lose the next 1280x960 allocation.
    set(yuri_bitmap_bits_allocator
        "${yuri_core}/visual/win32/BitmapBitsAlloc.cpp")
    file(READ "${yuri_bitmap_bits_allocator}" yuri_bitmap_bits_allocator_text)
    set(yuri_bitmap_basic_allocator_old [=[
class BasicAllocator : public iTVPMemoryAllocator
{
public:
	BasicAllocator() {
		TVPAddLog( TJS_W("(info) Use malloc for Bitmap") );
	}
	void* allocate( size_t size ) { return malloc(size); }
	void free( void* mem ) { ::free( mem ); }
};
]=])
    set(yuri_bitmap_basic_allocator_vita [=[
class BasicAllocator : public iTVPMemoryAllocator
{
public:
	BasicAllocator() {
		TVPAddLog( TJS_W("(info) Use USER_RW memblocks for large Bitmap") );
		krkrvita_boot_trace("yuri-bitmap-tiered-allocator-ready");
	}
	void* allocate( size_t size ) {
		void* memory = krkrvita::vita_bitmap_allocate(size);
		if(!memory) {
			krkrvita_yuri_reclaim_bitmap_memory();
			memory = krkrvita::vita_bitmap_allocate(size);
			if(memory) krkrvita_boot_trace("yuri-bitmap-oom-recovered");
		}
		return memory;
	}
	void free( void* mem ) { krkrvita::vita_bitmap_deallocate(mem); }
};
]=])
    string(REPLACE "#include \"DebugIntf.h\""
        "#include \"DebugIntf.h\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"krkrvita/vita_bitmap_allocator.hpp\"\nextern \"C\" void krkrvita_yuri_reclaim_bitmap_memory();"
        yuri_bitmap_bits_allocator_patched
        "${yuri_bitmap_bits_allocator_text}")
    string(REPLACE "${yuri_bitmap_basic_allocator_old}"
        "${yuri_bitmap_basic_allocator_vita}"
        yuri_bitmap_bits_allocator_patched
        "${yuri_bitmap_bits_allocator_patched}")
    if(yuri_bitmap_bits_allocator_patched STREQUAL
       yuri_bitmap_bits_allocator_text)
        message(FATAL_ERROR "Yuri Vita bitmap allocator patch no longer applies")
    endif()
    foreach(yuri_bitmap_allocator_contract
        "Use USER_RW memblocks for large Bitmap"
        "krkrvita::vita_bitmap_allocate(size)"
        "krkrvita::vita_bitmap_deallocate(mem)")
        string(FIND "${yuri_bitmap_bits_allocator_patched}"
            "${yuri_bitmap_allocator_contract}"
            yuri_bitmap_allocator_contract_offset)
        if(yuri_bitmap_allocator_contract_offset LESS 0)
            message(FATAL_ERROR
                "Yuri tiered bitmap allocator no longer applies: ${yuri_bitmap_allocator_contract}")
        endif()
    endforeach()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/BitmapBitsAlloc.cpp"
        CONTENT "${yuri_bitmap_bits_allocator_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_bitmap_bits_allocator}")
    list(APPEND yuri_visual_sources
        "${yuri_generated_dir}/BitmapBitsAlloc.cpp")

    # Yuri checks its graphics-cache size before inserting a newly decoded
    # image. That leaves the cache over budget by one complete image forever;
    # for a 1280x960 retail title the error is almost 5 MiB per insertion.
    # Enforce the bound after every insertion as well. Under the hardware
    # renderer the cache owns SGX textures, while the CPU decode bitmap can be
    # released immediately after upload.
    set(yuri_graphics_loader
        "${yuri_core}/visual/GraphicsLoaderIntf.cpp")
    file(READ "${yuri_graphics_loader}" yuri_graphics_loader_text)
    set(yuri_graphics_loader_patched "${yuri_graphics_loader_text}")
    string(REPLACE
        "TVPGraphicCache.AddWithHash(searchdata, hash, holder);"
        "TVPGraphicCache.AddWithHash(searchdata, hash, holder);\n\t\t\tTVPCheckGraphicCacheLimit();"
        yuri_graphics_loader_patched "${yuri_graphics_loader_patched}")
    string(REPLACE
        "TVPGraphicCache.AddWithHash(item.searchdata, hash, holder);"
        "TVPGraphicCache.AddWithHash(item.searchdata, hash, holder);\n\t\t\tTVPCheckGraphicCacheLimit();"
        yuri_graphics_loader_patched "${yuri_graphics_loader_patched}")
    if(yuri_graphics_loader_patched STREQUAL yuri_graphics_loader_text)
        message(FATAL_ERROR "Yuri graphics-cache bound patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/GraphicsLoaderIntf.cpp"
        CONTENT "${yuri_graphics_loader_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_graphics_loader}")
    list(APPEND yuri_visual_sources
        "${yuri_generated_dir}/GraphicsLoaderIntf.cpp")

    # Keep the experimental Yuri OpenGL compositor available for later work,
    # but do not compile or register it in the default Vita build. Its hidden
    # framebuffer/depth allocations are not stable enough for retail games.
    if(KRKRVITA_ENABLE_OPENGL_COMPOSITOR)
    # Generate a narrow platform adaptation: raw VitaGL state replaces Cocos'
    # cache, frontend callbacks are omitted, and mutable FBO textures use
    # SGX543's native NPOT support.
    set(yuri_ogl_render_manager
        "${yuri_core}/visual/ogl/RenderManager_ogl.cpp")
    file(READ "${yuri_ogl_render_manager}" yuri_ogl_text)
    set(yuri_ogl_original_text "${yuri_ogl_text}")
    set(yuri_vitagl_ogl_header [=[
#include <vitaGL.h>
#include <set>
#include "krkrvita/vitagl_presenter.hpp"
#include "krkrvita/vita_render_surface.hpp"

#ifndef CHECK_GL_ERROR_DEBUG
#define CHECK_GL_ERROR_DEBUG() ((void)0)
#define CHECK_GL_ERROR_DEBUG_WITH_FMT(...) ((void)0)
#endif
#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#endif
#ifndef GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2
#define GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 0x9276
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#endif
#ifndef GL_FRAMEBUFFER_UNSUPPORTED
#define GL_FRAMEBUFFER_UNSUPPORTED 0x8CDD
#endif
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
]=])
    string(REPLACE "#include \"ogl_common.h\"" "${yuri_vitagl_ogl_header}"
        yuri_ogl_text "${yuri_ogl_text}")
    foreach(yuri_cocos_header
        "#include \"renderer/CCGLProgramCache.h\"\n"
        "#include \"renderer/CCGLProgram.h\"\n"
        "#include \"base/CCDirector.h\"\n"
        "#include \"base/CCEventListenerCustom.h\"\n"
        "#include \"base/CCEventDispatcher.h\"\n"
        "#include \"base/CCEventType.h\"\n")
        string(REPLACE "${yuri_cocos_header}" "" yuri_ogl_text "${yuri_ogl_text}")
    endforeach()
    string(REPLACE
        "typedef void* (EGLAPIENTRY fGetProcAddress)(const char *);"
        "typedef void* (fGetProcAddress)(const char *);"
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "\tcocos2d::Director::getInstance()->setViewport();"
        "\tglViewport(0, 0, 960, 544);"
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "\tclass AdapterTexture2D : public cocos2d::Texture2D {"
        "#ifndef __vita__\n\tclass AdapterTexture2D : public cocos2d::Texture2D {"
        yuri_ogl_text "${yuri_ogl_text}")
    set(yuri_ogl_adapter_end [=[
	}
public:
	virtual bool IsOpaque() override {
]=])
    set(yuri_ogl_vita_adapter_end [=[
	}
#endif
#ifdef __vita__
	virtual cocos2d::Texture2D* GetAdapterTexture(cocos2d::Texture2D*) override {
		return nullptr;
	}
#endif
public:
	virtual bool IsOpaque() override {
]=])
    string(REPLACE "${yuri_ogl_adapter_end}" "${yuri_ogl_vita_adapter_end}"
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "\t\tcocos2d::EventListenerCustom *listener ="
        "#ifndef __vita__\n\t\tcocos2d::EventListenerCustom *listener ="
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "\t\tTVPSetPostUpdateEvent(_RestoreGLStatues);"
        "\t\tTVPSetPostUpdateEvent(_RestoreGLStatues);\n#endif"
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "unsigned int intw = power_of_two(w * sw), inth = power_of_two(h * sh);"
        "unsigned int intw = std::max(1u, (unsigned int)(w * sw));\n\t\t\tunsigned int inth = std::max(1u, (unsigned int)(h * sh));"
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "\t\tintw = power_of_two(intw), inth = power_of_two(inth);"
        "\t\t// SGX543 and VitaGL support renderable NPOT textures."
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "InternalInit(nullptr, power_of_two(w), power_of_two(h), 0);"
        "InternalInit(nullptr, w, h, 0);"
        yuri_ogl_text "${yuri_ogl_text}")

    # Yuri already separates logical layer dimensions from the physical
    # mutable backing texture through sw/sh. Its Android low-memory policy
    # uses a fixed half scale; fit that existing mechanism to the Vita display
    # instead. A 1280x960 logical layer becomes 725x544, eliminating the
    # 4,915,200-byte allocation seen in the hardware coredumps while retaining
    # Kirikiri coordinates and full-resolution source artwork.
    set(yuri_ogl_mutable_normal_old [=[
	static iTVPTexture2D *CreateMutableTexture2D_normal(const void *pixel, int pitch, unsigned int w, unsigned int h, TVPTextureFormat::e fmt) {
		float sw = 1.f, sh = 1.f;

		if (!pixel) {
			if (w > GetMaxTextureWidth()) {
				int n = (w + GetMaxTextureWidth() - 1) / GetMaxTextureWidth();
				sw = 1.0f / n;
			}
			if (h > GetMaxTextureHeight()) {
				int n = (h + GetMaxTextureHeight() - 1) / GetMaxTextureHeight();
				sh = 1.0f / n;
			}
		}

		return CreateMutableTexture2D(pixel, pitch, w, h, sw, sh, fmt);
	}
]=])
    set(yuri_ogl_mutable_vita [=[
	static iTVPTexture2D *CreateMutableTexture2D_normal(const void *pixel, int pitch, unsigned int w, unsigned int h, TVPTextureFormat::e fmt) {
		float sw = 1.f, sh = 1.f;
		if (!pixel) {
			const auto surface = krkrvita::fit_vita_render_surface(
				static_cast<int>(w), static_cast<int>(h));
			sw = surface.scale_width;
			sh = surface.scale_height;
			if (surface.is_scaled()) {
				static bool scaling_reported = false;
				if (!scaling_reported) {
					krkrvita_boot_trace("yuri-vita-render-target-scaled");
					scaling_reported = true;
				}
			}
		}
		return CreateMutableTexture2D(pixel, pitch, w, h, sw, sh, fmt);
	}
]=])
    string(REPLACE "${yuri_ogl_mutable_normal_old}"
        "${yuri_ogl_mutable_vita}" yuri_ogl_text "${yuri_ogl_text}")
    if(NOT yuri_ogl_text MATCHES "yuri-vita-render-target-scaled")
        message(FATAL_ERROR "Yuri Vita display-sized render target patch no longer applies")
    endif()

    # Yuri's lossless solid-image path stores a uniform RGBA bitmap as a 1x1
    # texture and preserves its logical extent through texture scale. Enable
    # that optimization without selecting Yuri's lossy half-resolution policy.
    set(yuri_ogl_static_normal_old [=[
	static iTVPTexture2D *CreateStaticTexture2D_normal(const void *dib, tjs_uint w, tjs_uint h, tjs_int pitch,
		TVPTextureFormat::e fmt, bool isOpaque) {
		int n = (w + GetMaxTextureWidth() - 1) / GetMaxTextureWidth();
		int tw = (w + n - 1) / n;
		n = (h + GetMaxTextureHeight() - 1) / GetMaxTextureHeight();
		int th = (h + n - 1) / n;
		return CreateStaticTexture2D(dib, w, h, pitch, fmt, tw, th, isOpaque);
	}

	static iTVPTexture2D *CreateStaticTexture2D_solid(const void *dib, tjs_uint w, tjs_uint h, tjs_int pitch,
		TVPTextureFormat::e fmt) {
		if (TVPCheckSolidPixel((const tjs_uint8*)dib, pitch, w, h)) {
			tTVPOGLTexture2D_static *ret = new tTVPOGLTexture2D_static(dib, 4, 1, 1, fmt, w, h, 1.0f / w, 1.0f / h, GL_NEAREST);
			return ret;
		}
		return nullptr;
	}
]=])
    set(yuri_ogl_static_vita [=[
	static iTVPTexture2D *CreateStaticTexture2D_solid(const void *dib, tjs_uint w, tjs_uint h, tjs_int pitch,
		TVPTextureFormat::e fmt) {
		if (TVPCheckSolidPixel((const tjs_uint8*)dib, pitch, w, h)) {
			tTVPOGLTexture2D_static *ret = new tTVPOGLTexture2D_static(dib, 4, 1, 1, fmt, w, h, 1.0f / w, 1.0f / h, GL_NEAREST);
			return ret;
		}
		return nullptr;
	}

	static iTVPTexture2D *CreateStaticTexture2D_normal(const void *dib, tjs_uint w, tjs_uint h, tjs_int pitch,
		TVPTextureFormat::e fmt, bool isOpaque) {
		if (dib && fmt == TVPTextureFormat::RGBA) {
			iTVPTexture2D *solid = CreateStaticTexture2D_solid(dib, w, h, pitch, fmt);
			if (solid) return solid;
		}
		int n = (w + GetMaxTextureWidth() - 1) / GetMaxTextureWidth();
		int tw = (w + n - 1) / n;
		n = (h + GetMaxTextureHeight() - 1) / GetMaxTextureHeight();
		int th = (h + n - 1) / n;
		return CreateStaticTexture2D(dib, w, h, pitch, fmt, tw, th, isOpaque);
	}
]=])
    string(REPLACE "${yuri_ogl_static_normal_old}"
        "${yuri_ogl_static_vita}" yuri_ogl_text "${yuri_ogl_text}")
    string(FIND "${yuri_ogl_text}"
        "iTVPTexture2D *solid = CreateStaticTexture2D_solid"
        yuri_ogl_solid_texture_patch)
    if(yuri_ogl_solid_texture_patch LESS 0)
        message(FATAL_ERROR "Yuri Vita solid texture patch no longer applies")
    endif()
    string(REPLACE
        "glBlendColor(v, v, v, v);"
        "glUniform1f(glGetUniformLocation(program, \"opacity\"), v);"
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "glUniform4f(id, Value[0], Value[1], Value[2], Value[4]);"
        "glUniform4f(id, Value[0], Value[1], Value[2], Value[3]);"
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "OperateTriangles(method, nQuads * 2, _tar, reftar, rcclip, &pttar[0], textures);"
        "OperateTriangles(method, 2, _tar, reftar, rcclip, &pttar[0], textures);"
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "\t\tTVPInitGLExtensionFunc();"
        "\t\tTVPInitGLExtensionFunc();\n#ifdef __vita__\n\t\t// VitaGL's pitched upload extension currently corrupts non-tight rows.\n\t\tGL_CHECK_unpack_subimage = false;\n#endif\n\t\tkrkrvita_boot_trace(\"yuri-opengl-renderer-initializing\");"
        yuri_ogl_text "${yuri_ogl_text}")
    string(REPLACE
        "#include \"pvr.h\""
        "#include \"pvr.h\"\n#include \"krkrvita/retail_bootstrap.hpp\""
        yuri_ogl_text "${yuri_ogl_text}")
    set(yuri_ogl_texture_formats_old [=[
		GLint nTexFormats; glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &nTexFormats);
		std::vector<GLint> texFormats; texFormats.resize(nTexFormats);
		glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, &texFormats.front());
		for (GLint f : texFormats) {
			TVPTextureFormats.insert(f);
		}
]=])
    set(yuri_ogl_texture_formats_vita [=[
		GLint nTexFormats = 0;
		glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &nTexFormats);
		if (nTexFormats > 0) {
			std::vector<GLint> texFormats(static_cast<std::size_t>(nTexFormats));
			glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, texFormats.data());
			for (GLint f : texFormats) TVPTextureFormats.insert(f);
		}
]=])
    string(REPLACE "${yuri_ogl_texture_formats_old}"
        "${yuri_ogl_texture_formats_vita}" yuri_ogl_text "${yuri_ogl_text}")
    # Compile Yuri's complete built-in render-method catalogue in one
    # SceShaccCg session, matching the backend's native ownership model.  The
    # old Vita adapter deferred every method and then tore the compiler down
    # after each individual shader.  That made ordinary logo transitions
    # repeatedly restart libshacccg during gameplay and could leave the old
    # VitaGL compiler state fragmented.  The retail VPK now carries Sony's
    # ATTRIBUTE2=12 memory entitlement, so the original eager catalogue fits.
    # Release the compiler once, after every built-in program is linked and
    # before Kirikiri begins allocating its full-size layer bitmaps.
    string(REPLACE
        "#endif\n\t}\n\n\ttTVPOGLTexture2D *tempTexture;"
        "#endif\n\t\tkrkrvita_boot_trace(\"yuri-opengl-shader-catalogue-ready\");\n\t\tglReleaseShaderCompiler();\n\t\tkrkrvita_boot_trace(\"yuri-opengl-shader-compiler-released\");\n\t\tkrkrvita_boot_trace(\"yuri-opengl-first-shader-ready\");\n\t\tkrkrvita_boot_trace(\"yuri-opengl-renderer-ready\");\n\t}\n\n\ttTVPOGLTexture2D *tempTexture;"
        yuri_ogl_text "${yuri_ogl_text}")

    string(REPLACE
        "\t\tInitGL();\n#ifdef TEST_SHADER_ENABLED"
        "\t\tInitGL();\n\t\tkrkrvita_boot_trace(\"yuri-opengl-state-ready\");\n\t\tkrkrvita_boot_trace(\"yuri-opengl-first-shader-compiling\");\n#ifdef TEST_SHADER_ENABLED"
        yuri_ogl_text "${yuri_ogl_text}")

    # VitaGL deliberately has no desktop glBlendColor entry point. Preserve
    # Yuri's constant-opacity semantics in shaders by sampling the destination
    # FBO, just as Yuri already does for blend modes without framebuffer-fetch
    # extensions. This covers every constant-color blend registration.
    set(yuri_ogl_constant_fill_old [=[
		CompileAndRegScript<tTVPOGLRenderMethod_Script_BlendColor>("FillMask", "void main(){gl_FragColor = vec4(0,0,0,1);}", 0)
			->SetBlendFuncSeparate(GL_FUNC_ADD, GL_ZERO, GL_ONE, GL_CONSTANT_COLOR, GL_ZERO);
		TEST_SHADER(FillMask, TVPFillMask(testdest, 256 * 256, TEST_SHADER_OPA));

		// ---------- ConstColorAlphaBlend ----------
		CompileAndRegScript<tTVPOGLRenderMethod_Script_BlendColor>("ConstColorAlphaBlend", fillColorShader, 0)
			->SetBlendFuncSeparate(GL_FUNC_ADD, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_ZERO, GL_ONE);
		TEST_SHADER(ConstColorAlphaBlend,
			TVPConstColorAlphaBlend(testdest, 256 * 256, TEST_SHADER_COLOR, TEST_SHADER_OPA));

			CompileAndRegScript<tTVPOGLRenderMethod_Script_BlendColor>("ConstColorAlphaBlend_a", colorPrefix +
			"void main(){\n"
			"    vec4 s = color;\n"
			"    s.a = 1.0;\n"
			"    gl_FragColor = s;\n"
			"}", 0)
			->SetBlendFuncSeparate(GL_FUNC_ADD, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR);
]=])
    set(yuri_ogl_constant_fill_new [=[
		auto *fill_mask = CompileAndRegScript<tTVPOGLRenderMethod_Script>("FillMask", opacityPrefix +
			"void main(){ vec4 d = texture2D(tex0, v_texCoord0); d.a = opacity; gl_FragColor = d; }", 1);
		fill_mask->SetTargetAsSrc();
		TEST_SHADER(FillMask, TVPFillMask(testdest, 256 * 256, TEST_SHADER_OPA));

		// ---------- ConstColorAlphaBlend ----------
		auto *const_color = CompileAndRegScript<tTVPOGLRenderMethod_Script>("ConstColorAlphaBlend", colorPrefix + opacityPrefix +
			"void main(){ vec4 d = texture2D(tex0, v_texCoord0); d.rgb = mix(d.rgb, color.rgb, opacity); gl_FragColor = d; }", 1);
		const_color->SetTargetAsSrc();
		TEST_SHADER(ConstColorAlphaBlend,
			TVPConstColorAlphaBlend(testdest, 256 * 256, TEST_SHADER_COLOR, TEST_SHADER_OPA));

		auto *const_color_a = CompileAndRegScript<tTVPOGLRenderMethod_Script>("ConstColorAlphaBlend_a", colorPrefix + opacityPrefix +
			"void main(){ vec4 d = texture2D(tex0, v_texCoord0); gl_FragColor = mix(d, vec4(color.rgb, 1.0), opacity); }", 1);
		const_color_a->SetTargetAsSrc();
]=])
    set(yuri_ogl_before_constant_fill "${yuri_ogl_text}")
    string(REPLACE "${yuri_ogl_constant_fill_old}"
        "${yuri_ogl_constant_fill_new}" yuri_ogl_text "${yuri_ogl_text}")
    if(yuri_ogl_text STREQUAL yuri_ogl_before_constant_fill)
        message(FATAL_ERROR "Yuri Vita constant fill shader patch no longer applies")
    endif()

    set(yuri_ogl_const_alpha_old [=[
		CompileAndRegScript<tTVPOGLRenderMethod_Script_BlendColor>("ConstAlphaBlend", copyShader, 1)
			->SetBlendFuncSeparate(GL_FUNC_ADD, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_ZERO, GL_ONE);
]=])
    set(yuri_ogl_const_alpha_new [=[
		auto *const_alpha = CompileAndRegScript<tTVPOGLRenderMethod_Script>("ConstAlphaBlend", opacityPrefix +
			"void main(){ vec4 s = texture2D(tex0, v_texCoord0); vec4 d = texture2D(tex1, v_texCoord1); d.rgb = mix(d.rgb, s.rgb, opacity); gl_FragColor = d; }", 2);
		const_alpha->SetTargetAsSrc();
]=])
    string(REPLACE "${yuri_ogl_const_alpha_old}" "${yuri_ogl_const_alpha_new}"
        yuri_ogl_text "${yuri_ogl_text}")

    set(yuri_ogl_alpha_sd_old [=[
		CompileAndRegScript<tTVPOGLRenderMethod_Script_BlendColor>("AlphaBlend_SD", /*opacityPrefix +*/ ScriptCommonPrefix +
			"    gl_FragColor = s;\n"
			"}", 1)->SetBlendFuncSeparate(GL_FUNC_ADD, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR);
]=])
    set(yuri_ogl_alpha_sd_new [=[
		auto *alpha_sd = CompileAndRegScript<tTVPOGLRenderMethod_Script>("AlphaBlend_SD", opacityPrefix +
			"void main(){ vec4 s = texture2D(tex0, v_texCoord0); vec4 d = texture2D(tex1, v_texCoord1); gl_FragColor = mix(d, s, opacity); }", 2);
		alpha_sd->SetTargetAsSrc();
]=])
    string(REPLACE "${yuri_ogl_alpha_sd_old}" "${yuri_ogl_alpha_sd_new}"
        yuri_ogl_text "${yuri_ogl_text}")

    set(yuri_ogl_add_blend_old [=[
		CompileAndRegScript<tTVPOGLRenderMethod_Script_BlendColor>("AddBlend", copyShader, 1)
			->SetBlendFuncSeparate(GL_FUNC_ADD, GL_CONSTANT_COLOR, GL_ONE, GL_ZERO, GL_ONE);
]=])
    set(yuri_ogl_add_blend_new [=[
		auto *add_blend = CompileAndRegScript<tTVPOGLRenderMethod_Script>("AddBlend", opacityPrefix +
			"void main(){ vec4 s = texture2D(tex0, v_texCoord0); vec4 d = texture2D(tex1, v_texCoord1); d.rgb += s.rgb * opacity; gl_FragColor = d; }", 2);
		add_blend->SetTargetAsSrc();
]=])
    string(REPLACE "${yuri_ogl_add_blend_old}" "${yuri_ogl_add_blend_new}"
        yuri_ogl_text "${yuri_ogl_text}")
    if(yuri_ogl_text MATCHES "GL_CONSTANT_COLOR|GL_ONE_MINUS_CONSTANT_COLOR")
        message(FATAL_ERROR "Unsupported constant-color blend remains in Yuri Vita renderer")
    endif()

    set(yuri_ogl_bridge [=[

extern "C" bool krkrvita_yuri_get_opengl_texture(
	iTVPTexture2D *base, unsigned int *name, int *internal_width,
	int *internal_height, float *scale_width, float *scale_height)
{
	auto *texture = dynamic_cast<tTVPOGLTexture2D *>(base);
	if (!texture || !texture->texture || !name || !internal_width ||
		!internal_height || !scale_width || !scale_height)
		return false;
	TVPSetRenderTarget(0);
	*name = texture->texture;
	*internal_width = static_cast<int>(texture->GetInternalWidth());
	*internal_height = static_cast<int>(texture->GetInternalHeight());
	texture->GetScale(*scale_width, *scale_height);
	return true;
}

]=])
    string(REPLACE
        "REGISTER_RENDERMANAGER(TVPRenderManager_OpenGL, opengl);"
        "${yuri_ogl_bridge}REGISTER_RENDERMANAGER(TVPRenderManager_OpenGL, opengl);"
        yuri_ogl_text "${yuri_ogl_text}")
    if(yuri_ogl_text STREQUAL yuri_ogl_original_text)
        message(FATAL_ERROR "Yuri Vita OpenGL adaptation produced no changes")
    endif()
    file(CONFIGURE OUTPUT "${yuri_generated_dir}/RenderManager_ogl.cpp"
        CONTENT "${yuri_ogl_text}" @ONLY NEWLINE_STYLE UNIX)
    list(APPEND yuri_visual_sources
        "${yuri_generated_dir}/RenderManager_ogl.cpp"
        "${yuri_core}/visual/ogl/etcpak.cpp"
        "${yuri_core}/visual/ogl/pvrtc.cpp")
    endif()

    # Correct two upstream UB defects in generated copies so the pinned Yuri
    # submodule remains an auditable oracle.  The TLG history arrays contain
    # N+M-1 bytes, and AxisParam::toAlign mutates by reference without a value
    # result.
    set(yuri_save_tlg5 "${yuri_core}/visual/SaveTLG5.cpp")
    file(READ "${yuri_save_tlg5}" yuri_save_tlg5_text)
    string(REPLACE
        "i < SLIDE_N + SLIDE_M; i++) Text[i] = 0;"
        "i < SLIDE_N + SLIDE_M - 1; i++) Text[i] = 0;"
        yuri_save_tlg5_patched "${yuri_save_tlg5_text}")
    if(yuri_save_tlg5_patched STREQUAL yuri_save_tlg5_text)
        message(FATAL_ERROR "Yuri SaveTLG5 bounds patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/SaveTLG5.cpp"
        CONTENT "${yuri_save_tlg5_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_save_tlg5}")
    list(APPEND yuri_visual_sources "${yuri_generated_dir}/SaveTLG5.cpp")

    set(yuri_resample_image "${yuri_core}/visual/gl/ResampleImage.cpp")
    file(READ "${yuri_resample_image}" yuri_resample_image_text)
    string(REPLACE
        "static const int toAlign( int& length ) {"
        "static void toAlign( int& length ) {"
        yuri_resample_image_patched "${yuri_resample_image_text}")
    if(yuri_resample_image_patched STREQUAL yuri_resample_image_text)
        message(FATAL_ERROR "Yuri ResampleImage return-type patch no longer applies")
    endif()
    # Yuri's release-only scratch buffers reserve capacity and then index
    # vector[0] while size remains zero. Make the objects real before writing;
    # this also removes optimizer-dependent heap corruption from image scaling.
    string(REPLACE "param.weight_.reserve( length );"
        "param.weight_.resize( length );"
        yuri_resample_image_patched "${yuri_resample_image_patched}")
    string(REPLACE "work.reserve( srcwidth );" "work.resize( srcwidth );"
        yuri_resample_image_patched "${yuri_resample_image_patched}")
    string(REPLACE "work.reserve( width );" "work.resize( width );"
        yuri_resample_image_patched "${yuri_resample_image_patched}")
    string(REPLACE "dstwork.reserve( clip.getDestWidth() );"
        "dstwork.resize( clip.getDestWidth() );"
        yuri_resample_image_patched "${yuri_resample_image_patched}")
    string(REPLACE "dstwork.reserve( param->clip_->getDestWidth() );"
        "dstwork.resize( param->clip_->getDestWidth() );"
        yuri_resample_image_patched "${yuri_resample_image_patched}")
    # iTVPBaseBitmap::GetScanLineForWrite performs copy-on-write bookkeeping.
    # Its current MT path calls that virtual mutator from every worker. Keep
    # resampling serial until it has a main-thread raw-span adapter.
    string(REPLACE "threadNum = TVPGetThreadNum();" "threadNum = 1;"
        yuri_resample_image_patched "${yuri_resample_image_patched}")
    foreach(yuri_resample_contract
        "param.weight_.resize( length );"
        "work.resize( srcwidth );"
        "work.resize( width );"
        "threadNum = 1;")
        string(FIND "${yuri_resample_image_patched}"
            "${yuri_resample_contract}" yuri_resample_contract_offset)
        if(yuri_resample_contract_offset LESS 0)
            message(FATAL_ERROR
                "Yuri safe resampler contract no longer applies: ${yuri_resample_contract}")
        endif()
    endforeach()
    string(REGEX MATCHALL "TVPExecThreadTask\\(threadNum,"
        yuri_resample_dispatch_sites "${yuri_resample_image_patched}")
    list(LENGTH yuri_resample_dispatch_sites
        yuri_resample_dispatch_site_count)
    if(NOT yuri_resample_dispatch_site_count EQUAL 1)
        message(FATAL_ERROR
            "Yuri resampler dispatch-site count changed: ${yuri_resample_dispatch_site_count}")
    endif()
    string(REPLACE "TVPExecThreadTask(threadNum,"
        "TVPExecThreadTaskVita(threadNum,"
        yuri_resample_image_patched "${yuri_resample_image_patched}")
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/ResampleImage.cpp"
        CONTENT "${yuri_resample_image_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_resample_image}")
    list(APPEND yuri_visual_sources "${yuri_generated_dir}/ResampleImage.cpp")

    # The Vita target has no Kirikiroid frontend preferences. Default to the
    # stable Yuri software compositor; VitaGL remains the final presentation
    # layer and the experimental OpenGL compositor can be enabled explicitly.
    set(yuri_render_manager "${yuri_core}/visual/RenderManager.cpp")
    file(READ "${yuri_render_manager}" yuri_render_manager_text)

    string(REPLACE
        "#include \"EventIntf.h\""
        "#include \"EventIntf.h\"\n#include \"krkrvita/deferred_recycle.hpp\"\n#include \"krkrvita/render_task_policy.hpp\"\n#include \"krkrvita/render_task_pool.hpp\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"krkrvita/vita_bitmap_allocator.hpp\"\n#include \"yuri_ms_gothic_stream.hpp\""
        yuri_render_manager_text "${yuri_render_manager_text}")

    # Decide alias safety from the actual CPU backing range, not merely from
    # wrapper-object identity. Yuri can construct multiple texture adapters
    # around the same bitmap. Shifted aliases cross worker row ownership and
    # must retain serial ordering; exact pixelwise aliases remain row-local.
set(yuri_adaptive_thread_helper [=[
static tjs_int GetAdaptiveThreadNum(tjs_int pixelNum, float factor)
{
	if (pixelNum >= factor * 500)
		return TVPGetThreadNum();
	else
		return 1;
}
]=])
    set(yuri_adaptive_thread_helper_vita [=[
static tjs_int GetAdaptiveThreadNum(tjs_int pixelNum, float factor,
	tjs_int rowCount)
{
	return krkrvita::select_adaptive_render_task_count(
		pixelNum, factor, rowCount, TVPGetThreadNum());
}

static bool TVPTextureBackingOverlaps(iTVPTexture2D *left,
	iTVPTexture2D *right)
{
	if(!left || !right) return false;
	const uintptr_t left_begin =
		reinterpret_cast<uintptr_t>(left->GetPixelData());
	const uintptr_t right_begin =
		reinterpret_cast<uintptr_t>(right->GetPixelData());
	if(!left_begin || !right_begin) return false;
	const uintptr_t left_end = left_begin +
		static_cast<uintptr_t>(left->GetPitch()) * left->GetHeight();
	const uintptr_t right_end = right_begin +
		static_cast<uintptr_t>(right->GetPitch()) * right->GetHeight();
	return left_begin < right_end && right_begin < left_end;
}

static bool TVPUnsafeTargetAlias(iTVPTexture2D *target,
	const tTVPRect &target_rect, iTVPTexture2D *input,
	const tTVPRect &input_rect)
{
	if(!TVPTextureBackingOverlaps(target, input)) return false;
	return target->GetPixelData() != input->GetPixelData() ||
		target->GetPitch() != input->GetPitch() ||
		target->GetFormat() != input->GetFormat() ||
		target_rect != input_rect;
}
]=])
    set(yuri_adaptive_thread_helper_input "${yuri_render_manager_text}")
    string(REPLACE "${yuri_adaptive_thread_helper}"
        "${yuri_adaptive_thread_helper_vita}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_adaptive_thread_helper_input)
        message(FATAL_ERROR "Yuri backing-alias helper patch no longer applies")
    endif()
    foreach(yuri_hybrid_render_contract
        "krkrvita/render_task_policy.hpp"
        "krkrvita::select_adaptive_render_task_count("
        "pixelNum, factor, rowCount, TVPGetThreadNum())")
        string(FIND "${yuri_render_manager_text}"
            "${yuri_hybrid_render_contract}" yuri_hybrid_render_offset)
        if(yuri_hybrid_render_offset LESS 0)
            message(FATAL_ERROR
                "Yuri hybrid render-task policy patch no longer applies: ${yuri_hybrid_render_contract}")
        endif()
    endforeach()
    set(yuri_bitmap_reclaim_anchor
        "static uint64_t _totalVMemSize = 0;")
    set(yuri_bitmap_reclaim_impl [=[
static uint64_t _totalVMemSize = 0;

extern "C" uint64_t krkrvita_yuri_recycled_texture_count();

extern "C" void krkrvita_yuri_reclaim_bitmap_memory()
{
	static bool pressure_reported = false;
	if(!pressure_reported) {
		krkrvita_boot_trace("yuri-bitmap-memory-pressure");
		pressure_reported = true;
	}
	// Yuri's own allocation-failure callback delivers the strongest compact
	// event. Complete that contract on Vita by also draining textures whose
	// refcount reached zero during the callbacks before retrying malloc. A
	// texture destructor may enqueue another texture into the next re-entrant-
	// safe recycler batch, so an OOM barrier must continue to a fixed point.
	krkrvita_compact_ms_gothic_pread_cache();
	TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
	const std::size_t recycle_batches =
		krkrvita::drain_deferred_recycle_batches(
			[]() { return krkrvita_yuri_recycled_texture_count(); },
			[]() { iTVPTexture2D::RecycleProcess(); });
	if(recycle_batches > 1)
		krkrvita_boot_trace("yuri-texture-recycler-multibatch-drained");
}
]=])
    string(REPLACE "${yuri_bitmap_reclaim_anchor}"
        "${yuri_bitmap_reclaim_impl}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    set(yuri_bitmap_malloc_old [=[
	ptr = ptrorg = (tjs_uint8*)malloc(allocbytes);
	if (!ptr) TVPThrowExceptionMessage(TVPCannotAllocateBitmapBits,
]=])
    set(yuri_bitmap_malloc_new [=[
	ptr = ptrorg = (tjs_uint8*)krkrvita::vita_bitmap_allocate(allocbytes);
	if(!ptr) {
		krkrvita_yuri_reclaim_bitmap_memory();
		ptr = ptrorg = (tjs_uint8*)krkrvita::vita_bitmap_allocate(allocbytes);
		if(ptr) krkrvita_boot_trace("yuri-bitmap-oom-recovered");
	}
	if (!ptr) TVPThrowExceptionMessage(TVPCannotAllocateBitmapBits,
]=])
    string(REPLACE "${yuri_bitmap_malloc_old}" "${yuri_bitmap_malloc_new}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    set(yuri_bitmap_free_input "${yuri_render_manager_text}")
    string(REPLACE
        "\t\tfree(record->alloc_ptr);"
        "\t\tkrkrvita::vita_bitmap_deallocate(record->alloc_ptr);"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_bitmap_free_input)
        message(FATAL_ERROR "Yuri local bitmap deallocator patch no longer applies")
    endif()
    set(yuri_compressed_texture_base_old
        "class tTVPSoftwareTexture2D_compress : public tTVPSoftwareTexture2D_static, public tTVPContinuousEventCallbackIntf {")
    set(yuri_compressed_texture_base_new
        "class tTVPSoftwareTexture2D_compress : public tTVPSoftwareTexture2D_static, public tTVPContinuousEventCallbackIntf, public tTVPCompactEventCallbackIntf {")
    string(REPLACE "${yuri_compressed_texture_base_old}"
        "${yuri_compressed_texture_base_new}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    set(yuri_compressed_texture_lifetime_old [=[
	~tTVPSoftwareTexture2D_compress() {
		if (BmpData) {
			TVPFreeBitmapBits(BmpData);
			BmpData = nullptr;
			TVPRemoveContinuousEventHook(this);
		}
	}
]=])
    set(yuri_compressed_texture_lifetime_new [=[
	void ReleasePixelData() {
		if(BmpData) {
			TVPFreeBitmapBits(BmpData);
			BmpData = nullptr;
		}
		PixelFrameLife = 0;
		TVPRemoveContinuousEventHook(this);
		TVPRemoveCompactEventHook(this);
	}

	~tTVPSoftwareTexture2D_compress() {
		ReleasePixelData();
	}
]=])
    string(REPLACE "${yuri_compressed_texture_lifetime_old}"
        "${yuri_compressed_texture_lifetime_new}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    set(yuri_compressed_texture_allocate_old [=[
			while (dst < dstend) {
				tjs_uint decodedLines = DecompressLineData(line, dst);
				dst += decodedLines * Pitch;
				line += decodedLines;
			}
		}
]=])
    set(yuri_compressed_texture_allocate_new [=[
			while (dst < dstend) {
				tjs_uint decodedLines = DecompressLineData(line, dst);
				dst += decodedLines * Pitch;
				line += decodedLines;
			}
			TVPAddCompactEventHook(this);
		}
]=])
    string(REPLACE "${yuri_compressed_texture_allocate_old}"
        "${yuri_compressed_texture_allocate_new}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    set(yuri_compressed_texture_callback_old [=[
	virtual void OnContinuousCallback(tjs_uint64 tick) override {
		if (--PixelFrameLife) return;
		if (BmpData) {
			TVPFreeBitmapBits(BmpData);
			BmpData = nullptr;
		}
		PixelFrameLife = 0;
		TVPRemoveContinuousEventHook(this);
	}
]=])
    set(yuri_compressed_texture_callback_new [=[
	virtual void OnContinuousCallback(tjs_uint64 tick) override {
		if (--PixelFrameLife) return;
		ReleasePixelData();
	}

	void TJS_INTF_METHOD OnCompact(tjs_int level) override {
		// GetPixelData sets life to three before a source enters the current
		// render. An allocation retry may deliver compact while preparing the
		// next source; never invalidate an input already prepared for the
		// synchronous worker barrier.
		if(level >= TVP_COMPACT_LEVEL_MINIMIZE && PixelFrameLife < 3)
			ReleasePixelData();
	}
]=])
    string(REPLACE "${yuri_compressed_texture_callback_old}"
        "${yuri_compressed_texture_callback_new}"
        yuri_render_manager_text "${yuri_render_manager_text}")

    # OperateRect materializes every compressed source on the engine thread
    # before it enters a split draw. Worker scanline reads must therefore use
    # that immutable buffer directly instead of racing Yuri's lifetime/event
    # metadata on every row.
    set(yuri_compressed_scanline_old [=[
	virtual const void * GetScanLineForRead(tjs_uint l) override {
		GetPixelData();
		return BmpData + l * Pitch;
	}
]=])
    set(yuri_compressed_scanline_vita [=[
	virtual const void * GetScanLineForRead(tjs_uint l) override {
		if(!krkrvita::RenderTaskPool::in_worker_context()) GetPixelData();
		assert(BmpData);
		return BmpData + l * Pitch;
	}
]=])
    set(yuri_compressed_scanline_input "${yuri_render_manager_text}")
    string(REPLACE "${yuri_compressed_scanline_old}"
        "${yuri_compressed_scanline_vita}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_compressed_scanline_input)
        message(FATAL_ERROR "Yuri compressed worker-read patch no longer applies")
    endif()

    # Job zero always runs on the engine thread and marks a dynamic bitmap
    # non-opaque. Other jobs write disjoint rows and must not concurrently
    # assign that shared metadata byte.
    set(yuri_dynamic_write_old [=[
	virtual void * GetScanLineForWrite(tjs_uint l) {
		Bitmap->IsOpaque = false;
		return (void*)GetScanLineForRead(l);
	}
]=])
    set(yuri_dynamic_write_vita [=[
	virtual void * GetScanLineForWrite(tjs_uint l) {
		if(!krkrvita::RenderTaskPool::in_worker_context())
			Bitmap->IsOpaque = false;
		return (void*)GetScanLineForRead(l);
	}
]=])
    set(yuri_dynamic_write_input "${yuri_render_manager_text}")
    string(REPLACE "${yuri_dynamic_write_old}" "${yuri_dynamic_write_vita}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_dynamic_write_input)
        message(FATAL_ERROR "Yuri dynamic worker-write patch no longer applies")
    endif()

    # Any overlapping copy must preserve row order even when separate wrapper
    # objects expose the same bitmap. Materialize/compare their backing ranges
    # on the owner thread before workers are notified.
    set(yuri_copy_direction_input "${yuri_render_manager_text}")
    string(REPLACE
        "\t\tbool backwardCopy = (_tar == _src && rctar.top > rcsrc.top);"
        "\t\tconst bool backingOverlap = TVPTextureBackingOverlaps(_tar, _src);\n\t\tbool backwardCopy = (backingOverlap && rctar.top > rcsrc.top);"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_copy_direction_input)
        message(FATAL_ERROR "Yuri overlapping copy direction patch no longer applies")
    endif()
    set(yuri_copy_alias_input "${yuri_render_manager_text}")
    string(REPLACE
		"\t\ttjs_int taskNum = GetAdaptiveThreadNum(w * h, THREAD_FACTOR);\n\t\tTVPExecThreadTask(taskNum, [=](int i){\n\t\t\ttjs_int y0, y1;\n\t\t\ty0 = h * i / taskNum;\n\t\t\ty1 = h * (i + 1) / taskNum;\n\t\t\tthis->PartialCopy("
		"\t\ttjs_int taskNum = backingOverlap ? 1 : GetAdaptiveThreadNum(w * h, THREAD_FACTOR);\n\t\tTVPExecThreadTask(taskNum, [=](int i){\n\t\t\ttjs_int y0, y1;\n\t\t\ty0 = h * i / taskNum;\n\t\t\ty1 = h * (i + 1) / taskNum;\n\t\t\tthis->PartialCopy("
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_copy_alias_input)
        message(FATAL_ERROR "Yuri overlapping copy serialization patch no longer applies")
    endif()
    set(yuri_direct_copy_alias_input "${yuri_render_manager_text}")
    string(REPLACE
        "\t\t\ttjs_int taskNum = _src == _tar ? 1 : GetAdaptiveThreadNum(w * h, 66);"
        "\t\t\ttjs_int taskNum = backingOverlap ? 1 : GetAdaptiveThreadNum(w * h, 66);"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_direct_copy_alias_input)
        message(FATAL_ERROR "Yuri direct-copy alias patch no longer applies")
    endif()

    # Row-split blends are safe only when each input either has distinct
    # backing storage or aliases the target at the exact same pixels.
    set(yuri_base_blt_alias_input "${yuri_render_manager_text}")
    string(REPLACE
        "\t\ttjs_int sx = rcsrc.left, dx = rctar.left, sy = rcsrc.top, dy = rctar.top;\n\n\t\ttjs_int taskNum = GetAdaptiveThreadNum(w * h, THREAD_FACTOR);"
        "\t\ttjs_int sx = rcsrc.left, dx = rctar.left, sy = rcsrc.top, dy = rctar.top;\n\n\t\tconst bool unsafeAlias = TVPUnsafeTargetAlias(_tar, rctar, _src, rcsrc);\n\t\ttjs_int taskNum = unsafeAlias ? 1 : GetAdaptiveThreadNum(w * h, THREAD_FACTOR);"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_base_blt_alias_input)
        message(FATAL_ERROR "Yuri BaseBlt alias gate no longer applies")
    endif()
    set(yuri_trans_blt_alias_input "${yuri_render_manager_text}")
    string(REPLACE
        "// \t\ttar = (tjs_uint8*)_tar->GetScanLineForWrite(rctar.top) + rctar.left * sizeof(tjs_uint32);\n\n\t\ttjs_int taskNum = GetAdaptiveThreadNum(w * h, THREAD_FACTOR);"
        "// \t\ttar = (tjs_uint8*)_tar->GetScanLineForWrite(rctar.top) + rctar.left * sizeof(tjs_uint32);\n\n\t\tconst bool unsafeAlias =\n\t\t\tTVPUnsafeTargetAlias(_tar, rctar, _src, rcsrc) ||\n\t\t\tTVPUnsafeTargetAlias(_tar, rctar, _dst, rcdst);\n\t\ttjs_int taskNum = unsafeAlias ? 1 : GetAdaptiveThreadNum(w * h, THREAD_FACTOR);"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_trans_blt_alias_input)
        message(FATAL_ERROR "Yuri TransBlt alias gate no longer applies")
    endif()
    set(yuri_univ_blt_alias_input "${yuri_render_manager_text}")
    string(REPLACE
        "// \t\trule = (tjs_uint8*)_rule->GetScanLineForRead(rcrule.top) + rcrule.left * sizeof(tjs_uint8);\n\n\t\ttjs_int taskNum = GetAdaptiveThreadNum(w * h, THREAD_FACTOR);"
        "// \t\trule = (tjs_uint8*)_rule->GetScanLineForRead(rcrule.top) + rcrule.left * sizeof(tjs_uint8);\n\n\t\tconst bool unsafeAlias =\n\t\t\tTVPUnsafeTargetAlias(_tar, rctar, _src, rcsrc) ||\n\t\t\tTVPUnsafeTargetAlias(_tar, rctar, _dst, rcdst) ||\n\t\t\tTVPUnsafeTargetAlias(_tar, rctar, _rule, rcrule);\n\t\ttjs_int taskNum = unsafeAlias ? 1 : GetAdaptiveThreadNum(w * h, THREAD_FACTOR);"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_univ_blt_alias_input)
        message(FATAL_ERROR "Yuri UnivTransBlt alias gate no longer applies")
    endif()

    # Every active adaptive primitive partitions a rectangle into horizontal
    # jobs. Never request more jobs than rows: that would create empty tasks
    # and does not preserve the intended one-or-more rows per worker contract.
    string(REPLACE "GetAdaptiveThreadNum(w * h, 150)"
        "GetAdaptiveThreadNum(w * h, 150, h)"
        yuri_render_manager_text "${yuri_render_manager_text}")
    string(REPLACE "GetAdaptiveThreadNum(w * h, THREAD_FACTOR)"
        "GetAdaptiveThreadNum(w * h, THREAD_FACTOR, h)"
        yuri_render_manager_text "${yuri_render_manager_text}")
    string(REPLACE "GetAdaptiveThreadNum(w * h, 66)"
        "GetAdaptiveThreadNum(w * h, 66, h)"
        yuri_render_manager_text "${yuri_render_manager_text}")
    string(REGEX MATCHALL
        "GetAdaptiveThreadNum\\(w \\* h, (150|THREAD_FACTOR|66), h\\)"
        yuri_height_clamped_render_sites "${yuri_render_manager_text}")
    list(LENGTH yuri_height_clamped_render_sites
        yuri_height_clamped_render_site_count)
    if(NOT yuri_height_clamped_render_site_count EQUAL 8)
        message(FATAL_ERROR
            "Yuri adaptive height-clamped render-site count changed: ${yuri_height_clamped_render_site_count}")
    endif()
    set(yuri_triangle_serial_input "${yuri_render_manager_text}")
    string(REPLACE
        "\t\t\ttjs_int taskNum = TVPGetThreadNum();\n\t\t\tif (taskNum > nTriangles) taskNum = nTriangles;"
        "\t\t\ttjs_int taskNum = 1; // overlapping triangles are order-dependent"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_triangle_serial_input)
        message(FATAL_ERROR "Yuri triangle serialization patch no longer applies")
    endif()

    # Only the exact registered Copy method ignores the previous destination.
    # Let LayerBitmapImpl use its no-copy COW preparation for that method while
    # preserving blend-target semantics for DirectCopy-derived filters such as
    # DoBoxBlur.
    set(yuri_direct_copy_target_input "${yuri_render_manager_text}")
    string(REPLACE
        "class tTVPRenderMethod_DirectCopy : public tTVPRenderMethod_Software {\npublic:"
        "class tTVPRenderMethod_DirectCopy : public tTVPRenderMethod_Software {\npublic:\n\tvirtual bool IsBlendTarget() override { return GetName() != \"Copy\"; }"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_direct_copy_target_input)
        message(FATAL_ERROR "Yuri exact-Copy blend-target patch no longer applies")
    endif()

    # Reuse box-filter column sums across frames. The rolling horizontal and
    # vertical implementation in the Vita OpenCV compatibility header is
    # O(width*height*channels), independent of kernel area.
    set(yuri_box_workspace_input "${yuri_render_manager_text}")
    string(REPLACE
        "class tTVPRenderMethod_DoBoxBlur : public tTVPRenderMethod_DirectCopy {\n\ttTVPRect area;"
        "class tTVPRenderMethod_DoBoxBlur : public tTVPRenderMethod_DirectCopy {\n\ttTVPRect area;\n\tcv::BoxFilterWorkspace Workspace;"
        yuri_render_manager_text "${yuri_render_manager_text}")
    string(REPLACE
        "\t\tcv::boxFilter(src_img, dst_img, -1, areasize);"
        "\t\tcv::boxFilter(src_img, dst_img, -1, areasize, Workspace);"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_box_workspace_input)
        message(FATAL_ERROR "Yuri persistent box-filter workspace patch no longer applies")
    endif()

    # Scaling coefficients and per-lane row scratch are persistent manager
    # state. Size every active lane on the owner thread before dispatch so a
    # first-use or geometry-growth frame never allocates inside Vita workers.
    set(yuri_resize_workspace_input "${yuri_render_manager_text}")
    string(REPLACE
        "\tiTVPTexture2D *tempTexture;"
        "\tiTVPTexture2D *tempTexture;\n\tcv::ResizeWorkspace ResizeWorkspace;\n\tstd::array<cv::ResizeTaskWorkspace, TVPMaxThreadNum> ResizeTaskWorkspaces;\n\tcv::WarpPerspectivePlan PerspectivePlan;\n\tcv::WarpAffinePlan AffinePlan;"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_resize_workspace_input)
        message(FATAL_ERROR "Yuri persistent OpenCV workspace patch no longer applies")
    endif()

    set(yuri_resize_rect_old [=[
	void OperateRect(iTVPRenderMethod* method,
		iTVPTexture2D *tar, tTVPRect rctar,
		iTVPTexture2D *src, tTVPRect rcsrc) {
		int sw = rcsrc.get_width(), sh = rcsrc.get_height(),
			dw = rctar.get_width(), dh = rctar.get_height();
		if (dw == 0 || dh == 0 || sw == 0 || sh == 0) return;
		if (sw > 0 && sh > 0 && (sw != dw || sh != dh)) {
			tTVPRect cr(0, 0, tar->GetWidth(), tar->GetHeight());
			if (cr.left > rctar.left) {
				rcsrc.left += (float)sw / dw * (cr.left - rctar.left);
				rctar.left = cr.left;
			}
			if (cr.right < rctar.right) {
				rcsrc.right -= (float)rcsrc.get_width() / rctar.get_width() * (rctar.right - cr.right);
				rctar.right = cr.right;
			}
			if (cr.top > rctar.top) {
				rcsrc.top += (float)sh / dh * (cr.top - rctar.top);
				rctar.top = cr.top;
			}
			if (cr.bottom < rctar.bottom) {
				rcsrc.bottom -= (float)rcsrc.get_height() / rctar.get_height() * (rctar.bottom - cr.bottom);
				rctar.bottom = cr.bottom;
			}
			sw = rcsrc.get_width(); sh = rcsrc.get_height();
			dw = rctar.get_width(); dh = rctar.get_height();
			if (sh == 0 || sw == 0 || dh == 0 || dw == 0) {
				return;
			}

			const uint8_t *sdata;
			int spitch = src->GetPitch();
			sdata = (const uint8_t *)src->GetPixelData() + (rcsrc.top * spitch + rcsrc.left * 4);

			iTVPTexture2D *tmp = getTempTexture(dw, dh + 1);
			uint8_t *ddata = (uint8_t *)tmp->GetScanLineForWrite(0);
			int dpitch = tmp->GetPitch();
// #ifdef _DEBUG
// 			printf("resize (%d, %d) -> (%d, %d)\n", sw, sh, dw, dh);
// #endif
#ifdef USE_SWSCALE
			static int swsFlags[4] = {
				SWS_POINT, // stNearest
				SWS_FAST_BILINEAR, // stFastLinear
				SWS_BILINEAR, // stLinear
				SWS_BICUBIC, // stCubic
			};
			//assert(StretchType < sizeof(swsFlags) / sizeof(swsFlags[0]));
			img_convert_ctx = sws_getCachedContext(img_convert_ctx,
				sw, sh, AV_PIX_FMT_RGBA, dw, dh, AV_PIX_FMT_RGBA,
				swsFlags[StretchType], nullptr, nullptr, nullptr);
			//assert(img_convert_ctx);
			// TODO multithreaded
			sws_scale(img_convert_ctx, &sdata, &spitch, 0, sh, &ddata, &dpitch);
#else
			cv::Size dsize(dw, dh);
			cv::Mat src_img(sh, sw, CV_8UC4, (void*)sdata, spitch);
			cv::Mat dst_img(dh, dw, CV_8UC4, (void*)ddata, dpitch);
			cv::resize(src_img, dst_img, dsize, 0, 0, cvFlags[StretchType]);
#endif
			tTVPRect rc(0, 0, dw, dh);
			((tTVPRenderMethod_Software*)method)->DoRender(
				tar, rctar,
				tar, rctar,
				tmp, rc,
				nullptr, rc);
		} else {
			((tTVPRenderMethod_Software*)method)->DoRender(
				tar, rctar,
				tar, rctar,
				src, rcsrc,
				nullptr, rcsrc);
		}
	}
]=])
    set(yuri_resize_rect_vita [=[
	void OperateRect(iTVPRenderMethod* method,
		iTVPTexture2D *tar, tTVPRect rctar,
		iTVPTexture2D *src, tTVPRect rcsrc) {
		int sw = rcsrc.get_width(), sh = rcsrc.get_height(),
			dw = rctar.get_width(), dh = rctar.get_height();
		if (dw == 0 || dh == 0 || sw == 0 || sh == 0) return;
		if (sw > 0 && sh > 0 && (sw != dw || sh != dh)) {
			tTVPRect cr(0, 0, tar->GetWidth(), tar->GetHeight());
			if (cr.left > rctar.left) {
				rcsrc.left += (float)sw / dw * (cr.left - rctar.left);
				rctar.left = cr.left;
			}
			if (cr.right < rctar.right) {
				rcsrc.right -= (float)rcsrc.get_width() / rctar.get_width() * (rctar.right - cr.right);
				rctar.right = cr.right;
			}
			if (cr.top > rctar.top) {
				rcsrc.top += (float)sh / dh * (cr.top - rctar.top);
				rctar.top = cr.top;
			}
			if (cr.bottom < rctar.bottom) {
				rcsrc.bottom -= (float)rcsrc.get_height() / rctar.get_height() * (rctar.bottom - cr.bottom);
				rctar.bottom = cr.bottom;
			}
			sw = rcsrc.get_width(); sh = rcsrc.get_height();
			dw = rctar.get_width(); dh = rctar.get_height();
			if (sh == 0 || sw == 0 || dh == 0 || dw == 0) return;

			const int spitch = src->GetPitch();
			const uint8_t *sdata =
				(const uint8_t *)src->GetPixelData() +
				(rcsrc.top * spitch + rcsrc.left * 4);
			const bool directCopy = method->GetName() == "Copy" &&
				tar->GetFormat() == TVPTextureFormat::RGBA &&
				src->GetFormat() == TVPTextureFormat::RGBA &&
				!TVPTextureBackingOverlaps(tar, src);
			iTVPTexture2D *tmp = directCopy ? nullptr :
				getTempTexture(dw, dh + 1);
			uint8_t *ddata = directCopy
				? (uint8_t *)tar->GetScanLineForWrite(rctar.top) +
					rctar.left * 4
				: (uint8_t *)tmp->GetScanLineForWrite(0);
			const int dpitch = directCopy ? tar->GetPitch() : tmp->GetPitch();

			cv::Size dsize(dw, dh);
			cv::Mat src_img(sh, sw, CV_8UC4, (void*)sdata, spitch);
			cv::Mat dst_img(dh, dw, CV_8UC4, (void*)ddata, dpitch);
			const int interpolation = cvFlags[StretchType];
			cv::prepareResizeWorkspace(
				src_img, dsize, interpolation, ResizeWorkspace);
			const tjs_int taskNum =
				GetAdaptiveThreadNum(dw * dh, 52, dh);
			for(tjs_int lane = 0; lane < taskNum; ++lane)
				cv::prepareResizeTaskWorkspace(
					src_img, dsize, interpolation,
					ResizeTaskWorkspaces[lane]);
			TVPExecThreadTaskVita(taskNum, [&](int lane) {
				const int rowBegin = dh * lane / taskNum;
				const int rowEnd = dh * (lane + 1) / taskNum;
				cv::resizeRows(src_img, dst_img, dsize, interpolation,
					ResizeWorkspace, ResizeTaskWorkspaces[lane],
					rowBegin, rowEnd);
			});

			if(!directCopy) {
				tTVPRect rc(0, 0, dw, dh);
				((tTVPRenderMethod_Software*)method)->DoRender(
					tar, rctar,
					tar, rctar,
					tmp, rc,
					nullptr, rc);
			}
		} else {
			((tTVPRenderMethod_Software*)method)->DoRender(
				tar, rctar,
				tar, rctar,
				src, rcsrc,
				nullptr, rcsrc);
		}
	}
]=])
    set(yuri_resize_rect_input "${yuri_render_manager_text}")
    string(REPLACE "${yuri_resize_rect_old}" "${yuri_resize_rect_vita}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_resize_rect_input)
        message(FATAL_ERROR "Yuri allocation-stable resize patch no longer applies")
    endif()

    set(yuri_triangle_destination_old [=[
			cv::Mat dst_img;
			cv::Size dst_size(rcclip.get_width(), rcclip.get_height());
]=])
    set(yuri_triangle_destination_vita [=[
			cv::Size dst_size(rcclip.get_width(), rcclip.get_height());
			const bool directCopy = method->GetName() == "Copy" &&
				target->GetFormat() == TVPTextureFormat::RGBA &&
				src->GetFormat() == TVPTextureFormat::RGBA &&
				!TVPTextureBackingOverlaps(target, src);
			iTVPTexture2D *tmp = directCopy ? nullptr :
				getTempTexture(dst_size.width, dst_size.height + 1);
			uint8_t *warpData = directCopy
				? (uint8_t *)target->GetScanLineForWrite(rcclip.top) +
					rcclip.left * 4
				: (uint8_t *)tmp->GetScanLineForWrite(0);
			const int warpPitch = directCopy ? target->GetPitch() :
				tmp->GetPitch();
			cv::Mat dst_img(dst_size.height, dst_size.width, CV_8UC4,
				warpData, warpPitch);
]=])
    set(yuri_triangle_destination_input "${yuri_render_manager_text}")
    string(REPLACE "${yuri_triangle_destination_old}"
        "${yuri_triangle_destination_vita}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_triangle_destination_input)
        message(FATAL_ERROR "Yuri triangle direct warp destination patch no longer applies")
    endif()

    set(yuri_perspective_destination_old [=[
				cv::Mat dst_img;
				cv::Size dst_size(rcclip.get_width(), rcclip.get_height());
]=])
    set(yuri_perspective_destination_vita [=[
				cv::Size dst_size(rcclip.get_width(), rcclip.get_height());
				const bool directCopy = method->GetName() == "Copy" &&
					target->GetFormat() == TVPTextureFormat::RGBA &&
					src->GetFormat() == TVPTextureFormat::RGBA &&
					!TVPTextureBackingOverlaps(target, src);
				iTVPTexture2D *tmp = directCopy ? nullptr :
					getTempTexture(dst_size.width, dst_size.height + 1);
				uint8_t *warpData = directCopy
					? (uint8_t *)target->GetScanLineForWrite(rcclip.top) +
						rcclip.left * 4
					: (uint8_t *)tmp->GetScanLineForWrite(0);
				const int warpPitch = directCopy ? target->GetPitch() :
					tmp->GetPitch();
				cv::Mat dst_img(dst_size.height, dst_size.width, CV_8UC4,
					warpData, warpPitch);
]=])
    set(yuri_perspective_destination_input "${yuri_render_manager_text}")
    string(REPLACE "${yuri_perspective_destination_old}"
        "${yuri_perspective_destination_vita}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_perspective_destination_input)
        message(FATAL_ERROR "Yuri perspective direct warp destination patch no longer applies")
    endif()

    set(yuri_affine_call_old
        "\t\t\t\tcv::warpAffine(src_img, dst_img, affine_matrix, dst_size, cvFlags[StretchType]);")
    set(yuri_affine_call_vita [=[
				const int interpolation = cvFlags[StretchType];
				if(interpolation == cv::INTER_CUBIC)
					(void)cv::warp_cubic_table();
				const bool validWarp =
					cv::prepareWarpAffine(affine_matrix, dst_size, AffinePlan);
				const tjs_int taskNum = GetAdaptiveThreadNum(
					dst_size.width * dst_size.height, 52, dst_size.height);
				TVPExecThreadTaskVita(taskNum, [&](int lane) {
					const int rowBegin = dst_size.height * lane / taskNum;
					const int rowEnd = dst_size.height * (lane + 1) / taskNum;
					if(validWarp)
						cv::warpAffineRows(src_img, dst_img, AffinePlan,
							dst_size, interpolation, rowBegin, rowEnd);
					else
						for(int row = rowBegin; row < rowEnd; ++row)
							std::memset(dst_img.ptr(row), 0,
								dst_size.width * 4);
				});
]=])
    set(yuri_affine_call_input "${yuri_render_manager_text}")
    string(REPLACE "${yuri_affine_call_old}" "${yuri_affine_call_vita}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_affine_call_input)
        message(FATAL_ERROR "Yuri row-parallel affine patch no longer applies")
    endif()

    set(yuri_perspective_call_old
        "cv::warpPerspective(src_img, dst_img, perspective_matrix, dst_size, cvFlags[StretchType]);")
    set(yuri_perspective_call_vita [=[
const int interpolation = cvFlags[StretchType];
				if(interpolation == cv::INTER_CUBIC)
					(void)cv::warp_cubic_table();
				const bool validWarp = cv::prepareWarpPerspective(
					perspective_matrix, PerspectivePlan);
				const tjs_int taskNum = GetAdaptiveThreadNum(
					dst_size.width * dst_size.height, 52, dst_size.height);
				TVPExecThreadTaskVita(taskNum, [&](int lane) {
					const int rowBegin = dst_size.height * lane / taskNum;
					const int rowEnd = dst_size.height * (lane + 1) / taskNum;
					if(validWarp)
						cv::warpPerspectiveRows(src_img, dst_img,
							PerspectivePlan, dst_size, interpolation,
							rowBegin, rowEnd);
					else
						for(int row = rowBegin; row < rowEnd; ++row)
							std::memset(dst_img.ptr(row), 0,
								dst_size.width * 4);
				});
]=])
    string(REGEX MATCHALL
        "cv::warpPerspective\\(src_img, dst_img, perspective_matrix, dst_size, cvFlags\\[StretchType\\]\\)"
        yuri_perspective_call_sites "${yuri_render_manager_text}")
    list(LENGTH yuri_perspective_call_sites yuri_perspective_call_count)
    if(NOT yuri_perspective_call_count EQUAL 2)
        message(FATAL_ERROR
            "Yuri perspective warp call count changed: ${yuri_perspective_call_count}")
    endif()
    string(REPLACE "${yuri_perspective_call_old}"
        "${yuri_perspective_call_vita}"
        yuri_render_manager_text "${yuri_render_manager_text}")

    set(yuri_triangle_warp_copy_old [=[
			iTVPTexture2D *tmp = new tTVPSoftwareTexture2D_static(dst_img.ptr(0), dst_img.step1(0), dst_size.width, dst_size.height, TVPTextureFormat::RGBA);
			tTVPRect rc(0, 0, dst_size.width, dst_size.height);
// 			if (rc.right > dst->GetWidth()) rc.right = dst->GetWidth();
// 			if (rc.bottom > dst->GetHeight()) rc.bottom = dst->GetHeight();

			((tTVPRenderMethod_Software*)method)->DoRender(
				target, rcclip,
				target, rcclip,
				tmp, rc,
				nullptr, rc);
			tmp->Release();
]=])
    set(yuri_triangle_warp_copy_vita [=[
			if(!directCopy) {
				tTVPRect rc(0, 0, dst_size.width, dst_size.height);
				((tTVPRenderMethod_Software*)method)->DoRender(
					target, rcclip,
					target, rcclip,
					tmp, rc,
					nullptr, rc);
			}
]=])
    set(yuri_triangle_warp_copy_input "${yuri_render_manager_text}")
    string(REPLACE "${yuri_triangle_warp_copy_old}"
        "${yuri_triangle_warp_copy_vita}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_triangle_warp_copy_input)
        message(FATAL_ERROR "Yuri triangle persistent warp texture patch no longer applies")
    endif()

    set(yuri_perspective_warp_copy_old [=[
				iTVPTexture2D *tmp = new tTVPSoftwareTexture2D_static(dst_img.ptr(0), dst_img.step1(0), dst_size.width, dst_size.height, TVPTextureFormat::RGBA);
				tTVPRect rc(0, 0, dst_size.width, dst_size.height);

				((tTVPRenderMethod_Software*)method)->DoRender(
					target, rcclip,
					target, rcclip,
					tmp, rc,
					nullptr, rc);
				tmp->Release();
]=])
    set(yuri_perspective_warp_copy_vita [=[
				if(!directCopy) {
					tTVPRect rc(0, 0, dst_size.width, dst_size.height);
					((tTVPRenderMethod_Software*)method)->DoRender(
						target, rcclip,
						target, rcclip,
						tmp, rc,
						nullptr, rc);
				}
]=])
    set(yuri_perspective_warp_copy_input "${yuri_render_manager_text}")
    string(REPLACE "${yuri_perspective_warp_copy_old}"
        "${yuri_perspective_warp_copy_vita}"
        yuri_render_manager_text "${yuri_render_manager_text}")
    if(yuri_render_manager_text STREQUAL yuri_perspective_warp_copy_input)
        message(FATAL_ERROR "Yuri perspective persistent warp texture patch no longer applies")
    endif()

    foreach(yuri_opencv_render_contract
        "GetName() != \"Copy\""
        "cv::BoxFilterWorkspace Workspace"
        "cv::prepareResizeWorkspace("
        "cv::prepareResizeTaskWorkspace("
        "cv::resizeRows("
        "cv::prepareWarpAffine("
        "cv::warpAffineRows("
        "cv::prepareWarpPerspective("
        "cv::warpPerspectiveRows("
        "!TVPTextureBackingOverlaps(tar, src)"
        "!TVPTextureBackingOverlaps(target, src)")
        string(FIND "${yuri_render_manager_text}"
            "${yuri_opencv_render_contract}"
            yuri_opencv_render_contract_offset)
        if(yuri_opencv_render_contract_offset LESS 0)
            message(FATAL_ERROR
                "Yuri OpenCV fast-path contract no longer applies: ${yuri_opencv_render_contract}")
        endif()
    endforeach()
    foreach(yuri_removed_opencv_pattern
        "cv::resize(src_img, dst_img"
        "new tTVPSoftwareTexture2D_static(dst_img.ptr(0)")
        string(FIND "${yuri_render_manager_text}"
            "${yuri_removed_opencv_pattern}"
            yuri_removed_opencv_offset)
        if(NOT yuri_removed_opencv_offset LESS 0)
            message(FATAL_ERROR
                "Yuri obsolete OpenCV path remains: ${yuri_removed_opencv_pattern}")
        endif()
    endforeach()

    foreach(yuri_memory_contract
        "krkrvita_yuri_reclaim_bitmap_memory"
        "yuri-bitmap-memory-pressure"
        "yuri-bitmap-oom-recovered"
        "krkrvita::vita_bitmap_allocate(allocbytes)"
        "krkrvita::vita_bitmap_deallocate(record->alloc_ptr)"
        "public tTVPCompactEventCallbackIntf"
        "TVPAddCompactEventHook(this)"
        "PixelFrameLife < 3")
        string(FIND "${yuri_render_manager_text}" "${yuri_memory_contract}"
            yuri_memory_contract_offset)
        if(yuri_memory_contract_offset LESS 0)
            message(FATAL_ERROR
                "Yuri software bitmap memory contract no longer applies: ${yuri_memory_contract}")
        endif()
    endforeach()
    set(yuri_render_config_block
        "\t\t_createStaticTexture2D = tTVPSoftwareTexture2D::Create;\n\t\tstd::string compTexMethod = IndividualConfigManager::GetInstance()->GetValue<std::string>(\"software_compress_tex\", \"none\");\n\t\tif (compTexMethod == \"halfline\") _createStaticTexture2D = tTVPSoftwareTexture2D_half::Create;\n\t\telse if (compTexMethod == \"lz4\") _createStaticTexture2D = tTVPSoftwareTexture2D_lz4::Create;\n\t\telse if (compTexMethod == \"lz4+tlg5\") _createStaticTexture2D = tTVPSoftwareTexture2D_lz4_tlg5::Create;")
    if(KRKRVITA_ENABLE_OPENGL_COMPOSITOR)
        set(yuri_render_vita_block
            "\t\t_createStaticTexture2D = tTVPSoftwareTexture2D::Create;")
    else()
        set(yuri_render_vita_block
            "\t\t_createStaticTexture2D = tTVPSoftwareTexture2D::Create;\n\t\tkrkrvita_boot_trace(\"yuri-software-static-texture-direct-ready\");\n\t\tkrkrvita_boot_trace(\"yuri-opencv-software-fastpaths-ready\");")
    endif()
    string(REPLACE "${yuri_render_config_block}" "${yuri_render_vita_block}"
        yuri_render_manager_patched "${yuri_render_manager_text}")
    if(yuri_render_manager_patched STREQUAL yuri_render_manager_text)
        message(FATAL_ERROR "Yuri software texture policy patch no longer applies")
    endif()
    set(yuri_renderer_config_block
        "\t\tttstr str = IndividualConfigManager::GetInstance()->GetValue<std::string>(\"renderer\", \"software\");\n\t\t_RenderManager = TVPGetRenderManager(str);")
    if(KRKRVITA_ENABLE_OPENGL_COMPOSITOR)
        set(yuri_renderer_vita_block
            "\t\t_RenderManager = TVPGetRenderManager(TJS_W(\"opengl\"));")
    else()
        set(yuri_renderer_vita_block
            "\t\t_RenderManager = TVPGetRenderManager(TJS_W(\"software\"));")
    endif()
    string(REPLACE "${yuri_renderer_config_block}" "${yuri_renderer_vita_block}"
        yuri_render_manager_patched_2 "${yuri_render_manager_patched}")
    if(yuri_render_manager_patched_2 STREQUAL yuri_render_manager_patched)
        message(FATAL_ERROR "Yuri Vita renderer selection patch no longer applies")
    endif()

    # iTVPTexture2D::Release intentionally defers destruction until the end of
    # a rendered frame.  Yuri's Cocos and desktop frame loops call
    # RecycleProcess() for that purpose; without a frontend, the Vita backend
    # owns the same frame-boundary contract.  Keep deletion re-entrant-safe: a
    # texture destructor may release another texture, which belongs to the
    # next recycle batch rather than the vector currently being traversed.
    set(yuri_texture_recycler_old [=[
static std::vector<iTVPTexture2D*> _toDeleteTextures;

void iTVPTexture2D::RecycleProcess()
{
	for (iTVPTexture2D* tex : _toDeleteTextures) {
		delete tex;
	}
	_toDeleteTextures.clear();
}
]=])
    set(yuri_texture_recycler_vita [=[
static std::vector<iTVPTexture2D*> _toDeleteTextures;
static uint64_t _recycledTextureCount = 0;

extern "C" void krkrvita_boot_trace(const char *stage);

// Deferred texture destruction, with the lifetime invariant made sound.
//
// Upstream Release() queues a texture when RefCount == 1 but leaves RefCount
// at 1, and RecycleProcess() then deletes every queued texture unconditionally.
// That leaves two ways for one heap block to end up with two live owners:
//
//   - Release() called again on an already-queued texture still sees
//     RefCount == 1, so the texture is queued a second time and later deleted
//     twice.
//   - A queued texture still reports RefCount == 1, so IsIndependent() treats
//     it as exclusively owned and AddRef() succeeds. Anything that acquires it
//     between the queueing and the drain is left holding freed memory.
//
// Either one lets a freshly created texture land on top of a live one. That
// matters here because tTVPLayerManager clears its draw buffer to 0xFF000000
// (LayerManager.cpp:102, :112, :139, :149, :163), and a layer bitmap sharing
// that block would read opaque black -- the observed retail symptom.
//
// The fix is to make "queued" a distinct state: RefCount drops to 0 exactly
// once, so a texture cannot be queued twice, and RecycleProcess() refuses to
// delete anything that was acquired again after queueing. IsIndependent()
// reports false for RefCount == 0, so a dead texture is never mistaken for an
// exclusively owned one and copy-on-write stays conservative.
void iTVPTexture2D::RecycleProcess()
{
	std::vector<iTVPTexture2D*> pending;
	pending.swap(_toDeleteTextures);
	for (iTVPTexture2D* tex : pending) {
		if (tex->RefCount > 0) {
			// Acquired again between queueing and this drain. Deleting it here
			// is the use-after-free; keep it, and let its final Release() queue
			// it again.
			static bool resurrected_reported = false;
			if (!resurrected_reported) {
				krkrvita_boot_trace("yuri-texture-resurrected-after-queue");
				resurrected_reported = true;
			}
			continue;
		}
		delete tex;
		++_recycledTextureCount;
	}
}

extern "C" uint64_t krkrvita_yuri_recycled_texture_count()
{
	return _recycledTextureCount;
}
]=])
    string(REPLACE "${yuri_texture_recycler_old}"
        "${yuri_texture_recycler_vita}"
        yuri_render_manager_patched_3 "${yuri_render_manager_patched_2}")
    if(yuri_render_manager_patched_3 STREQUAL yuri_render_manager_patched_2)
        message(FATAL_ERROR "Yuri frame-boundary texture recycler patch no longer applies")
    endif()

    # Queue a texture exactly once. Leaving RefCount at 1 while queued is what
    # makes a second Release() queue it again, and a double delete hands one
    # heap block to two live objects.
    set(yuri_texture_release_old [=[
void iTVPTexture2D::Release() {
	if (RefCount == 1)
		_toDeleteTextures.push_back(this);
	else
		--RefCount;
}
]=])
    set(yuri_texture_release_vita [=[
void iTVPTexture2D::Release() {
	if (RefCount <= 0) {
		// Already queued for destruction. Releasing again would queue it a
		// second time and delete it twice.
		static bool double_release_reported = false;
		if (!double_release_reported) {
			krkrvita_boot_trace("yuri-texture-double-release");
			double_release_reported = true;
		}
		return;
	}
	if (RefCount == 1) {
		RefCount = 0;
		_toDeleteTextures.push_back(this);
		return;
	}
	--RefCount;
}
]=])
    string(REPLACE "${yuri_texture_release_old}"
        "${yuri_texture_release_vita}"
        yuri_render_manager_patched_3b "${yuri_render_manager_patched_3}")
    if(yuri_render_manager_patched_3b STREQUAL yuri_render_manager_patched_3)
        message(FATAL_ERROR "Yuri texture release invariant patch no longer applies")
    endif()
    set(yuri_render_manager_patched_3 "${yuri_render_manager_patched_3b}")
    string(REGEX MATCHALL "TVPExecThreadTask\\(taskNum,"
        yuri_render_dispatch_sites "${yuri_render_manager_patched_3}")
    list(LENGTH yuri_render_dispatch_sites yuri_render_dispatch_site_count)
    if(NOT yuri_render_dispatch_site_count EQUAL 9)
        message(FATAL_ERROR
            "Yuri RenderManager dispatch-site count changed: ${yuri_render_dispatch_site_count}")
    endif()
    string(REPLACE "TVPExecThreadTask(taskNum,"
        "TVPExecThreadTaskVita(taskNum,"
        yuri_render_manager_patched_4 "${yuri_render_manager_patched_3}")

    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/RenderManager.cpp"
        CONTENT "${yuri_render_manager_patched_4}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_render_manager}")
    list(APPEND yuri_visual_sources
        "${yuri_generated_dir}/RenderManager.cpp")

    # Preserve Yuri's FreeType registry for game-supplied fonts and register the
    # personal-use MS Gothic collection at its fixed Vita storage path. This
    # build requires face zero of that collection: silently substituting a
    # firmware face changes retail layout and hides a failed font deployment.
    set(yuri_font_impl "${yuri_core}/visual/FontImpl.cpp")
    file(READ "${yuri_font_impl}" yuri_font_impl_text)
    string(REPLACE
        "#include \"FontImpl.h\""
        "#include \"FontImpl.h\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"yuri_ms_gothic_stream.hpp\""
        yuri_font_impl_with_trace "${yuri_font_impl_text}")

    # The Android backend asks Cocos to resolve its bundled fallback font.
    # Our intentionally frontend-free FileUtils facade has no asset namespace,
    # so carrying that lookup into Vita produces an empty storage name. Retain
    # only real game-supplied font discovery and make the enumerator's
    # empty-input semantics explicit.
    string(REPLACE
        "int TVPEnumFontsProc(const ttstr &FontPath)\n{\n    if(!TVPIsExistentStorageNoSearch(FontPath)) {"
        "int TVPEnumFontsProc(const ttstr &FontPath)\n{\n    if(FontPath.IsEmpty()) return 0;\n    if(!TVPIsExistentStorageNoSearch(FontPath)) {"
        yuri_font_impl_vita "${yuri_font_impl_with_trace}")
    if(yuri_font_impl_vita STREQUAL yuri_font_impl_with_trace)
        message(FATAL_ERROR "Yuri Vita empty font-path patch no longer applies")
    endif()
    string(REPLACE
        "\tTVPFontNamesInit = true;"
        "\tTVPFontNamesInit = true;\n\tconst ttstr vita_ms_gothic_path = TJS_W(\"ux0:data/krkrvita/msgothic.ttc\");\n\tconst int vita_ms_gothic_face_count = TVPEnumFontsProc(vita_ms_gothic_path);"
        yuri_font_impl_vita_ms_gothic "${yuri_font_impl_vita}")
    if(yuri_font_impl_vita_ms_gothic STREQUAL yuri_font_impl_vita)
        message(FATAL_ERROR "Yuri Vita MS Gothic discovery patch no longer applies")
    endif()
    set(yuri_cocos_font_fallback
        "        std::string fullPath = cocos2d::FileUtils::getInstance()->fullPathForFilename(\"DroidSansFallback.ttf\");\n        if (TVPEnumFontsProc(fullPath)) break;")
    string(REPLACE "${yuri_cocos_font_fallback}" ""
        yuri_font_impl_vita_2 "${yuri_font_impl_vita_ms_gothic}")
    if(yuri_font_impl_vita_2 STREQUAL yuri_font_impl_vita_ms_gothic)
        message(FATAL_ERROR "Yuri Cocos font fallback patch no longer applies")
    endif()
    string(REPLACE
        "if (s->Mode & (S_IFREG | S_IFDIR)) {"
        "if (s->Mode == S_IFREG) {"
        yuri_font_impl_vita_2b "${yuri_font_impl_vita_2}")
    if(yuri_font_impl_vita_2b STREQUAL yuri_font_impl_vita_2)
        message(FATAL_ERROR "Yuri Vita font file-type patch no longer applies")
    endif()

    # Yuri's original non-Android fonts/ scan passes a unified storage URI to
    # opendir(), then loses the directory prefix when registering each result.
    # Resolve the directory to a native Vita path for enumeration while
    # retaining the unified path for archive/storage reads.
    set(yuri_font_directory_block
        "\t\tTVPGetLocalFileListAt(TVPGetAppPath() + \"/fonts\", lister);\n        auto itend = list.end();\n        for (auto it = list.begin(); it != itend; ++it) {\n            TVPEnumFontsProc(*it);\n        }")
    set(yuri_vita_font_directory_block
        "\t\tttstr font_storage_path = TVPGetAppPath() + TJS_W(\"fonts/\");\n\t\tttstr font_native_path = font_storage_path;\n\t\tTVPGetLocalName(font_native_path);\n\t\tTVPGetLocalFileListAt(font_native_path, lister);\n        auto itend = list.end();\n        for (auto it = list.begin(); it != itend; ++it) {\n            TVPEnumFontsProc(font_storage_path + *it);\n        }")
    string(REPLACE "${yuri_font_directory_block}"
        "${yuri_vita_font_directory_block}"
        yuri_font_impl_vita_3 "${yuri_font_impl_vita_2b}")
    if(yuri_font_impl_vita_3 STREQUAL yuri_font_impl_vita_2b)
        message(FATAL_ERROR "Yuri Vita game-font directory patch no longer applies")
    endif()
    set(yuri_font_registry_marker
        "\tif (TVPDefaultFontName.IsEmpty()) {\n\t\tTVPShowSimpleMessageBox((\"Could not found any font.\\nPlease ensure that at least \\\"default.ttf\\\" exists\"), \"Exception Occured\");\n    }\n}")
    set(yuri_vita_font_registry [=[
	if (vita_ms_gothic_face_count > 0) {
		// Enumeration above has already opened and parsed the collection with
		// FreeType. Register the required face by its known collection index
		// instead of depending on which localized SFNT family-name records a
		// particular FreeType build exposes. Face zero is non-proportional MS
		// Gothic; UI Gothic and PGothic are different collection indices.
		TVPFontNamePathInfo vita_ms_gothic_info;
		vita_ms_gothic_info.Path = vita_ms_gothic_path;
		vita_ms_gothic_info.Getter = nullptr;
		vita_ms_gothic_info.Index = 0;
		TVPFontNames.Add(TJS_W("MS Gothic"), vita_ms_gothic_info);
		TVPFontNames.Add(TJS_W("ＭＳ ゴシック"), vita_ms_gothic_info);
		TVPFontNames.Add(TJS_W("ＭＳゴシック"), vita_ms_gothic_info);
		TVPDefaultFontName = TJS_W("ＭＳ ゴシック");
		krkrvita_boot_trace("yuri-msgothic-collection-parsed");
		krkrvita_boot_trace("yuri-msgothic-primary-selected");
	} else {
		krkrvita_boot_trace("yuri-msgothic-required-font-missing");
		TVPThrowExceptionMessage(TJS_W(
			"Required MS Gothic collection could not be opened or parsed at "
			"ux0:data/krkrvita/msgothic.ttc"));
	}
}
]=])
    string(REPLACE "${yuri_font_registry_marker}" "${yuri_vita_font_registry}"
        yuri_font_impl_patched "${yuri_font_impl_vita_3}")
    if(yuri_font_impl_patched STREQUAL yuri_font_impl_vita_3)
        message(FATAL_ERROR "Yuri Vita font registry patch no longer applies")
    endif()

    # A malformed or incomplete private-font entry can have no storage file.
    # FreeType must never send that empty sentinel to the storage-media manager:
    # doing so raises the misleading `Not supported media type ""` exception.
    string(REPLACE
        "\tif (info->Getter) {\n\t\treturn info->Getter(info);\n\t}\n\treturn TVPCreateBinaryStreamForRead(info->Path, TJS_W(\"\"));"
        "\tif (info->Getter) {\n\t\treturn info->Getter(info);\n\t}\n\tif (info->Path.IsEmpty()) return nullptr;\n\treturn TVPCreateBinaryStreamForRead(info->Path, TJS_W(\"\"));"
        yuri_font_impl_patched_2 "${yuri_font_impl_patched}")
    if(yuri_font_impl_patched_2 STREQUAL yuri_font_impl_patched)
        message(FATAL_ERROR "Yuri Vita empty system-font stream guard no longer applies")
    endif()
    set(yuri_font_stream_return [=[
	if (info->Getter) {
		return info->Getter(info);
	}
	if (info->Path.IsEmpty()) return nullptr;
	return TVPCreateBinaryStreamForRead(info->Path, TJS_W(""));
]=])
    set(yuri_font_stream_return_vita [=[
	if (info->Getter) {
		return info->Getter(info);
	}
	if (info->Path.IsEmpty()) return nullptr;
	if (info->Index == 0 &&
		info->Path == TJS_W("ux0:data/krkrvita/msgothic.ttc")) {
		if (tTJSBinaryStream *stream =
			krkrvita_create_ms_gothic_pread_stream()) {
			static bool stream_reported = false;
			if (!stream_reported) {
				krkrvita_boot_trace("yuri-msgothic-pread-stream-ready");
				stream_reported = true;
			}
			return stream;
		}
		static bool fallback_reported = false;
		if (!fallback_reported) {
			krkrvita_boot_trace("yuri-msgothic-pread-cache-fallback");
			fallback_reported = true;
		}
	}
	return TVPCreateBinaryStreamForRead(info->Path, TJS_W(""));
]=])
    string(REPLACE "${yuri_font_stream_return}"
        "${yuri_font_stream_return_vita}"
        yuri_font_impl_patched_3 "${yuri_font_impl_patched_2}")
    if(yuri_font_impl_patched_3 STREQUAL yuri_font_impl_patched_2)
        message(FATAL_ERROR "Yuri Vita MS Gothic pread stream patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/FontImpl.cpp"
        CONTENT "${yuri_font_impl_patched_3}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_font_impl}")
    list(APPEND yuri_visual_sources "${yuri_generated_dir}/FontImpl.cpp")
    list(APPEND yuri_visual_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vita/yuri_ms_gothic_stream.cpp")

    # FontSystem snapshots the registry on first use. Retail addFont.dll calls
    # can occur after that point, so its private name table does not contain a
    # newly registered game font. FreeType then substitutes the default face;
    # on Vita that default is a PVF alias rather than a disk file. Consult the
    # authoritative FontImpl registry for every existence check instead.
    set(yuri_font_system "${yuri_core}/visual/FontSystem.cpp")
    file(READ "${yuri_font_system}" yuri_font_system_text)
    string(REPLACE
        "#include \"FontSystem.h\""
        "#include \"FontSystem.h\"\n#include \"FontImpl.h\""
        yuri_font_system_vita "${yuri_font_system_text}")
    string(REPLACE
        "\tint * t = TVPFontNames.Find(name);\n\treturn t != NULL;"
        "\treturn TVPFindFont(name) != nullptr;"
        yuri_font_system_patched "${yuri_font_system_vita}")
    if(yuri_font_system_patched STREQUAL yuri_font_system_vita)
        message(FATAL_ERROR "Yuri dynamic game-font registry patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/FontSystem.cpp"
        CONTENT "${yuri_font_system_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_font_system}")
    list(APPEND yuri_visual_sources "${yuri_generated_dir}/FontSystem.cpp")

    # MS Gothic's normal dialogue path measures a character and immediately
    # rasterizes it. Yuri historically called FT_Load_Glyph for both halves.
    # Retain the exact unrendered slot after measurement and reuse it only when
    # every load input is identical. A small exact-key metrics cache handles
    # repeated characters without changing rounded advance semantics. Both are
    # limited to the fixed face-zero MS Gothic registry entry.
    set(yuri_freetype_impl "${yuri_core}/visual/FreeType.cpp")
    file(READ "${yuri_freetype_impl}" yuri_freetype_impl_text)
    string(REPLACE
        "#include \"FontImpl.h\""
        "#include \"FontImpl.h\"\n#include \"krkrvita/retail_bootstrap.hpp\""
        yuri_freetype_impl_patched "${yuri_freetype_impl_text}")
    string(REPLACE
        "\tHeight = 10;"
        "\tHeight = 10;\n\tconst TVPFontNamePathInfo *vita_font_info = TVPFindFont(fontname);\n\tUseVitaMsGothicFastPath = vita_font_info &&\n\t\tvita_font_info->Index == 0 &&\n\t\tTVP_GET_FACE_INDEX_FROM_OPTIONS(options) == 0 &&\n\t\tvita_font_info->Path == TJS_W(\"ux0:data/krkrvita/msgothic.ttc\");"
        yuri_freetype_impl_patched_2 "${yuri_freetype_impl_patched}")
    set(yuri_freetype_set_height [=[
void tFreeTypeFace::SetHeight(int height)
{
	Height = height;
	FT_Error err = FT_Set_Pixel_Sizes(FTFace, 0, Height);
]=])
    set(yuri_freetype_set_height_vita [=[
void tFreeTypeFace::SetHeight(int height)
{
	// Height is initialized to 10 before FreeType has a size. Only a prior
	// successful FT_Set_Pixel_Sizes makes a same-height call a real no-op.
	if(SizeInitialized && Height == height) return;
	InvalidatePreparedGlyph();
	Height = height;
	FT_Error err = FT_Set_Pixel_Sizes(FTFace, 0, Height);
	SizeInitialized = (err == 0);
]=])
    string(REPLACE "${yuri_freetype_set_height}"
        "${yuri_freetype_set_height_vita}"
        yuri_freetype_impl_patched_3 "${yuri_freetype_impl_patched_2}")
    string(REPLACE
        "\tif(!GetGlyphMetricsFromCharcode(code, metrics))\n\t\treturn NULL;"
        "\tif(!GetGlyphMetricsFromCharcode(code, metrics))\n\t\treturn NULL;\n\t// FT_Render_Glyph and bitmap normalization mutate the slot. It may be\n\t// consumed by this call, but must never survive as a prepared load.\n\tInvalidatePreparedGlyph();"
        yuri_freetype_impl_patched_4 "${yuri_freetype_impl_patched_3}")
    string(REPLACE
        "bool tFreeTypeFace::GetGlyphSizeFromCharcode(tjs_char code, tGlyphMetrics & metrics)\n{\n\tif(!LoadGlyphSlotFromCharcode(code)) return false;"
        "bool tFreeTypeFace::GetGlyphSizeFromCharcode(tjs_char code, tGlyphMetrics & metrics)\n{\n\tconst krkrvita::YuriGlyphCacheKey cache_key{\n\t\tstatic_cast<std::uint32_t>(code),\n\t\tstatic_cast<std::uint32_t>(Options),\n\t\tstatic_cast<std::int32_t>(Height)};\n\tif(UseVitaMsGothicFastPath && SizeInitialized &&\n\t\tGlyphMetricsCache.find(cache_key, metrics))\n\t\treturn true;\n\n\t// Preserve upstream retry semantics: a failed FT_Load_Glyph may be a\n\t// transient stream or allocation error, not a stable missing character.\n\tif(!LoadGlyphSlotFromCharcode(code)) return false;"
        yuri_freetype_impl_patched_5a "${yuri_freetype_impl_patched_4}")
    string(REPLACE
        "\tmetrics.CellIncY = FT_PosToInt( FTFace->glyph->metrics.vertAdvance );\n\n\treturn true;"
        "\tmetrics.CellIncY = FT_PosToInt( FTFace->glyph->metrics.vertAdvance );\n\t// Preserve Yuri's original rounded horizontal/vertical advance exactly.\n\t// Only successful loads at a successfully initialized size are cached;\n\t// failures must remain retryable.\n\tif(UseVitaMsGothicFastPath && SizeInitialized)\n\t\tGlyphMetricsCache.store(cache_key, metrics);\n\n\treturn true;"
        yuri_freetype_impl_patched_5 "${yuri_freetype_impl_patched_5a}")
    string(REPLACE
        "\tFT_Error err;\n\terr = FT_Load_Glyph(FTFace, glyph_index, load_glyph_flag);"
        "\tconst krkrvita::YuriPreparedGlyphKey prepared_key{\n\t\tstatic_cast<std::uint32_t>(code),\n\t\tstatic_cast<std::uint32_t>(glyph_index),\n\t\tstatic_cast<std::uint32_t>(Options),\n\t\tstatic_cast<std::int32_t>(Height),\n\t\tstatic_cast<std::int32_t>(load_glyph_flag)};\n\tif(UseVitaMsGothicFastPath && PreparedGlyphValid &&\n\t\tPreparedGlyphKey == prepared_key) {\n\t\tstatic bool reuse_reported = false;\n\t\tif(!reuse_reported) {\n\t\t\tkrkrvita_boot_trace(\"yuri-msgothic-prepared-slot-reused\");\n\t\t\treuse_reported = true;\n\t\t}\n\t\treturn true;\n\t}\n\tInvalidatePreparedGlyph();\n\n\tFT_Error err;\n\terr = FT_Load_Glyph(FTFace, glyph_index, load_glyph_flag);"
        yuri_freetype_impl_patched_6 "${yuri_freetype_impl_patched_5}")
    string(REPLACE
        "\tif( Options & TVP_TF_ITALIC ) FT_GlyphSlot_Oblique( FTFace->glyph );\n\n\treturn true;"
        "\tif( Options & TVP_TF_ITALIC ) FT_GlyphSlot_Oblique( FTFace->glyph );\n\n\tif(UseVitaMsGothicFastPath) {\n\t\tPreparedGlyphKey = prepared_key;\n\t\tPreparedGlyphValid = true;\n\t}\n\treturn true;"
        yuri_freetype_impl_patched_7 "${yuri_freetype_impl_patched_6}")
    string(REPLACE
        "const FT_Outline* tFreeTypeFace::GetOulineData(tjs_char code, float &w, float &h)\n{\n\tFT_UInt glyph_index"
        "const FT_Outline* tFreeTypeFace::GetOulineData(tjs_char code, float &w, float &h)\n{\n\t// This path calls FT_Load_Glyph directly and replaces the shared slot.\n\tInvalidatePreparedGlyph();\n\tFT_UInt glyph_index"
        yuri_freetype_impl_patched_8 "${yuri_freetype_impl_patched_7}")
    foreach(yuri_freetype_fast_contract
        "UseVitaMsGothicFastPath = vita_font_info"
        "SizeInitialized && Height == height"
        "GlyphMetricsCache.find"
        "Preserve Yuri's original rounded horizontal/vertical advance exactly"
        "PreparedGlyphKey == prepared_key"
        "yuri-msgothic-prepared-slot-reused"
        "FT_Render_Glyph and bitmap normalization mutate the slot"
        "This path calls FT_Load_Glyph directly")
        string(FIND "${yuri_freetype_impl_patched_8}"
            "${yuri_freetype_fast_contract}" yuri_freetype_fast_offset)
        if(yuri_freetype_fast_offset LESS 0)
            message(FATAL_ERROR
                "Yuri MS Gothic FreeType fast-path patch no longer applies: ${yuri_freetype_fast_contract}")
        endif()
    endforeach()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/FreeType.cpp"
        CONTENT "${yuri_freetype_impl_patched_8}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_freetype_impl}")
    list(APPEND yuri_visual_sources "${yuri_generated_dir}/FreeType.cpp")

    # Yuri records the face index while enumerating TTC collections, but its
    # FreeType rasterizer discarded that index and always opened face zero.
    # Preserve the collection's real semantics; msgothic.ttc face zero is
    # specifically MS Gothic, while UI Gothic and PGothic remain faces 1/2.
    set(yuri_freetype_rasterizer
        "${yuri_core}/visual/FreeTypeFontRasterizer.cpp")
    file(READ "${yuri_freetype_rasterizer}" yuri_freetype_rasterizer_text)
    string(REPLACE
        "#include \"FontSystem.h\""
        "#include \"FontSystem.h\"\n#include \"FontImpl.h\"\n#include \"krkrvita/retail_bootstrap.hpp\""
        yuri_freetype_rasterizer_patched
        "${yuri_freetype_rasterizer_text}")
    string(REPLACE
        "\topt |= (font.Flags & TVP_TF_FONTFILE) ? TVP_FACE_OPTIONS_FILE : 0;\n\tbool recreate = false;"
        "\topt |= (font.Flags & TVP_TF_FONTFILE) ? TVP_FACE_OPTIONS_FILE : 0;\n\tconst TVPFontNamePathInfo *selected_info = TVPFindFont(stdname);\n\tif (selected_info)\n\t\topt |= TVP_FACE_OPTIONS_FACE_INDEX(selected_info->Index);\n\tbool recreate = false;"
        yuri_freetype_rasterizer_patched_2
        "${yuri_freetype_rasterizer_patched}")
    if(yuri_freetype_rasterizer_patched_2 STREQUAL
       yuri_freetype_rasterizer_patched)
        message(FATAL_ERROR "Yuri TTC face-index patch no longer applies")
    endif()
    string(REPLACE
        "\tFace->SetHeight( font.Height < 0 ? -font.Height : font.Height );"
        "\tFace->SetHeight( font.Height < 0 ? -font.Height : font.Height );\n\tstatic bool ms_gothic_face_reported = false;\n\tif (!ms_gothic_face_reported && selected_info &&\n\t\tselected_info->Index == 0 &&\n\t\tselected_info->Path == TJS_W(\"ux0:data/krkrvita/msgothic.ttc\")) {\n\t\tkrkrvita_boot_trace(\"yuri-msgothic-freetype-face-applied\");\n\t\tms_gothic_face_reported = true;\n\t}"
        yuri_freetype_rasterizer_patched_3
        "${yuri_freetype_rasterizer_patched_2}")
    string(REPLACE
        "\tif( Face ) {\n\t\ttGlyphMetrics metrics;\n\t\tif( Face->GetGlyphSizeFromCharcode( ch, metrics) ) {\n\t\t\tw = metrics.CellIncX;\n\t\t\th = metrics.CellIncY;\n\t\t} else {\n\t\t\tw = Face->GetHeight();\n\t\t\th = w;\n\t\t}\n\t}"
        "\tif( Face ) {\n\t\ttGlyphMetrics metrics;\n\t\tif( Face->GetGlyphSizeFromCharcode( ch, metrics) ) {\n\t\t\tw = metrics.CellIncX;\n\t\t\th = metrics.CellIncY;\n\t\t} else {\n\t\t\tw = Face->GetHeight();\n\t\t\th = w;\n\t\t}\n\t\tstatic bool extent_reported = false;\n\t\tif (!extent_reported) {\n\t\t\tkrkrvita_boot_trace(\"yuri-freetype-text-extent-complete\");\n\t\t\textent_reported = true;\n\t\t}\n\t}"
        yuri_freetype_rasterizer_patched_4
        "${yuri_freetype_rasterizer_patched_3}")
    set(yuri_freetype_extent_traced [=[
void FreeTypeFontRasterizer::GetTextExtent(tjs_char ch, tjs_int &w, tjs_int &h) {
	if( Face ) {
		tGlyphMetrics metrics;
		if( Face->GetGlyphSizeFromCharcode( ch, metrics) ) {
			w = metrics.CellIncX;
			h = metrics.CellIncY;
		} else {
			w = Face->GetHeight();
			h = w;
		}
		static bool extent_reported = false;
		if (!extent_reported) {
			krkrvita_boot_trace("yuri-freetype-text-extent-complete");
			extent_reported = true;
		}
	}
}
]=])
    set(yuri_freetype_extent_prefix_safe [=[
bool FreeTypeFontRasterizer::GetTextExtentCacheState(tjs_uint64 &state) const {
	return Face && Face->GetVitaMsGothicMetricState(state);
}
//---------------------------------------------------------------------------
void FreeTypeFontRasterizer::GetTextExtent(tjs_char ch, tjs_int &w, tjs_int &h) {
	(void)GetTextExtentForPrefixCache(ch, w, h);
}
//---------------------------------------------------------------------------
bool FreeTypeFontRasterizer::GetTextExtentForPrefixCache(
	tjs_char ch, tjs_int &w, tjs_int &h) {
	if( Face ) {
		tGlyphMetrics metrics;
		const bool loaded = Face->GetGlyphSizeFromCharcode(ch, metrics);
		if(loaded) {
			w = metrics.CellIncX;
			h = metrics.CellIncY;
		} else {
			w = Face->GetHeight();
			h = w;
		}
		static bool extent_reported = false;
		if (!extent_reported) {
			krkrvita_boot_trace("yuri-freetype-text-extent-complete");
			extent_reported = true;
		}
		tjs_uint64 state;
		return loaded && Face->GetVitaMsGothicMetricState(state);
	}
	return false;
}
]=])
    string(REPLACE "${yuri_freetype_extent_traced}"
        "${yuri_freetype_extent_prefix_safe}"
        yuri_freetype_rasterizer_patched_4a
        "${yuri_freetype_rasterizer_patched_4}")
    if(yuri_freetype_rasterizer_patched_4a STREQUAL
       yuri_freetype_rasterizer_patched_4)
        message(FATAL_ERROR
            "Yuri stable text-prefix extent patch no longer applies")
    endif()
    string(REPLACE
        "\tif(font.Blured) data->Blur(); // nasty ...\n\treturn data;"
        "\tif(font.Blured) data->Blur(); // nasty ...\n\tstatic bool glyph_reported = false;\n\tif (!glyph_reported) {\n\t\tkrkrvita_boot_trace(\"yuri-freetype-first-glyph-rendered\");\n\t\tglyph_reported = true;\n\t}\n\treturn data;"
        yuri_freetype_rasterizer_patched_5
        "${yuri_freetype_rasterizer_patched_4a}")
    if(yuri_freetype_rasterizer_patched_5 STREQUAL
       yuri_freetype_rasterizer_patched_2)
        message(FATAL_ERROR "Yuri FreeType hardware proof patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/FreeTypeFontRasterizer.cpp"
        CONTENT "${yuri_freetype_rasterizer_patched_5}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_freetype_rasterizer}")
    list(APPEND yuri_visual_sources
        "${yuri_generated_dir}/FreeTypeFontRasterizer.cpp")

    set(yuri_layer_bitmap_impl
        "${yuri_core}/visual/win32/LayerBitmapImpl.cpp")
    file(READ "${yuri_layer_bitmap_impl}" yuri_layer_bitmap_impl_text)
    # LayerBitmapImpl is the one Yuri visual source that spells the include as
    # visual/FreeType.h.  That path bypasses the generated Vita overlay and
    # gives this translation unit a different tFreeTypeFace definition from
    # FreeType.cpp and FreeTypeFontRasterizer.cpp.  Keep every linked consumer
    # on the same augmented header; mixing the upstream and overlay definitions
    # is an ODR/ABI violation even where LayerBitmapImpl treats the face as an
    # opaque implementation detail.
    string(REPLACE
        "#include \"visual/FreeType.h\""
        "#include \"FreeType.h\""
        yuri_layer_bitmap_impl_overlay
        "${yuri_layer_bitmap_impl_text}")
    if(yuri_layer_bitmap_impl_overlay STREQUAL yuri_layer_bitmap_impl_text)
        message(FATAL_ERROR
            "Yuri LayerBitmapImpl FreeType overlay include patch no longer applies")
    endif()
    string(REPLACE
        "#include \"FreeTypeFontRasterizer.h\""
        "#include \"FreeTypeFontRasterizer.h\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"krkrvita/text_prefix_width.hpp\""
        yuri_layer_bitmap_impl_patched "${yuri_layer_bitmap_impl_overlay}")
    string(REPLACE
        "\t\tTVPFontRasterizers[FONT_RASTER_FREE_TYPE] = new FreeTypeFontRasterizer();"
        "\t\tTVPFontRasterizers[FONT_RASTER_FREE_TYPE] = new FreeTypeFontRasterizer();\n\t\tkrkrvita_boot_trace(\"yuri-msgothic-freetype-rasterizer-selected\");"
        yuri_layer_bitmap_impl_patched_2
        "${yuri_layer_bitmap_impl_patched}")
    # The Yuri/Windows constructor historically relied on its derived class
    # assigning Bitmap immediately.  That is not exception-safe: if a large
    # texture allocation throws, the base destructor observes an indeterminate
    # pointer while unwinding and turns a recoverable OOM into a data abort.
    string(REPLACE
        "\tFont = TVPFontSystem->GetDefaultFont();\n\tPrerenderedFont = NULL;\n\t//LogFont = TVPDefaultLOGFONT;"
        "\tFont = TVPFontSystem->GetDefaultFont();\n\tPrerenderedFont = NULL;\n\tBitmap = nullptr;\n\t//LogFont = TVPDefaultLOGFONT;"
        yuri_layer_bitmap_impl_patched_3
        "${yuri_layer_bitmap_impl_patched_2}")
    if(yuri_layer_bitmap_impl_patched_3 STREQUAL yuri_layer_bitmap_impl_patched_2)
        message(FATAL_ERROR "Yuri bitmap exception-safety patch no longer applies")
    endif()
    if(yuri_layer_bitmap_impl_patched_3 STREQUAL yuri_layer_bitmap_impl_text)
        message(FATAL_ERROR "Yuri Vita FreeType rasterizer patch no longer applies")
    endif()
    set(yuri_bitmap_independ_old [=[
void tTVPNativeBaseBitmap::Independ()
{
	// sever Bitmap's image sharing
	if (Bitmap->IsIndependent() && !Bitmap->IsStatic()) return;
	iTVPTexture2D *newb = GetRenderManager()->CreateTexture2D(Bitmap->GetWidth(), Bitmap->GetHeight(), Bitmap);
	Bitmap->Release();
	Bitmap = newb;
	FontChanged = true; // informs internal font information is invalidated
}
]=])
    set(yuri_layer_bitmap_impl_patched_4
        "${yuri_layer_bitmap_impl_patched_3}")

    # ApplyFont only refreshes font/rasterizer metadata.  In the compiled Vita
    # backend FreeTypeFontRasterizer::ApplyFont(tTVPNativeBaseBitmap *, ...)
    # reads GetFont() and never reads or writes the bitmap texture.  Detaching
    # here therefore copied a shared 1280x960 surface even for read-only calls
    # such as getTextWidth().  Pixel-writing text entry points already detach
    # before ApplyFont: DrawGlyph, DrawTextSingle and DrawTextMultiple.
    set(yuri_apply_font_detaching [=[
void tTVPNativeBaseBitmap::ApplyFont()
{
	// apply font
	if(FontChanged || GlobalFontState != TVPGlobalFontStateMagic)
	{
		Independ();

		FontChanged = false;
]=])
    set(yuri_apply_font_metadata_only [=[
void tTVPNativeBaseBitmap::ApplyFont()
{
	// Font application changes only cached font/rasterizer metadata.  Every
	// pixel-writing text entry point has already detached its destination.
	if(FontChanged || GlobalFontState != TVPGlobalFontStateMagic)
	{
		FontChanged = false;
]=])
    string(REPLACE "${yuri_apply_font_detaching}"
        "${yuri_apply_font_metadata_only}"
        yuri_layer_bitmap_impl_patched_5
        "${yuri_layer_bitmap_impl_patched_4}")
    if(yuri_layer_bitmap_impl_patched_5 STREQUAL yuri_layer_bitmap_impl_patched_4)
        message(FATAL_ERROR "Yuri metadata-only ApplyFont patch no longer applies")
    endif()
    string(REPLACE
        "\t\tCachedText.Clear();\n\t\tTextWidth = TextHeight = 0;"
        "\t\tCachedText.Clear();\n\t\tCachedTextMetricState = 0;\n\t\tCachedTextPrefixReusable = false;\n\t\tTextWidth = TextHeight = 0;"
        yuri_layer_bitmap_impl_patched_5a
        "${yuri_layer_bitmap_impl_patched_5}")
    if(yuri_layer_bitmap_impl_patched_5a STREQUAL
       yuri_layer_bitmap_impl_patched_5)
        message(FATAL_ERROR
            "Yuri text-prefix font-state invalidation patch no longer applies")
    endif()

    # Yuri's text width is exactly the unsigned sum of independent per-code-
    # unit advances; it has no pair kerning or shaping state. KAG's history
    # layer appends one character and asks for the entire accumulated width on
    # every timer tick. Resume after the existing exact cached string only
    # when both code units and the lossless FreeType metric-state key match.
    # Missing/transient glyph loads opt the result out, retaining retries.
    set(yuri_get_text_size_full_scan [=[
void tTVPNativeBaseBitmap::GetTextSize(const ttstr & text)
{
	ApplyFont();

	if(text != CachedText)
	{
		CachedText = text;

		if(PrerenderedFont)
		{
			tjs_uint width = 0;
			const tjs_char *buf = text.c_str();
			while(*buf)
			{
				const tTVPPrerenderedCharacterItem * item =
					PrerenderedFont->Find(*buf);
				if(item != NULL)
				{
					width += item->Inc;
				}
				else
				{
					tjs_int w, h;
					GetCurrentRasterizer()->GetTextExtent( *buf, w, h );
					width += w;
				}
				buf++;
			}
			TextWidth = width;
			TextHeight = std::abs(Font.Height);
		}
		else
		{
			tjs_uint width = 0;
			const tjs_char *buf = text.c_str();

			while(*buf)
			{
				tjs_int w, h;
				GetCurrentRasterizer()->GetTextExtent( *buf, w, h );
				width += w;
				buf++;
			}
			TextWidth = width;
			TextHeight = std::abs(Font.Height);
		}
	}
}
]=])
    set(yuri_get_text_size_prefix_reuse [=[
void tTVPNativeBaseBitmap::GetTextSize(const ttstr & text)
{
	ApplyFont();

	if(text != CachedText)
	{
		FontRasterizer *rasterizer = GetCurrentRasterizer();
		tjs_uint64 metric_state = 0;
		const bool metric_state_valid =
			rasterizer->GetTextExtentCacheState(metric_state);
		const tjs_char *text_data = text.c_str();
		const auto prefix = krkrvita::yuri_find_text_width_prefix(
			CachedText.c_str(), CachedText.GetLen(),
			static_cast<tjs_uint>(TextWidth), CachedTextPrefixReusable,
			CachedTextMetricState, text_data, text.GetLen(),
			metric_state_valid, metric_state);

		// Preserve Yuri's exception behavior: the exact-string cache changes
		// before rasterization, while a failed partial result is never reusable
		// as the prefix of a later string.
		CachedText = text;
		CachedTextPrefixReusable = false;
		tjs_uint width = static_cast<tjs_uint>(prefix.width);
		const tjs_char *buf = text_data;
		if(prefix.code_units != 0) buf += prefix.code_units;
		bool measured_stably = metric_state_valid;
		if(prefix.reused)
		{
			static bool prefix_reported = false;
			if(!prefix_reported)
			{
				krkrvita_boot_trace("yuri-msgothic-text-prefix-reused");
				prefix_reported = true;
			}
		}

		while(*buf)
		{
			const tTVPPrerenderedCharacterItem * item =
				PrerenderedFont ? PrerenderedFont->Find(*buf) : NULL;
			if(item != NULL)
			{
				width += item->Inc;
			}
			else
			{
				tjs_int w, h;
				const bool glyph_stable =
					rasterizer->GetTextExtentForPrefixCache(*buf, w, h);
				measured_stably = measured_stably && glyph_stable;
				width += w;
			}
			buf++;
		}
		TextWidth = width;
		TextHeight = std::abs(Font.Height);
		CachedTextMetricState = metric_state;
		CachedTextPrefixReusable = measured_stably;
	}
}
]=])
    string(REPLACE "${yuri_get_text_size_full_scan}"
        "${yuri_get_text_size_prefix_reuse}"
        yuri_layer_bitmap_impl_patched_5b
        "${yuri_layer_bitmap_impl_patched_5a}")
    if(yuri_layer_bitmap_impl_patched_5b STREQUAL
       yuri_layer_bitmap_impl_patched_5a)
        message(FATAL_ERROR
            "Yuri growing-line text-prefix patch no longer applies")
    endif()

    # InternalBlendText is private and is reached only after one of the three
    # text drawing entry points above has called Independ().  Calling
    # GetTextureForRender for every shadow/glyph merely repeats the ownership
    # check; use that already-detached destination without changing blend or
    # rasterization semantics.  Real aliases still detach once at the public
    # entry boundary before their first pixel write.
    set(yuri_internal_blend_detaching [=[
	TVPGetRenderManager()->OperateRect(method,
		GetTextureForRender(method->IsBlendTarget(), &drect), nullptr, drect,
]=])
    set(yuri_internal_blend_detached [=[
	TVPGetRenderManager()->OperateRect(method,
		GetTexture(), nullptr, drect,
]=])
    string(REPLACE "${yuri_internal_blend_detaching}"
        "${yuri_internal_blend_detached}"
        yuri_layer_bitmap_impl_patched_6
        "${yuri_layer_bitmap_impl_patched_5b}")
    if(yuri_layer_bitmap_impl_patched_6 STREQUAL yuri_layer_bitmap_impl_patched_5b)
        message(FATAL_ERROR "Yuri per-glyph redundant detach patch no longer applies")
    endif()

    # Remove Yuri's non-compiling historical shader sketch from this function
    # so the generated source itself has one unambiguous destination-ownership
    # contract.  The block was permanently excluded by #if 0 upstream.
    set(yuri_internal_blend_dead_shader_path [=[
#if 0
    if (pShader->isBlendEnabled() || !IsIndependent()) {
		iTVPTexture2D *origTex = GetTexture();
		iTVPTexture2D *tex = GetTextureForRender();
        TVPRenderTexture2(pShader,
            tex, drect,
            origTex, drect,
            _CharacterTexture, texRect(0, 0, w, h));
    } else { // optimize for independent texture
		iTVPTexture2D *tmptex = TVPCreateTextureForRender(drect.get_width(), drect.get_height());
		iTVPTexture2D *tex = GetTexture();
        texRect rc(drect);
        rc.x = 0; rc.y = 0;
        TVPCopyTexture(
            tmptex, rc,
            tex, drect);
        TVPRenderTexture2(pShader,
            tex, drect,
            tmptex, rc,
            _CharacterTexture, texRect(0, 0, w, h));
        tmptex->Release();
    }
#endif
]=])
    string(REPLACE "${yuri_internal_blend_dead_shader_path}" ""
        yuri_layer_bitmap_impl_patched_7
        "${yuri_layer_bitmap_impl_patched_6}")
    if(yuri_layer_bitmap_impl_patched_7 STREQUAL yuri_layer_bitmap_impl_patched_6)
        message(FATAL_ERROR "Yuri dead text shader path removal no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/LayerBitmapImpl.cpp"
        CONTENT "${yuri_layer_bitmap_impl_patched_7}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_layer_bitmap_impl}")
    list(APPEND yuri_visual_sources
        "${yuri_generated_dir}/LayerBitmapImpl.cpp")

    # Yuri's Cocos frontend carried the final software surface into its scene,
    # but its BasicDrawDevice accidentally retained a null-buffer semicolon and
    # compiled its base destination rectangle update out with the old D3D code.
    # The Vita window adapter is the frontend boundary now, so preserve the
    # software-compositor path while restoring those two engine contracts.
    set(yuri_basic_draw_device
        "${yuri_core}/visual/win32/BasicDrawDevice.cpp")
    file(READ "${yuri_basic_draw_device}" yuri_basic_draw_device_text)
    set(yuri_basic_dest_noop [=[
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetDestRectangle(const tTVPRect & rect)
{
#if 0
]=])
    set(yuri_basic_dest_vita [=[
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetDestRectangle(const tTVPRect & rect)
{
	inherited::SetDestRectangle(rect);
#if 0
]=])
    string(REPLACE "${yuri_basic_dest_noop}" "${yuri_basic_dest_vita}"
        yuri_basic_draw_device_patched "${yuri_basic_draw_device_text}")
    if(yuri_basic_draw_device_patched STREQUAL yuri_basic_draw_device_text)
        message(FATAL_ERROR
            "Yuri Vita BasicDrawDevice destination patch no longer applies")
    endif()
    string(REPLACE
        "\t\t\tif (buf);\n\t\t\t\tform->UpdateDrawBuffer(buf->GetTexture());"
        "\t\t\tif (buf)\n\t\t\t\tform->UpdateDrawBuffer(buf->GetTexture());"
        yuri_basic_draw_device_patched_2
        "${yuri_basic_draw_device_patched}")
    if(yuri_basic_draw_device_patched_2 STREQUAL yuri_basic_draw_device_patched)
        message(FATAL_ERROR
            "Yuri Vita BasicDrawDevice null-buffer patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/BasicDrawDevice.cpp"
        CONTENT "${yuri_basic_draw_device_patched_2}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_visual_sources "${yuri_basic_draw_device}")
    list(APPEND yuri_visual_sources
        "${yuri_generated_dir}/BasicDrawDevice.cpp")

    list(REMOVE_ITEM yuri_visual_sources
        "${yuri_core}/visual/LoadBPG.cpp"
        "${yuri_core}/visual/LoadJXR.cpp"
        "${yuri_core}/visual/LoadPVRv3.cpp"
        "${yuri_core}/visual/win32/GDIFontRasterizer.cpp"
        "${yuri_core}/visual/win32/NativeFreeTypeFace.cpp"
        "${yuri_core}/visual/win32/TVPSysFont.cpp"
        "${yuri_core}/visual/win32/TVPScreen.cpp"
        "${yuri_core}/visual/win32/VSyncTimingThread.cpp"
    )
    list(APPEND yuri_visual_sources
        "${yuri_core}/visual/ARM/tvpgl_arm.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vita/yuri_pvr.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vita/yuri_screen.cpp"
    )
    add_library(krkrvita-yuri-visual STATIC EXCLUDE_FROM_ALL
        ${yuri_visual_sources}
    )
    krkrvita_set_yuri_backend_target_defaults(krkrvita-yuri-visual)
    target_compile_definitions(krkrvita-yuri-visual PRIVATE
        KRKRVITA_BACKEND_ONLY=1
    )
    target_link_libraries(krkrvita-yuri-visual PUBLIC
        krkrvita-yuri-extension
        krkrvita-yuri-utils
        krkrvita-yuri-base
        krkrvita-yuri-tjs
        jpeg
        png
        webp
        freetype
        swscale
        avutil
    )

    # Engine/application orchestration is backend code in Yuri. Its Android
    # launcher, Cocos scene, forms and crash-upload UI are intentionally not
    # members of this target; their OS contracts are supplied by Vita files.
    set(yuri_detect_cpu "${yuri_core}/environ/DetectCPU.cpp")
    file(READ "${yuri_detect_cpu}" yuri_detect_cpu_text)
    string(REPLACE
        "#ifdef __APPLE__\n    // must be iOS\n    TVPCPUFeatures |= TVP_CPU_FAMILY_ARM | TVP_CPU_HAS_NEON;\n#endif"
        "#if defined(__APPLE__) || defined(__vita__)\n    TVPCPUFeatures |= TVP_CPU_FAMILY_ARM | TVP_CPU_HAS_NEON;\n#endif"
        yuri_detect_cpu_patched "${yuri_detect_cpu_text}")
    if(yuri_detect_cpu_patched STREQUAL yuri_detect_cpu_text)
        message(FATAL_ERROR "Yuri Vita ARM CPU feature patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/DetectCPU.cpp"
        CONTENT "${yuri_detect_cpu_patched}"
        @ONLY NEWLINE_STYLE UNIX)

    set(yuri_application "${yuri_core}/environ/Application.cpp")
    file(READ "${yuri_application}" yuri_application_text)
    string(REPLACE
        "#include \"Application.h\""
        "#include \"Application.h\"\n#include \"LayerIntf.h\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"krkrvita/system_app_id_compat.hpp\"\n#include \"krkrvita/vita_storage_path.hpp\"\n#include \"krkrvita/yuri_storage_preflight.hpp\""
        yuri_application_patched "${yuri_application_text}")
    string(REPLACE
        "bool tTVPApplication::StartApplication(ttstr path) {"
        "bool tTVPApplication::StartApplication(ttstr path) {\n\tkrkrvita_boot_trace(\"yuri-start-application-entered\");"
        yuri_application_patched "${yuri_application_patched}")
    # Kirikiri's System.exeName is the game executable's path, and some titles
    # refuse to boot unless chopStorageExt(System.exeName) + ".cf" exists.
    # Yuri returns the project directory here because Android has no game
    # executable; on Vita the Windows executable is staged with the game, so
    # resolve it and keep the directory only as the fallback.
    string(REPLACE
        "ttstr ExePath() {\n\treturn TVPNativeProjectDir;\n}"
        "ttstr ExePath() {\n#ifdef __vita__\n\tstatic ttstr resolved_root;\n\tstatic ttstr resolved_executable;\n\tif(!TVPNativeProjectDir.IsEmpty() && TVPNativeProjectDir != resolved_root) {\n\t\tresolved_root = TVPNativeProjectDir;\n\t\tresolved_executable =\n\t\t\tkrkrvita_yuri_project_executable_path(TVPNativeProjectDir);\n\t}\n\tif(!resolved_executable.IsEmpty()) return resolved_executable;\n#endif\n\treturn TVPNativeProjectDir;\n}"
        yuri_application_patched "${yuri_application_patched}")
    string(REPLACE
        "\tTVPNativeProjectDir = path;"
        "#ifdef __vita__\n\tpath = ttstr(krkrvita::vita_directory_path(\n\t\tstd::u16string_view(path.c_str(), path.GetLen())));\n#endif\n\tTVPNativeProjectDir = path;"
        yuri_application_patched "${yuri_application_patched}")
    string(REPLACE
        "\t\tTVPProjectDir = TVPNormalizeStorageName(path);"
        "\t\tTVPProjectDir = TVPNormalizeStorageName(krkrvita_yuri_select_project(path));\n\t\tkrkrvita_boot_trace(\"yuri-project-normalized\");"
        yuri_application_patched "${yuri_application_patched}")
    string(REPLACE
        "\t\tTVPInitScriptEngine();"
        "\t\tTVPInitScriptEngine();\n\t\tkrkrvita_boot_trace(\"yuri-script-engine-initialized\");"
        yuri_application_patched "${yuri_application_patched}")
    string(REPLACE
        "\t\tTVPInitFontNames();"
        "\t\tTVPInitFontNames();\n\t\tkrkrvita_boot_trace(\"yuri-fonts-initialized\");"
        yuri_application_patched "${yuri_application_patched}")
    set(yuri_layer_cache_policy_anchor
        "\t\tTVPInitializeBaseSystems();")
    set(yuri_layer_cache_policy_replacement
        "\t\tTVPInitializeBaseSystems();\n\t\tTVPFreeUnusedLayerCache = true;\n\t\tkrkrvita_boot_trace(\"yuri-eager-layer-cache-release-enabled\");\n\t\tkrkrvita_boot_trace(\"yuri-base-systems-initialized\");\n\t\tif(!krkrvita::install_system_app_id_compat(*TVPGetScriptEngine()))\n\t\t\tTVPThrowInternalError;\n\t\tkrkrvita_boot_trace(\"yuri-system-app-id-compat-ready\");")
    set(yuri_layer_cache_policy_input "${yuri_application_patched}")
    string(REPLACE
        "${yuri_layer_cache_policy_anchor}"
        "${yuri_layer_cache_policy_replacement}"
        yuri_application_patched "${yuri_application_patched}")
    if(yuri_application_patched STREQUAL yuri_layer_cache_policy_input)
        message(FATAL_ERROR
            "Yuri eager layer-cache release patch no longer applies")
    endif()
    string(REPLACE
        "\t\tInitialize();"
        "\t\tInitialize();\n\t\tkrkrvita_boot_trace(\"yuri-application-initialized\");\n\t\tkrkrvita_yuri_storage_preflight(path);"
        yuri_application_patched "${yuri_application_patched}")
    string(REPLACE
        "\t\tTVPLoadPluigins(); // load plugin module *.tpm"
        "\t\tTVPLoadPluigins(); // load plugin module *.tpm\n\t\tkrkrvita_boot_trace(\"yuri-internal-plugins-ready\");"
        yuri_application_patched "${yuri_application_patched}")
    string(REPLACE
        "\t\tTVPSystemInit();"
        "\t\tTVPSystemInit();\n\t\tkrkrvita_yuri_startup_storage_preflight();\n\t\tkrkrvita_boot_trace(\"yuri-system-initialized\");"
        yuri_application_patched "${yuri_application_patched}")
    string(REPLACE
        "\t\tTVPSystemControl = new tTVPSystemControl();"
        "\t\tTVPSystemControl = new tTVPSystemControl();\n\t\tkrkrvita_boot_trace(\"yuri-system-control-created\");"
        yuri_application_patched "${yuri_application_patched}")
    string(REPLACE
        "\t\t/*if(TVPProjectDirSelected)*/ TVPInitializeStartupScript();"
        "\t\tkrkrvita_boot_trace(\"yuri-startup-script-entered\");\n\t\t/*if(TVPProjectDirSelected)*/ TVPInitializeStartupScript();\n\t\tkrkrvita_boot_trace(\"yuri-startup-script-complete\");"
        yuri_application_patched "${yuri_application_patched}")
    foreach(yuri_application_trace_name
        yuri-start-application-entered
        yuri-project-normalized
        yuri-script-engine-initialized
        yuri-fonts-initialized
        yuri-eager-layer-cache-release-enabled
        yuri-base-systems-initialized
        yuri-system-app-id-compat-ready
        yuri-application-initialized
        yuri-internal-plugins-ready
        yuri-system-initialized
        yuri-system-control-created
        yuri-startup-script-entered
        yuri-startup-script-complete)
        string(FIND "${yuri_application_patched}"
            "krkrvita_boot_trace(\"${yuri_application_trace_name}\")"
            yuri_application_trace_offset)
        if(yuri_application_trace_offset LESS 0)
            message(FATAL_ERROR
                "Yuri application trace point no longer applies: ${yuri_application_trace_name}")
        endif()
    endforeach()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/Application.cpp"
        CONTENT "${yuri_application_patched}"
        @ONLY NEWLINE_STYLE UNIX)

    add_library(krkrvita-yuri-environ STATIC EXCLUDE_FROM_ALL
        "${yuri_generated_dir}/Application.cpp"
        "${yuri_generated_dir}/DetectCPU.cpp"
        "${yuri_core}/environ/win32/SystemControl.cpp"
        src/platform/vita/yuri_config.cpp
        src/platform/vita/yuri_menu.cpp
        src/platform/vita/yuri_platform.cpp
        src/platform/vita/yuri_video_overlay.cpp
    )
    krkrvita_set_yuri_backend_target_defaults(krkrvita-yuri-environ)
    target_compile_definitions(krkrvita-yuri-environ PRIVATE
        KRKRVITA_BACKEND_ONLY=1
    )
    target_link_libraries(krkrvita-yuri-environ PUBLIC
        krkrvita-yuri-sound
        krkrvita-yuri-visual
        krkrvita-yuri-extension
        krkrvita-yuri-utils
        krkrvita-yuri-base
        krkrvita-yuri-tjs
        SceIofilemgr_stub
        SceLibKernel_stub
        SceKernelThreadMgr_stub
        SceSysmem_stub
        SceRtc_stub
    )

    # Yuri's statically registered native modules are part of the retail-game
    # backend ABI. This includes xp3filter and addFont; no Windows DLL loading
    # or Android plugin UI is involved.
    file(GLOB yuri_plugin_sources CONFIGURE_DEPENDS
        "${KRKRVITA_YURI_SOURCE_DIR}/src/plugins/*.cpp"
    )
    # layerExMovie is coupled to Yuri's separate FFmpeg movie graph. Keep it
    # out until that backend target is ported; substituting a Cocos/Android
    # video frontend would violate the Vita backend boundary.
    list(REMOVE_ITEM yuri_plugin_sources
        "${KRKRVITA_YURI_SOURCE_DIR}/src/plugins/layerExMovie.cpp")

    # saveStruct serializes quoted strings one UTF-16 code unit at a time.
    # Yuri's Android stream buffers the complete output, but Vita deliberately
    # writes local files directly to avoid a second multi-megabyte save image.
    # Without a bounded adapter that becomes tens of thousands of sceIoWrite
    # calls for a modest KAG system-variable dictionary. Generate a plugin
    # overlay with an 8 KiB fixed buffer and explicit flushes before the stream
    # is closed or a memory-stream result is observed.
    set(yuri_save_struct_plugin
        "${KRKRVITA_YURI_SOURCE_DIR}/src/plugins/saveStruct.cpp")
    file(READ "${yuri_save_struct_plugin}" yuri_save_struct_text)
    string(REPLACE
        "#include \"PluginIntf.h\""
        "#include \"PluginIntf.h\"\n#include \"krkrvita/buffered_units.hpp\"\n#include \"krkrvita/retail_bootstrap.hpp\""
        yuri_save_struct_patched "${yuri_save_struct_text}")
    set(yuri_save_struct_fields_old [=[
	tTJSBinaryStream *stream;
	const tjs_char *_newline;
]=])
    set(yuri_save_struct_fields_new [=[
	tTJSBinaryStream *stream;
	const tjs_char *_newline;
	krkrvita::BufferedUnits<tjs_char, 4096> buffer;
	void writeBuffered(const tjs_char *data, size_t count);

public:
	void flush();
]=])
    string(REPLACE "${yuri_save_struct_fields_old}"
        "${yuri_save_struct_fields_new}"
        yuri_save_struct_patched "${yuri_save_struct_patched}")
    set(yuri_save_struct_writes_old [=[
void tTVPStringStream::write(tjs_char c) {
	stream->Write(&c, sizeof(c));
}

void tTVPStringStream::write(const tjs_char *s) {
	stream->Write(s, TJS_strlen(s) * sizeof(*s));
}
]=])
    set(yuri_save_struct_writes_new [=[
void tTVPStringStream::writeBuffered(const tjs_char *data, size_t count) {
	const size_t bytes = count * sizeof(tjs_char);
	if(stream->Write(data, static_cast<tjs_uint>(bytes)) != bytes)
		TVPThrowExceptionMessage(TJS_W("Cannot write saveStruct stream"));
}

void tTVPStringStream::write(tjs_char c) {
	auto sink = [this](const tjs_char *data, size_t count) {
		writeBuffered(data, count);
	};
	buffer.append(c, sink);
}

void tTVPStringStream::write(const tjs_char *s) {
	if(!s) return;
	auto sink = [this](const tjs_char *data, size_t count) {
		writeBuffered(data, count);
	};
	buffer.append(s, TJS_strlen(s), sink);
}

void tTVPStringStream::flush() {
	auto sink = [this](const tjs_char *data, size_t count) {
		writeBuffered(data, count);
	};
	buffer.flush_to(sink);
}
]=])
    string(REPLACE "${yuri_save_struct_writes_old}"
        "${yuri_save_struct_writes_new}"
        yuri_save_struct_patched "${yuri_save_struct_patched}")
    # Numeric formatting must join the same ordered buffer. Leaving either
    # overload on the raw stream lets a number overtake preceding punctuation
    # or dictionary text which has not filled the buffer yet.
    string(REPLACE
        "stream->Write(s.c_str(), s.length() * sizeof(tjs_char));"
        "write(s.c_str());"
        yuri_save_struct_patched "${yuri_save_struct_patched}")
    set(yuri_save_struct_before_flush "${yuri_save_struct_patched}")
    string(REPLACE
        "        delete stream;"
        "        writer.flush();\n        delete stream;"
        yuri_save_struct_patched "${yuri_save_struct_patched}")
    string(REPLACE
        "            *result = (const tjs_char*)ms.GetInternalBuffer();"
        "            writer.flush();\n            *result = (const tjs_char*)ms.GetInternalBuffer();"
        yuri_save_struct_patched "${yuri_save_struct_patched}")
    # Dictionary::toStructString appends its terminating NUL after the common
    # serialization call. Flush that final unit as well before exposing the
    # memory-stream buffer to TJS.
    string(REPLACE
        "            writer.write((tjs_char)0);\n\t\t\t*result = (const tjs_char*)ms.GetInternalBuffer();"
        "            writer.write((tjs_char)0);\n            writer.flush();\n\t\t\t*result = (const tjs_char*)ms.GetInternalBuffer();"
        yuri_save_struct_patched "${yuri_save_struct_patched}")
    set(yuri_save_struct_registration_old [=[
		ArrayCountProp = val.AsObject();
	}
}
]=])
    set(yuri_save_struct_registration_new [=[
		ArrayCountProp = val.AsObject();
	}
	krkrvita_boot_trace("yuri-savestruct-buffered-ready");
}
]=])
    string(REPLACE
        "${yuri_save_struct_registration_old}"
        "${yuri_save_struct_registration_new}"
        yuri_save_struct_patched "${yuri_save_struct_patched}")
    if(yuri_save_struct_patched STREQUAL yuri_save_struct_before_flush)
        message(FATAL_ERROR "Yuri saveStruct buffered flush patch no longer applies")
    endif()
    foreach(yuri_save_struct_contract IN ITEMS
            "krkrvita::BufferedUnits<tjs_char, 4096> buffer;"
            "buffer.append(c, sink);"
            "buffer.append(s, TJS_strlen(s), sink);"
            "buffer.flush_to(sink);"
            "write(s.c_str());"
            "yuri-savestruct-buffered-ready")
        string(FIND "${yuri_save_struct_patched}"
            "${yuri_save_struct_contract}" yuri_save_struct_contract_offset)
        if(yuri_save_struct_contract_offset LESS 0)
            message(FATAL_ERROR
                "Yuri saveStruct buffering contract is incomplete: ${yuri_save_struct_contract}")
        endif()
    endforeach()
    set(yuri_save_struct_generated "${yuri_generated_dir}/saveStruct.cpp")
    file(CONFIGURE OUTPUT "${yuri_save_struct_generated}"
        CONTENT "${yuri_save_struct_patched}" @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_plugin_sources "${yuri_save_struct_plugin}")
    list(APPEND yuri_plugin_sources "${yuri_save_struct_generated}")

    set(yuri_xp3filter_plugin
        "${KRKRVITA_YURI_SOURCE_DIR}/src/plugins/xp3filter.cpp")
    file(READ "${yuri_xp3filter_plugin}" yuri_xp3filter_plugin_text)
    string(REPLACE
        "    ttstr path = TVPGetAppPath() + TJS_W(\"xp3filter.tjs\");"
        "    ttstr path;\n    tTJSVariant filter_option;\n    if (TVPGetCommandLine(TJS_W(\"-xp3filter\"), &filter_option))\n        path = ttstr(filter_option);\n    else\n        path = TVPGetAppPath() + TJS_W(\"xp3filter.tjs\");"
        yuri_xp3filter_plugin_patched "${yuri_xp3filter_plugin_text}")
    if(yuri_xp3filter_plugin_patched STREQUAL yuri_xp3filter_plugin_text)
        message(FATAL_ERROR "Yuri external xp3filter path patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_generated_dir}/xp3filter.cpp"
        CONTENT "${yuri_xp3filter_plugin_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(REMOVE_ITEM yuri_plugin_sources "${yuri_xp3filter_plugin}")
    list(APPEND yuri_plugin_sources "${yuri_generated_dir}/xp3filter.cpp")
    list(APPEND yuri_plugin_sources
        "${KRKRVITA_YURI_SOURCE_DIR}/src/plugins/ncbind/ncbind.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_fstat_module.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/rsa_pss_signature.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_sigcheck_module.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_psbfile_module.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_sqlite_module.cpp")

    if(NOT IS_DIRECTORY "${KRKRVITA_EXTRANS_SOURCE_DIR}")
        message(FATAL_ERROR "Pinned Kirikiri extrans sources are unavailable")
    endif()
    set(yuri_extrans_generated_dir "${yuri_generated_dir}/extrans")
    file(MAKE_DIRECTORY "${yuri_extrans_generated_dir}")
    set(yuri_extrans_sources)
    foreach(extrans_unit
        wave.cpp
        mosaic.cpp
        turn.cpp
        turntrans_table.cpp
        rotatebase.cpp
        rotatetrans.cpp
        ripple.cpp)
        file(READ "${KRKRVITA_EXTRANS_SOURCE_DIR}/${extrans_unit}"
            extrans_text)
        string(REPLACE "#include <windows.h>" ""
            extrans_text "${extrans_text}")
        string(REPLACE "#include \"tp_stub.h\""
            "#include \"krkrvita_extrans_compat.hpp\""
            extrans_text "${extrans_text}")
        string(REPLACE "data->Dest->GetScanLineForWrite("
            "krkrvita_extrans_scanline_for_write(data->Dest, "
            extrans_text "${extrans_text}")
        string(REPLACE "data->Src1->GetScanLine("
            "krkrvita_extrans_scanline_for_read(data->Src1, "
            extrans_text "${extrans_text}")
        string(REPLACE "data->Src2->GetScanLine("
            "krkrvita_extrans_scanline_for_read(data->Src2, "
            extrans_text "${extrans_text}")
        string(REPLACE "data->Dest->GetPitchBytes("
            "krkrvita_extrans_pitch(data->Dest, "
            extrans_text "${extrans_text}")
        string(REPLACE "data->Src1->GetPitchBytes("
            "krkrvita_extrans_pitch(data->Src1, "
            extrans_text "${extrans_text}")
        string(REPLACE "data->Src2->GetPitchBytes("
            "krkrvita_extrans_pitch(data->Src2, "
            extrans_text "${extrans_text}")
        if(extrans_unit STREQUAL "ripple.cpp")
            string(REPLACE "#include <intrin.h>" ""
                extrans_text "${extrans_text}")
            string(FIND "${extrans_text}"
                "static void TVPRippleTransform_sse2_f(" ripple_x86_start)
            string(FIND "${extrans_text}"
                "typedef void (*tTVPRippleTransformFunc)(" ripple_x86_end)
            if(ripple_x86_start LESS 0 OR ripple_x86_end LESS 0 OR
               ripple_x86_end LESS_EQUAL ripple_x86_start)
                message(FATAL_ERROR "Kirikiri extrans ripple SIMD boundary moved")
            endif()
            string(SUBSTRING "${extrans_text}" 0 ${ripple_x86_start}
                ripple_portable_prefix)
            string(SUBSTRING "${extrans_text}" ${ripple_x86_end} -1
                ripple_portable_suffix)
            set(extrans_text
                "${ripple_portable_prefix}${ripple_portable_suffix}")
            set(ripple_init_old [=[
static void TVPInitRippleTransformFuncs()
{
	tjs_uint32 cputype = TVPGetCPUType();
#ifndef _M_X64
	if(cputype & TVP_CPU_HAS_MMX)
	{
		// MMX が使用可能な場合
		TVPRippleTransform_f = TVPRippleTransform_mmx_f;
		TVPRippleTransform_b = TVPRippleTransform_mmx_b;
	}

	if((cputype & TVP_CPU_HAS_MMX) && (cputype & TVP_CPU_HAS_EMMX))
	{
		// MMX/EMMX が使用可能な場合
		// EMMX バージョンは MMX バージョンに prefetch 命令を追加しただけだが
		// 微妙に速い
		TVPRippleTransform_f = TVPRippleTransform_emmx_f;
		TVPRippleTransform_b = TVPRippleTransform_emmx_b;
	}
#endif
	if(cputype & TVP_CPU_HAS_SSE2)
	{
		// SSE2 が使用可能な場合
		TVPRippleTransform_f = TVPRippleTransform_sse2_f;
		TVPRippleTransform_b = TVPRippleTransform_sse2_b;
	}
}
]=])
            set(ripple_init_portable [=[
static void TVPInitRippleTransformFuncs()
{
	// The Vita ARM backend deliberately uses the portable reference kernels.
	TVPRippleTransform_f = TVPRippleTransform_c_f;
	TVPRippleTransform_b = TVPRippleTransform_c_b;
}
]=])
            string(REPLACE "${ripple_init_old}" "${ripple_init_portable}"
                extrans_ripple_patched "${extrans_text}")
            if(extrans_ripple_patched STREQUAL extrans_text)
                message(FATAL_ERROR "Kirikiri extrans ripple init patch no longer applies")
            endif()
            set(extrans_text "${extrans_ripple_patched}")
        endif()
        set(extrans_output "${yuri_extrans_generated_dir}/${extrans_unit}")
        file(CONFIGURE OUTPUT "${extrans_output}" CONTENT "${extrans_text}"
            @ONLY NEWLINE_STYLE UNIX)
        list(APPEND yuri_extrans_sources "${extrans_output}")
    endforeach()
    list(APPEND yuri_plugin_sources
        ${yuri_extrans_sources}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_extrans_module.cpp")

    # extNagano is a closed-source KAGEX transition plug-in.  Its public
    # contract is the documented provider-name set; the Vita backend links a
    # Yuri crossfade fallback for those names so titles remain runnable while
    # preserving the ordinary transition timing/options path.
    list(APPEND yuri_plugin_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_extnagano_module.cpp")

    # krflash.dll is a closed-source Windows ActiveX bridge.  A number of
    # games link it during startup even when their active script path never
    # instantiates FlashPlayer.  Keep that load-only case runnable, while the
    # host compatibility audit still reports actual Flash playback as a
    # feature gap.
    list(APPEND yuri_plugin_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_krflash_module.cpp")

    # gfxEffect.dll exposes the documented gfxFire object.  The Vita module
    # preserves its script-facing state and call surface; the proprietary
    # fire-pixel kernel is intentionally a no-op fallback.
    list(APPEND yuri_plugin_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_gfxeffect_module.cpp")

    # KAGEX motion scripts catch the DLL load and then use
    # Motion.ResourceManager/Player immediately.  Keep the script-visible
    # compatibility surface registered even though the full PSB/E-mote pixel
    # renderer is not yet part of the Vita backend.
    list(APPEND yuri_plugin_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_motionplayer_module.cpp")

    # layerExDraw is a Windows/GDI+ plug-in whose KAGEX script surface is used
    # after a caught load failure.  Register a conservative Vita fallback so
    # those globals remain callable during startup; its image operations are
    # still no-ops, which the compatibility gate must keep reporting.
    list(APPEND yuri_plugin_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_layerexdraw_module.cpp")

    # scriptsEx is the portable upstream implementation fetched from
    # KrKr2-Next; it attaches to Kirikiri's built-in Scripts class.
    # The companion module only records the boot trace.
    list(APPEND yuri_plugin_sources
        "${KRKRVITA_KRKR2_NEXT_SOURCE_DIR}/cpp/plugins/scriptsEx.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_scriptsex_module.cpp")

    # layerExBTOA is likewise the portable upstream implementation fetched from
    # KrKr2-Next. It attaches to Kirikiri's built-in Layer class
    # and computes real alpha/province pixels; the companion module only
    # records the boot trace. Titles link it without a try/catch, so an absent
    # module ends the boot rather than costing an optional effect.
    list(APPEND yuri_plugin_sources
        "${KRKRVITA_KRKR2_NEXT_SOURCE_DIR}/cpp/plugins/layerExBTOA.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_layerexbtoa_module.cpp")

    # The debloated Yuri Android binary used as the compatibility oracle ships
    # layerExSave even though Yuri's public source tree omits it. Import the
    # original backend implementation needed by this game's screenshot/save
    # paths. Its asynchronous Window helper depends on the Android frontend's
    # message bridge, so keep the complete synchronous Layer API (PNG, TLG5,
    # province and image utilities) at this backend-only milestone.
    if(NOT IS_DIRECTORY "${KRKRVITA_LAYEREXSAVE_SOURCE_DIR}")
        message(FATAL_ERROR "Pinned Kirikiri layerExSave sources are unavailable")
    endif()
    set(yuri_layerexsave_generated_dir "${yuri_generated_dir}/layerExSave")
    file(MAKE_DIRECTORY "${yuri_layerexsave_generated_dir}/LodePNG")
    file(MAKE_DIRECTORY "${yuri_layerexsave_generated_dir}/tlg5")
    foreach(layerexsave_header
        compress.hpp
        savepng.hpp
        savetlg5.hpp
        utils.hpp)
        file(READ "${KRKRVITA_LAYEREXSAVE_SOURCE_DIR}/${layerexsave_header}"
            layerexsave_header_text)
        string(REPLACE "iTJSBinaryStream" "tTJSBinaryStream"
            layerexsave_header_text "${layerexsave_header_text}")
        string(REPLACE "out->Destruct();" "delete out;"
            layerexsave_header_text "${layerexsave_header_text}")
        file(CONFIGURE
            OUTPUT "${yuri_layerexsave_generated_dir}/${layerexsave_header}"
            CONTENT "${layerexsave_header_text}" @ONLY NEWLINE_STYLE UNIX)
    endforeach()
    set(yuri_layerexsave_sources)
    foreach(layerexsave_unit
        savepng.cpp
        savetlg5.cpp
        utils.cpp
        LodePNG/lodepng.cpp
        tlg5/slide.cpp)
        file(READ "${KRKRVITA_LAYEREXSAVE_SOURCE_DIR}/${layerexsave_unit}"
            layerexsave_text)
        string(REPLACE "iTJSBinaryStream" "tTJSBinaryStream"
            layerexsave_text "${layerexsave_text}")
        string(REPLACE "out->Destruct();" "delete out;"
            layerexsave_text "${layerexsave_text}")
        if(layerexsave_unit STREQUAL "tlg5/slide.cpp")
            string(REPLACE
                "i < SLIDE_N + SLIDE_M; i++) Text[i] = 0;"
                "i < SLIDE_N + SLIDE_M - 1; i++) Text[i] = 0;"
                layerexsave_safe "${layerexsave_text}")
            if(layerexsave_safe STREQUAL layerexsave_text)
                message(FATAL_ERROR "layerExSave TLG5 bounds patch no longer applies")
            endif()
            set(layerexsave_text "${layerexsave_safe}")
        elseif(layerexsave_unit STREQUAL "LodePNG/lodepng.cpp")
            # The old decoder uses 32-bit cursors for a size_t-sized chunk;
            # it also advances past absent iTXt terminators and can wrap
            # length+1 before ucvector_resize. Keep cursors in size_t, clamp
            # optional terminators to the chunk boundary, and reject the only
            # value whose allocation size cannot be represented.
            string(REPLACE
                "unsigned length, begin, compressed;"
                "size_t length, begin;\n  unsigned compressed;"
                layerexsave_safe "${layerexsave_text}")
            if(layerexsave_safe STREQUAL layerexsave_text)
                message(FATAL_ERROR "layerExSave LodePNG size patch no longer applies")
            endif()
            set(layerexsave_text "${layerexsave_safe}")
            string(REPLACE
                "begin += length + 1;\n    length = 0;"
                "begin += length;\n    if(begin < chunkLength) ++begin;\n    length = 0;"
                layerexsave_safe "${layerexsave_text}")
            if(layerexsave_safe STREQUAL layerexsave_text)
                message(FATAL_ERROR "layerExSave iTXt langtag bounds patch no longer applies")
            endif()
            set(layerexsave_text "${layerexsave_safe}")
            string(REPLACE
                "begin += length + 1;\n\n    length = chunkLength < begin ? 0 : chunkLength - begin;"
                "begin += length;\n    if(begin < chunkLength) ++begin;\n\n    length = chunkLength - begin;"
                layerexsave_safe "${layerexsave_text}")
            if(layerexsave_safe STREQUAL layerexsave_text)
                message(FATAL_ERROR "layerExSave iTXt transkey bounds patch no longer applies")
            endif()
            set(layerexsave_text "${layerexsave_safe}")
            string(REPLACE
                "if(!ucvector_resize(&decoded, length + 1)) CERROR_BREAK(error, 83 /*alloc fail*/);"
                "if(length == (size_t)-1) CERROR_BREAK(error, 83 /*alloc fail*/);\n      if(!ucvector_resize(&decoded, length + 1)) CERROR_BREAK(error, 83 /*alloc fail*/);"
                layerexsave_safe "${layerexsave_text}")
            if(layerexsave_safe STREQUAL layerexsave_text)
                message(FATAL_ERROR "layerExSave iTXt allocation patch no longer applies")
            endif()
            set(layerexsave_text "${layerexsave_safe}")
        endif()
        if(layerexsave_unit MATCHES "^(savepng|savetlg5|utils)\\.cpp$")
            set(layerexsave_text
                "#define NCB_MODULE_NAME TJS_W(\"layerExSave.dll\")\n${layerexsave_text}")
        endif()
        set(layerexsave_output
            "${yuri_layerexsave_generated_dir}/${layerexsave_unit}")
        file(CONFIGURE OUTPUT "${layerexsave_output}"
            CONTENT "${layerexsave_text}" @ONLY NEWLINE_STYLE UNIX)
        list(APPEND yuri_layerexsave_sources "${layerexsave_output}")
    endforeach()
    list(APPEND yuri_plugin_sources
        ${yuri_layerexsave_sources}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_layerexsave_probe.cpp")

    # KAGEX games commonly load layerExImage.dll unconditionally while
    # booting. Import its published implementation as an internal module; a
    # successful Plugins.link must install the real Layer methods rather than
    # becoming a platform no-op. Yuri exposes direct pixel access through its
    # layerExBase_GL adapter, which is the equivalent of the original plugin's
    # mainImageBufferForWrite path when the software renderer is selected.
    if(NOT IS_DIRECTORY "${KRKRVITA_LAYEREXIMAGE_SOURCE_DIR}")
        message(FATAL_ERROR "Pinned Kirikiri layerExImage sources are unavailable")
    endif()
    set(yuri_layereximage_generated_dir
        "${yuri_generated_dir}/layerExImage")
    file(MAKE_DIRECTORY "${yuri_layereximage_generated_dir}")

    file(READ "${KRKRVITA_LAYEREXIMAGE_SOURCE_DIR}/LayerExImage.h"
        layereximage_header_text)
    string(REPLACE
        "class layerExImage : public layerExBase"
        "class layerExImage : public layerExBase_GL"
        layereximage_header_patched "${layereximage_header_text}")
    if(layereximage_header_patched STREQUAL layereximage_header_text)
        message(FATAL_ERROR "layerExImage Yuri layer adapter patch no longer applies")
    endif()
    string(REPLACE
        "layerExImage(DispatchT obj) : layerExBase(obj) {}"
        "layerExImage(DispatchT obj) : layerExBase_GL(obj) {}"
        layereximage_header_constructor "${layereximage_header_patched}")
    if(layereximage_header_constructor STREQUAL layereximage_header_patched)
        message(FATAL_ERROR "layerExImage Yuri constructor patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_layereximage_generated_dir}/LayerExImage.h"
        CONTENT "${layereximage_header_constructor}"
        @ONLY NEWLINE_STYLE UNIX)

    file(READ "${KRKRVITA_LAYEREXIMAGE_SOURCE_DIR}/LayerExImage.cpp"
        layereximage_impl_text)
    string(REPLACE
        "layerExBase::reset();"
        "layerExBase_GL::reset();"
        layereximage_impl_patched "${layereximage_impl_text}")
    if(layereximage_impl_patched STREQUAL layereximage_impl_text)
        message(FATAL_ERROR "layerExImage reset adapter patch no longer applies")
    endif()
    string(REPLACE
        "typedef unsigned short WORD;\n#endif"
        "typedef unsigned short WORD;\ntypedef struct tagRGBQUAD {\n\tBYTE rgbBlue;\n\tBYTE rgbGreen;\n\tBYTE rgbRed;\n\tBYTE rgbReserved;\n} RGBQUAD;\n#endif"
        layereximage_impl_types "${layereximage_impl_patched}")
    if(layereximage_impl_types STREQUAL layereximage_impl_patched)
        message(FATAL_ERROR "layerExImage portable RGBQUAD patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_layereximage_generated_dir}/LayerExImage.cpp"
        CONTENT "${layereximage_impl_types}"
        @ONLY NEWLINE_STYLE UNIX)

    file(READ "${KRKRVITA_LAYEREXIMAGE_SOURCE_DIR}/Main.cpp"
        layereximage_main_text)
    set(layereximage_main_text
        "#define NCB_MODULE_NAME TJS_W(\"layerExImage.dll\")\n#include \"krkrvita/retail_bootstrap.hpp\"\n${layereximage_main_text}")
    string(REPLACE
        "\tTVPAddImportantLog(ttstr(copyright));"
        "\tTVPAddImportantLog(ttstr(copyright));\n\tkrkrvita_boot_trace(\"retail-layereximage-ready\");"
        layereximage_main_patched "${layereximage_main_text}")
    if(layereximage_main_patched STREQUAL layereximage_main_text)
        message(FATAL_ERROR "layerExImage registration marker patch no longer applies")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_layereximage_generated_dir}/Main.cpp"
        CONTENT "${layereximage_main_patched}"
        @ONLY NEWLINE_STYLE UNIX)
    list(APPEND yuri_plugin_sources
        "${yuri_layereximage_generated_dir}/LayerExImage.cpp"
        "${yuri_layereximage_generated_dir}/Main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_shrink_copy.cpp")

    # Squirrel 2.2.4 is used by KAGEX ``sq``/``wsq`` tags. The upstream
    # plugin assumes Win32's 16-bit wchar_t;
    # Yuri's Vita TJS ABI instead uses char16_t.  Generate a narrow source
    # overlay which keeps the upstream VM/bridge code intact while replacing
    # only the character-library and Windows-header boundary.
    if(NOT IS_DIRECTORY "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel")
        message(FATAL_ERROR "Pinned Kirikiri Squirrel sources are unavailable")
    endif()
    set(yuri_squirrel_generated_dir "${yuri_generated_dir}/squirrel")
    set(yuri_squirrel_overlay_dir "${yuri_squirrel_generated_dir}/vm")
    file(MAKE_DIRECTORY "${yuri_squirrel_generated_dir}/include")
    file(MAKE_DIRECTORY "${yuri_squirrel_overlay_dir}")

    file(READ "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/include/squirrel.h"
        yuri_squirrel_header_text)
    string(REPLACE
        "#define _SQUIRREL_H_"
        "#define _SQUIRREL_H_\n#include \"yuri_squirrel_compat.hpp\""
        yuri_squirrel_header_text "${yuri_squirrel_header_text}")
    string(REPLACE "#ifdef _UNICODE"
        "#if defined(_UNICODE) || defined(KRKRVITA_SQUIRREL_UNICODE)"
        yuri_squirrel_header_text "${yuri_squirrel_header_text}")
    string(REPLACE "typedef wchar_t SQChar;" "typedef char16_t SQChar;"
        yuri_squirrel_header_text "${yuri_squirrel_header_text}")
    # The upstream header emulates a 16-bit wchar_t on MSVC.  GCC already
    # has a built-in wchar_t, so retain only the SQChar alias on Vita.
    string(REPLACE "#define wchar_t unsigned short" ""
        yuri_squirrel_header_text "${yuri_squirrel_header_text}")
    string(REPLACE "typedef unsigned short wchar_t;" ""
        yuri_squirrel_header_text "${yuri_squirrel_header_text}")
    foreach(yuri_squirrel_macro IN ITEMS
            "#define _SC(a) L##a|#define _SC(a) u##a"
            "#define\tscstrcmp\twcscmp|#define\tscstrcmp\tkrkrvita::squirrel::strcmp16"
            "#define\tscstrncmp\twcsncmp|#define\tscstrncmp\tkrkrvita::squirrel::strncmp16"
            "#define scsprintf\tswprintf|#define scsprintf\tkrkrvita::squirrel::sprintf16"
            "#define scstrlen\twcslen|#define scstrlen\tkrkrvita::squirrel::strlen16"
            "#define scstrtod\twcstod|#define scstrtod\tkrkrvita::squirrel::strtod16"
            "#define scstrtol\twcstol|#define scstrtol\tkrkrvita::squirrel::strtol16"
            "#define scatoi\t\t_wtoi|#define scatoi\t\tkrkrvita::squirrel::atoi16"
            "#define scstrtoul\twcstoul|#define scstrtoul\tkrkrvita::squirrel::strtoul16"
            "#define scvsprintf\tvswprintf|#define scvsprintf\tkrkrvita::squirrel::vsprintf_unbounded16"
            "#define scvsnprintf\tvsnwprintf|#define scvsnprintf\tkrkrvita::squirrel::vsprintf16"
            "#define scstrstr\twcsstr|#define scstrstr\tkrkrvita::squirrel::strstr16"
            "#define scisspace\tiswspace|#define scisspace\tkrkrvita::squirrel::isspace16"
            "#define scisdigit\tiswdigit|#define scisdigit\tkrkrvita::squirrel::isdigit16"
            "#define scisxdigit\tiswxdigit|#define scisxdigit\tkrkrvita::squirrel::isxdigit16"
            "#define scisalpha\tiswalpha|#define scisalpha\tkrkrvita::squirrel::isalpha16"
            "#define sciscntrl\tiswcntrl|#define sciscntrl\tkrkrvita::squirrel::iscntrl16"
            "#define scisalnum\tiswalnum|#define scisalnum\tkrkrvita::squirrel::isalnum16"
            "#define scprintf\twprintf|#define scprintf\tkrkrvita::squirrel::printf16")
        string(REPLACE "|" ";" yuri_squirrel_macro_parts
            "${yuri_squirrel_macro}")
        list(GET yuri_squirrel_macro_parts 0 yuri_squirrel_macro_old)
        list(GET yuri_squirrel_macro_parts 1 yuri_squirrel_macro_new)
        string(REPLACE "${yuri_squirrel_macro_old}" "${yuri_squirrel_macro_new}"
            yuri_squirrel_header_text "${yuri_squirrel_header_text}")
    endforeach()
    if(yuri_squirrel_header_text MATCHES "typedef wchar_t SQChar|wcscmp|vsnwprintf")
        message(FATAL_ERROR "Squirrel UTF-16 compatibility patch is incomplete")
    endif()
    file(CONFIGURE
        OUTPUT "${yuri_squirrel_generated_dir}/include/squirrel.h"
        CONTENT "${yuri_squirrel_header_text}" @ONLY NEWLINE_STYLE UNIX)

    # The pinned sqobject helper has an MSVC-only typo in pushInstance
    # (references vm/idx instead of its v/stack arguments).  Keep the
    # authoritative implementation, but provide a tiny generated overlay so
    # strict GCC/Clang and Vita builds compile the same behavior.
    file(READ "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/sqobject/sqfunc.h"
        yuri_squirrel_sqfunc_text)
    string(REPLACE "sq_remove(vm, -2);" "sq_remove(v, -2);"
        yuri_squirrel_sqfunc_text "${yuri_squirrel_sqfunc_text}")
    string(REPLACE "value->initSelf(vm, idx);" "value->initSelf(v, -1);"
        yuri_squirrel_sqfunc_text "${yuri_squirrel_sqfunc_text}")
    string(REPLACE "sq_pop(vm, 1);" "sq_pop(v, 1);"
        yuri_squirrel_sqfunc_text "${yuri_squirrel_sqfunc_text}")
    file(CONFIGURE OUTPUT "${yuri_squirrel_overlay_dir}/sqfunc.h"
        CONTENT "${yuri_squirrel_sqfunc_text}" @ONLY NEWLINE_STYLE UNIX)
    file(READ "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/sqobject/sqobjectclass.h"
        yuri_squirrel_sqobjectclass_text)
    string(REPLACE "#include \"sqfunc.h\"" "#include <sqfunc.h>"
        yuri_squirrel_sqobjectclass_text "${yuri_squirrel_sqobjectclass_text}")
    file(CONFIGURE OUTPUT "${yuri_squirrel_overlay_dir}/sqobjectclass.h"
        CONTENT "${yuri_squirrel_sqobjectclass_text}" @ONLY NEWLINE_STYLE UNIX)
    file(READ "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/sqobject/sqthread.h"
        yuri_squirrel_sqthread_text)
    string(REPLACE "#include \"sqobjectclass.h\"" "#include <sqobjectclass.h>"
        yuri_squirrel_sqthread_text "${yuri_squirrel_sqthread_text}")
    file(CONFIGURE OUTPUT "${yuri_squirrel_overlay_dir}/sqthread.h"
        CONTENT "${yuri_squirrel_sqthread_text}" @ONLY NEWLINE_STYLE UNIX)

    file(READ "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/include/sqstdio.h"
        yuri_squirrel_sqstdio_header_text)
    file(CONFIGURE
        OUTPUT "${yuri_squirrel_generated_dir}/include/sqstdio.h"
        CONTENT "${yuri_squirrel_sqstdio_header_text}" @ONLY NEWLINE_STYLE UNIX)
    foreach(yuri_squirrel_public_header IN ITEMS
            sqstdaux.h sqstdblob.h sqstdmath.h sqstdstring.h sqstdsystem.h)
        file(READ
            "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/include/${yuri_squirrel_public_header}"
            yuri_squirrel_public_header_text)
        file(CONFIGURE
            OUTPUT "${yuri_squirrel_generated_dir}/include/${yuri_squirrel_public_header}"
            CONTENT "${yuri_squirrel_public_header_text}" @ONLY NEWLINE_STYLE UNIX)
    endforeach()

    file(READ "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/include/squirrel.h"
        yuri_squirrel_header_probe)
    set(yuri_squirrel_wrapper_sources)
    foreach(yuri_squirrel_plugin_unit IN ITEMS
            Main.cpp sqfile.cpp sqstdio.cpp sqtjsobj.cpp sqwrapper.cpp)
        file(READ "${KRKRVITA_SQUIRREL_SOURCE_DIR}/${yuri_squirrel_plugin_unit}"
            yuri_squirrel_plugin_text)
        string(REPLACE "#include <windows.h>" ""
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "#include <tchar.h>" "#include <cstdarg>"
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "#include \"tp_stub.h\""
            "#include \"tp_stub.h\"\n#include \"combase.h\""
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        if(yuri_squirrel_plugin_unit STREQUAL "Main.cpp")
            string(REPLACE "#include \"ncbind/ncbind.hpp\""
                "#include \"combase.h\"\n#include \"PluginImpl.h\"\n#include \"EventIntf.h\"\n#include \"krkrvita/retail_bootstrap.hpp\"\n#include \"ncbind/ncbind.hpp\""
                yuri_squirrel_main_injected "${yuri_squirrel_plugin_text}")
            if(yuri_squirrel_main_injected STREQUAL yuri_squirrel_plugin_text)
                message(FATAL_ERROR "Squirrel Main.cpp registration include patch no longer applies")
            endif()
            set(yuri_squirrel_plugin_text "${yuri_squirrel_main_injected}")
            string(REPLACE
                "\tTVPAddImportantLog(ttstr(copyright));"
                "\tTVPAddImportantLog(ttstr(copyright));\n\tkrkrvita_boot_trace(\"retail-squirrel-ready\");"
                yuri_squirrel_main_marked "${yuri_squirrel_plugin_text}")
            if(yuri_squirrel_main_marked STREQUAL yuri_squirrel_plugin_text)
                message(FATAL_ERROR "Squirrel Main.cpp registration marker patch no longer applies")
            endif()
            set(yuri_squirrel_plugin_text "${yuri_squirrel_main_marked}")
        endif()
        string(REPLACE "L\"" "u\""
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "#include \"../json/Writer.hpp\""
            "#include <Writer.hpp>"
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "_vsntprintf_s(msg, 1024, _TRUNCATE, format, args);"
            "krkrvita::squirrel::vsprintf16(msg, 1024, format, args);"
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "TCHAR msg[1024];" "SQChar msg[1024];"
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "TCHAR *p = msg;" "SQChar *p = msg;"
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "wchar_t buf[256];" "char16_t buf[256];"
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "swprintf(buf, 255, u\"\\\\u%04x\", ch);"
            "krkrvita::squirrel::sprintf16(buf, u\"\\\\u%04x\", ch);"
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "sizeof tjs_char" "sizeof(tjs_char)"
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "sizeof tTJSVariant" "sizeof(tTJSVariant)"
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        string(REPLACE "wcscmp(classname, SQUIRRELOBJCLASS)"
            "krkrvita::squirrel::strcmp16(classname, SQUIRRELOBJCLASS)"
            yuri_squirrel_plugin_text "${yuri_squirrel_plugin_text}")
        if(yuri_squirrel_plugin_unit STREQUAL "Main.cpp")
            set(yuri_squirrel_plugin_text
                "#define NCB_MODULE_NAME TJS_W(\"squirrel.dll\")\n${yuri_squirrel_plugin_text}")
        endif()
        set(yuri_squirrel_plugin_output
            "${yuri_squirrel_generated_dir}/krkrvita_${yuri_squirrel_plugin_unit}")
        file(CONFIGURE OUTPUT "${yuri_squirrel_plugin_output}"
            CONTENT "${yuri_squirrel_plugin_text}" @ONLY NEWLINE_STYLE UNIX)
        list(APPEND yuri_squirrel_wrapper_sources "${yuri_squirrel_plugin_output}")
    endforeach()

    file(GLOB yuri_squirrel_vm_sources CONFIGURE_DEPENDS
        "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/squirrel/*.cpp"
        "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/sqobject/*.cpp"
        "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/sqstdlib/*.cpp")
    if(NOT yuri_squirrel_vm_sources)
        message(FATAL_ERROR "Pinned Squirrel VM source list is empty")
    endif()
    # Relocate the VM translation units into a generated overlay directory so
    # quote-includes from sqobject sources resolve the patched sqfunc/thread
    # headers before the unmodified upstream copies.  sqratfunc.cpp is an
    # optional C++ binding excluded by the upstream NOUSESQRAT build.
    set(yuri_squirrel_vm_overlay_sources)
    set(yuri_squirrel_vm_overlay_index 0)
    foreach(yuri_squirrel_vm_source IN LISTS yuri_squirrel_vm_sources)
        get_filename_component(yuri_squirrel_vm_name
            "${yuri_squirrel_vm_source}" NAME)
        # The plugin's Kirikiri-specific sqfile/sqstdio units replace the
        # generic Squirrel standard-library implementations.  Linking both
        # copies produces duplicate exported VM symbols; retain the upstream
        # source numbering while omitting only those two generic units.
        math(EXPR yuri_squirrel_vm_overlay_index
            "${yuri_squirrel_vm_overlay_index} + 1")
        if(yuri_squirrel_vm_name STREQUAL "sqratfunc.cpp")
            continue()
        endif()
        if(yuri_squirrel_vm_name STREQUAL "sqfile.cpp" OR
           yuri_squirrel_vm_name STREQUAL "sqstdio.cpp")
            continue()
        endif()
        file(READ "${yuri_squirrel_vm_source}" yuri_squirrel_vm_text)
        # The upstream VM uses the Windows wide-C runtime when SQChar is
        # wchar_t.  Vita deliberately represents SQChar as UTF-16 char16_t,
        # so route those narrow compatibility shims through the same bridge
        # used by the public headers instead of relying on Vita's wchar_t
        # (which is 32-bit on the cross toolchain).
        if(yuri_squirrel_vm_name STREQUAL "sqstdio.cpp")
            string(REPLACE "_wfopen"
                "krkrvita::squirrel::fopen16"
                yuri_squirrel_vm_text "${yuri_squirrel_vm_text}")
        elseif(yuri_squirrel_vm_name STREQUAL "sqstdstring.cpp")
            string(REPLACE "#define scstrchr wcschr"
                "#define scstrchr krkrvita::squirrel::strchr16"
                yuri_squirrel_vm_text "${yuri_squirrel_vm_text}")
            string(REPLACE "#define scsnprintf wsnprintf"
                "#define scsnprintf krkrvita::squirrel::snprintf16"
                yuri_squirrel_vm_text "${yuri_squirrel_vm_text}")
            string(REPLACE "#define scatoi _wtoi"
                "#define scatoi krkrvita::squirrel::atoi16"
                yuri_squirrel_vm_text "${yuri_squirrel_vm_text}")
            string(REPLACE "#define scstrtok wcstok"
                "#define scstrtok krkrvita::squirrel::strtok16"
                yuri_squirrel_vm_text "${yuri_squirrel_vm_text}")
        elseif(yuri_squirrel_vm_name STREQUAL "sqstdsystem.cpp")
            string(REPLACE "#define scgetenv _wgetenv"
                "#define scgetenv krkrvita::squirrel::getenv16"
                yuri_squirrel_vm_text "${yuri_squirrel_vm_text}")
            string(REPLACE "#define scsystem _wsystem"
                "#define scsystem krkrvita::squirrel::system16"
                yuri_squirrel_vm_text "${yuri_squirrel_vm_text}")
            string(REPLACE "#define scasctime _wasctime"
                "#define scasctime krkrvita::squirrel::asctime16"
                yuri_squirrel_vm_text "${yuri_squirrel_vm_text}")
            string(REPLACE "#define scremove _wremove"
                "#define scremove krkrvita::squirrel::remove16"
                yuri_squirrel_vm_text "${yuri_squirrel_vm_text}")
            string(REPLACE "#define screname _wrename"
                "#define screname krkrvita::squirrel::rename16"
                yuri_squirrel_vm_text "${yuri_squirrel_vm_text}")
        endif()
        if(yuri_squirrel_vm_name STREQUAL "sqstdio.cpp" AND
           yuri_squirrel_vm_text MATCHES "_wfopen")
            message(FATAL_ERROR "Squirrel sqstdio wide fopen patch no longer applies")
        elseif(yuri_squirrel_vm_name STREQUAL "sqstdstring.cpp" AND
               (yuri_squirrel_vm_text MATCHES "wcschr|wsnprintf|wcstok|#define scatoi _wtoi"))
            message(FATAL_ERROR "Squirrel sqstdstring UTF-16 patch no longer applies")
        elseif(yuri_squirrel_vm_name STREQUAL "sqstdsystem.cpp" AND
               (yuri_squirrel_vm_text MATCHES "_wgetenv|_wsystem|_wasctime|_wremove|_wrename"))
            message(FATAL_ERROR "Squirrel sqstdsystem UTF-16 patch no longer applies")
        endif()
        set(yuri_squirrel_vm_output
            "${yuri_squirrel_overlay_dir}/${yuri_squirrel_vm_overlay_index}_${yuri_squirrel_vm_name}")
        file(CONFIGURE OUTPUT "${yuri_squirrel_vm_output}"
            CONTENT "${yuri_squirrel_vm_text}" @ONLY NEWLINE_STYLE UNIX)
        list(APPEND yuri_squirrel_vm_overlay_sources
            "${yuri_squirrel_vm_output}")
    endforeach()
    set(yuri_squirrel_writer_source
        "${yuri_squirrel_generated_dir}/Writer.hpp")
    file(READ "${KRKRVITA_SQUIRREL_SOURCE_DIR}/../json/Writer.hpp"
        yuri_squirrel_writer_text)
    string(REPLACE "L\"" "u\"" yuri_squirrel_writer_text
        "${yuri_squirrel_writer_text}")
    set(yuri_squirrel_writer_text
        "#include \"yuri_squirrel_compat.hpp\"\n#include \"combase.h\"\n${yuri_squirrel_writer_text}")
    file(CONFIGURE OUTPUT "${yuri_squirrel_writer_source}"
        CONTENT "${yuri_squirrel_writer_text}" @ONLY NEWLINE_STYLE UNIX)
    list(APPEND yuri_plugin_sources
        ${yuri_squirrel_wrapper_sources}
        ${yuri_squirrel_vm_overlay_sources})

    # The PSD extension is the next shared KAGEX dependency after Squirrel.
    # Generate an overlay from the maintained Kirikiri implementation rather
    # than hand-writing a second PSD decoder.  Only the plugin boundary is
    # changed: Yuri already provides the same IStream/TJS/ncbind contracts.
    if(NOT IS_DIRECTORY "${KRKRVITA_PSDFILE_SOURCE_DIR}/psdparse")
        message(FATAL_ERROR "Pinned Kirikiri PSD sources are unavailable")
    endif()
    set(yuri_psd_generated_dir "${yuri_generated_dir}/psdfile")
    file(MAKE_DIRECTORY "${yuri_psd_generated_dir}")
    file(MAKE_DIRECTORY "${yuri_psd_generated_dir}/psdparse")
    file(GLOB yuri_psd_plugin_sources CONFIGURE_DEPENDS
        "${KRKRVITA_PSDFILE_SOURCE_DIR}/*.cpp")
    file(GLOB yuri_psd_parser_sources CONFIGURE_DEPENDS
        "${KRKRVITA_PSDFILE_SOURCE_DIR}/psdparse/*.cpp")
    # Vitasdk ships Boost.Iostreams headers but its mapped-file object is
    # intentionally empty.  Replace only the PSDFile local-file entry point
    # with the same parser over a persistent byte vector; archive-backed PSDs
    # continue to use the plugin's original IStream path unchanged.
    list(FILTER yuri_psd_parser_sources EXCLUDE REGEX "/psdfile\\.cpp$")
    file(GLOB yuri_psd_plugin_headers CONFIGURE_DEPENDS
        "${KRKRVITA_PSDFILE_SOURCE_DIR}/*.h"
        "${KRKRVITA_PSDFILE_SOURCE_DIR}/*.hpp")
    file(GLOB yuri_psd_parser_headers CONFIGURE_DEPENDS
        "${KRKRVITA_PSDFILE_SOURCE_DIR}/psdparse/*.h"
        "${KRKRVITA_PSDFILE_SOURCE_DIR}/psdparse/*.hpp")
    set(yuri_psd_sources
        ${yuri_psd_plugin_sources} ${yuri_psd_parser_sources}
        ${yuri_psd_plugin_headers} ${yuri_psd_parser_headers})
    if(NOT yuri_psd_sources)
        message(FATAL_ERROR "Pinned Kirikiri PSD source list is empty")
    endif()
    foreach(yuri_psd_source IN LISTS yuri_psd_sources)
        get_filename_component(yuri_psd_name "${yuri_psd_source}" NAME)
        get_filename_component(yuri_psd_parent "${yuri_psd_source}" DIRECTORY)
        file(READ "${yuri_psd_source}" yuri_psd_text)
        string(REPLACE "#include <ncbind.hpp>"
            "#include <ncbind/ncbind.hpp>"
            yuri_psd_text "${yuri_psd_text}")
        string(REPLACE "#include \"ncbind.hpp\""
            "#include \"ncbind/ncbind.hpp\""
            yuri_psd_text "${yuri_psd_text}")
        # The maintained plugin uses Yuri's COM-shaped IStream bridge for
        # both PSD input and generated layer BMP output.  The source's
        # combase include supplies the type, while StorageImpl.h supplies the
        # Vita implementation declarations (the bridge itself is emitted by
        # Yuri's generated StorageImpl.cpp).
        string(REPLACE "#include \"combase.h\""
            "#include \"combase.h\"\n#include \"StorageImpl.h\""
            yuri_psd_text "${yuri_psd_text}")
        string(REPLACE "#include \"StorageImpl.h\""
            "#include \"StorageImpl.h\"\n#include \"krkrvita/psd_stream_adapter.hpp\""
            yuri_psd_text "${yuri_psd_text}")
        string(REPLACE "#include <boost/iostreams/device/mapped_file.hpp>"
            "#include <vector>"
            yuri_psd_text "${yuri_psd_text}")
        string(REPLACE
            "boost::iostreams::mapped_file_source in;"
            "std::vector<unsigned char> in;"
            yuri_psd_text "${yuri_psd_text}")
        string(REPLACE "TVPCreateBinaryStreamAdapter(stream)"
            "krkrvita_psd_create_binary_stream(stream)"
            yuri_psd_text "${yuri_psd_text}")
        string(REPLACE "wcschr(" "TJS_strchr("
            yuri_psd_text "${yuri_psd_text}")
        if(yuri_psd_parent STREQUAL "${KRKRVITA_PSDFILE_SOURCE_DIR}")
            set(yuri_psd_output "${yuri_psd_generated_dir}/${yuri_psd_name}")
        else()
            set(yuri_psd_output "${yuri_psd_generated_dir}/psdparse/${yuri_psd_name}")
        endif()
        if(yuri_psd_name STREQUAL "main.cpp")
            string(REPLACE "#include <ncbind/ncbind.hpp>"
                "#include <ncbind/ncbind.hpp>\n#include \"krkrvita/retail_bootstrap.hpp\""
                yuri_psd_text "${yuri_psd_text}")
            string(REPLACE
                "TVPAddCompactEventHook((tTVPCompactEventCallbackIntf *)psdStorage);"
                "TVPAddCompactEventHook((tTVPCompactEventCallbackIntf *)psdStorage);\n        krkrvita_boot_trace(\"retail-psd-ready\");"
                yuri_psd_text "${yuri_psd_text}")
        endif()
        file(CONFIGURE OUTPUT "${yuri_psd_output}"
            CONTENT "${yuri_psd_text}" @ONLY NEWLINE_STYLE UNIX)
        get_filename_component(yuri_psd_ext "${yuri_psd_name}" EXT)
        if(yuri_psd_ext STREQUAL ".cpp")
            list(APPEND yuri_plugin_sources "${yuri_psd_output}")
        endif()
    endforeach()
    list(APPEND yuri_plugin_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail/yuri_psdfile_vita.cpp")

    add_library(krkrvita-yuri-plugins STATIC EXCLUDE_FROM_ALL
        ${yuri_plugin_sources}
    )
    krkrvita_set_yuri_backend_target_defaults(krkrvita-yuri-plugins)
    target_include_directories(krkrvita-yuri-plugins BEFORE PRIVATE
        "${KRKRVITA_EXTRANS_SOURCE_DIR}"
        "${yuri_layerexsave_generated_dir}"
        "${KRKRVITA_LAYEREXSAVE_SOURCE_DIR}"
        "${KRKRVITA_LAYEREXSAVE_SOURCE_DIR}/LodePNG"
        "${KRKRVITA_LAYEREXSAVE_SOURCE_DIR}/tlg5"
        "${yuri_layereximage_generated_dir}"
        "${yuri_squirrel_generated_dir}"
        "${yuri_squirrel_overlay_dir}"
        "${yuri_squirrel_generated_dir}/include"
        "${yuri_psd_generated_dir}"
        "${yuri_psd_generated_dir}/psdparse"
        "${KRKRVITA_PSDFILE_SOURCE_DIR}"
        "${KRKRVITA_PSDFILE_SOURCE_DIR}/psdparse"
        "${KRKRVITA_YURI_SOURCE_DIR}/src/core/environ"
        "${KRKRVITA_YURI_SOURCE_DIR}/src/plugins"
        "${KRKRVITA_YURI_SOURCE_DIR}/src/plugins/ncbind"
        "${KRKRVITA_SQUIRREL_SOURCE_DIR}"
        "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/include"
        "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/sqobject"
        "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/sqstdlib"
        "${KRKRVITA_SQUIRREL_SOURCE_DIR}/squirrel/squirrel"
        "${KRKRVITA_SQUIRREL_SOURCE_DIR}/../json"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/retail")
    target_compile_definitions(krkrvita-yuri-plugins PRIVATE
        KRKRVITA_SQUIRREL_UNICODE=1
        NOUSESQRAT=1
        BOOST_LITTLE_ENDIAN=1)
    set(Boost_USE_STATIC_RUNTIME ON)
    find_package(Boost COMPONENTS iostreams filesystem system REQUIRED NO_MODULE)
    target_link_libraries(krkrvita-yuri-plugins PUBLIC
        krkrvita-yuri-environ
        krkrvita-yuri-sound
        krkrvita-yuri-visual
        krkrvita-yuri-extension
        krkrvita-yuri-utils
        krkrvita-yuri-base
        krkrvita-yuri-tjs
        krkrvita-common
        krkrvita-sqlite
        crypto
        z
        Boost::iostreams
        Boost::filesystem
        Boost::system
    )

    # This target is the platform edge of the standalone Yuri backend. Keep
    # Android, Cocos, UI and SDL sources out of it by construction.
    add_library(krkrvita-yuri-vita-platform STATIC EXCLUDE_FROM_ALL
        src/platform/vita/vita_bitmap_allocator.cpp
        src/platform/vita/yuri_input.cpp
        src/platform/vita/yuri_storage_preflight.cpp
        src/platform/vita/yuri_thread_policy.cpp
        src/platform/vita/yuri_threading_self_test.cpp
        src/platform/vita/yuri_window_layer.cpp
        src/engine/vita/vitagl_presenter.cpp
    )
    krkrvita_set_yuri_backend_target_defaults(krkrvita-yuri-vita-platform)
    target_compile_definitions(krkrvita-yuri-vita-platform PRIVATE
        KRKRVITA_VITAGL=1
    )
    if(KRKRVITA_ENABLE_OPENGL_COMPOSITOR)
        target_compile_definitions(krkrvita-yuri-vita-platform PRIVATE
            KRKRVITA_YURI_OPENGL_COMPOSITOR=1
        )
    endif()
    target_link_libraries(krkrvita-yuri-vita-platform PUBLIC
        krkrvita-yuri-tjs
        SceCtrl_stub
        SceTouch_stub
    )

    add_executable(krkrvita-yuri
        src/platform/vita/yuri_main.cpp
        src/engine/vita/vita_launch.cpp
        src/engine/vita/early_boot_trace.c
    )
    krkrvita_set_yuri_backend_target_defaults(krkrvita-yuri)
    target_link_options(krkrvita-yuri PRIVATE -Wl,--strip-debug)
    target_link_libraries(krkrvita-yuri PRIVATE
        -Wl,--start-group
        -Wl,--whole-archive
        krkrvita-yuri-plugins
        -Wl,--no-whole-archive
        krkrvita-retail-resolver
        krkrvita-yuri-vita-platform
        krkrvita-yuri-environ
        krkrvita-yuri-sound
        krkrvita-yuri-visual
        krkrvita-yuri-extension
        krkrvita-yuri-utils
        krkrvita-yuri-base
        krkrvita-yuri-tjs
        vitaGL
        vitashark
        SceShaccCgExt
        SceShaccCg_stub
        taihen_stub
        mathneon
        SceKernelDmacMgr_stub
        SceIofilemgr_stub
        SceLibKernel_stub
        SceKernelThreadMgr_stub
        SceSysmem_stub
        SceRtc_stub
        SceCtrl_stub
        SceTouch_stub
        SceAudio_stub
        SceAudioIn_stub
        SceGxm_stub
        SceDisplay_stub
        SceAppMgr_stub
        SceAppUtil_stub
        SceCommonDialog_stub
        SceSysmodule_stub
        openal
        krkrvita-vita-avformat
        krkrvita-vita-avcodec
        krkrvita-vita-avutil
        krkrvita-vita-swresample
        krkrvita-vita-swscale
        mp3lame
        jpeg
        png
        webp
        sharpyuv
        freetype
        z
        bz2
        archive
        zstd
        vorbisfile
        vorbis
        ogg
        opusfile
        opus
        turbojpeg
        # VitaSDK's libstdc++ uses a weak pthread_cancel reference as its
        # __gthread_active_p probe. A normal static-library link does not pull
        # that object from libpthread, causing std::mutex construction and
        # locking to become no-ops while condition_variable still enters
        # pthread code. Established Vita ports whole-archive libpthread for
        # this reason; Yuri uses these primitives across script, render and
        # audio paths, so enforce the fix at the executable boundary.
        -Wl,--whole-archive
        pthread
        -Wl,--no-whole-archive
        m
        -Wl,--end-group
    )

    # A successful link is not enough for this port: archive ordering can
    # silently discard static native plug-ins, and accidentally adding one
    # frontend source can make a superficially valid but unusable executable.
    # Keep these checks as explicit build dependencies of every VPK.
    add_custom_target(krkrvita-yuri-check
        COMMAND "${CMAKE_COMMAND}"
            -DCOMPILE_COMMANDS=${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
            -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyReleaseCompileFlags.cmake"
        COMMAND "${CMAKE_COMMAND}"
            -DCOMPILE_COMMANDS=${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
            -DGENERATED_DIR=${yuri_generated_dir}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DYURI_SOURCE_DIR=${KRKRVITA_YURI_SOURCE_DIR}
            -DKRKR2_SOURCE_DIR=${KRKRVITA_KRKR2_NEXT_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyYuriBuild.cmake"
        COMMAND "${CMAKE_COMMAND}"
            -DELF=$<TARGET_FILE:krkrvita-yuri>
            -DNM=${CMAKE_NM}
            -DREADELF=${CMAKE_READELF}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyVitaElf.cmake"
        DEPENDS krkrvita-yuri
        COMMENT "Verifying Yuri backend source and Vita ELF contracts"
        VERBATIM
    )

    # The shader compiler lives on ur0:, outside safe homebrew's sandbox.
    vita_create_self(krkrvita-yuri.self krkrvita-yuri UNSAFE)

    if(NOT EXISTS "${KRKRVITA_PATCH_SOURCE_DIR}/patch/alldata.js")
        message(FATAL_ERROR "Pinned Kirikiroid2 patch snapshot is incomplete")
    endif()
    # vita-pack-vpk receives additions on its command line, so passing the
    # patch repository's ~1000 files individually exceeds ARG_MAX. Keep the
    # manifest directly readable for startup selection and ship the pinned
    # patch tree as one ZIP. The Vita launcher extracts only the exact
    # manifest entry into a revisioned cache before Yuri initializes.
    set(krkrvita_retail_patch_bundle
        "${CMAKE_CURRENT_BINARY_DIR}/krkrvita-retail-patches.zip")
    add_custom_command(
        OUTPUT "${krkrvita_retail_patch_bundle}"
        COMMAND "${CMAKE_COMMAND}" -E tar cf
            "${krkrvita_retail_patch_bundle}" --format=zip patch
        WORKING_DIRECTORY "${KRKRVITA_PATCH_SOURCE_DIR}"
        DEPENDS "${KRKRVITA_PATCH_SOURCE_DIR}/patch/alldata.js"
        COMMENT "Bundling pinned Kirikiroid2 retail patches"
        VERBATIM)
    add_custom_target(krkrvita-retail-patch-bundle
        DEPENDS "${krkrvita_retail_patch_bundle}")

    # Request Sony's expanded application-memory budget. This is not implied
    # by _newlib_heap_size_user: it is a PARAM.SFO contract enforced by the
    # system before eboot.bin starts. Established large VitaGL applications
    # (GTASA Vita, DaedalusX64, Flycast Vita and the so-loader boilerplate)
    # all set this exact field explicitly.
    set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
    vita_create_vpk(krkrvita-yuri.vpk KRVITA001 krkrvita-yuri.self
        VERSION 00.02
        NAME "Kirikiri Vita"
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/vita/sce_sys/icon0.png"
             sce_sys/icon0.png
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/vita/bubble/bg0.png"
             sce_sys/livearea/contents/bg0.png
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/vita/bubble/startup.png"
             sce_sys/livearea/contents/startup.png
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/vita/bubble/template.xml"
             sce_sys/livearea/contents/template.xml
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/vita/default-xp3filter.tjs"
             krkrvita/default-xp3filter.tjs
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/vita/retail-after-startup.tjs"
             krkrvita/retail-after-startup.tjs
        FILE "${KRKRVITA_PATCH_SOURCE_DIR}/patch/alldata.js"
             krkrvita/patches/alldata.js
        FILE "${krkrvita_retail_patch_bundle}"
             krkrvita/patches/patches.zip
    )
    add_dependencies(krkrvita-yuri.vpk-vpk
        krkrvita-retail-patch-bundle
        krkrvita-yuri-check)
    add_custom_command(TARGET krkrvita-yuri.vpk-vpk POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DVPK=${CMAKE_CURRENT_BINARY_DIR}/krkrvita-yuri.vpk
            -DVERIFY_DIR=${CMAKE_CURRENT_BINARY_DIR}/vpk-contract-check
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyVitaVpk.cmake"
        COMMENT "Verifying Vita package and embedded retail patch bundle"
        VERBATIM)
endfunction()

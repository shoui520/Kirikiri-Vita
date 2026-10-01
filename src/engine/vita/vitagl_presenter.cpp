#include "krkrvita/vitagl_presenter.hpp"
#include "krkrvita/frame_probe.hpp"
#include "krkrvita/presentation_surface_transaction.hpp"
#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/vita_memory_budget.hpp"
#include "krkrvita/vita_video_frame.hpp"

#include <vitaGL.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>

#include <cstdio>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

namespace {

constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 544;
// vglInitExtended's fourth parameter is the amount of free USER_RW RAM left
// outside VitaGL, not the size of VitaGL's pool: VitaGL claims everything
// above it. A constant here therefore gives VitaGL every byte the application
// did not reserve up front, which on an ATTRIBUTE2=12 build is well over
// 100 MiB that this presenter never touches. Measure what is actually free and
// leave VitaGL only kVitaGlPoolBytes.
int application_ram_threshold() {
    SceKernelFreeMemorySizeInfo info{};
    info.size = sizeof(info);
    if (sceKernelGetFreeMemorySize(&info) < 0 || info.size_user <= 0) {
        krkrvita_boot_trace("vitagl-free-memory-query-failed");
        return krkrvita::kVitaGlMinApplicationRamThresholdBytes;
    }
    const int threshold = krkrvita::vitagl_application_ram_threshold(
        static_cast<std::size_t>(info.size_user));
    // The exact figures decide how much large-bitmap memory a retail project
    // gets, so put them in the hardware log rather than inferring them.
    char trace[96];
    std::snprintf(trace, sizeof trace,
                  "vitagl-user-ram-free-%dm-threshold-%dm",
                  info.size_user / (1024 * 1024), threshold / (1024 * 1024));
    krkrvita_boot_trace(trace);
    return threshold;
}
// vitaGL keeps resources referenced by the preceding four frames alive.
// Updating a texture inside that window makes glTexSubImage2D clone its old
// backing store first. Five presentation textures match vitaGL's documented
// streaming-video pattern and make a full-frame update a single fast copy.
constexpr std::size_t kPresentationBuffers = 5;

bool initialized = false;
bool first_present = true;
std::uint32_t successful_game_frames = 0;
std::uint32_t contentful_game_frames = 0;
std::uint32_t uploaded_game_frames = 0;
std::uint32_t full_frame_uploads = 0;
std::uint32_t partial_frame_uploads = 0;
std::uint64_t uploaded_pixels = 0;
bool first_source_probe = true;
bool cursor_visible = false;
bool cursor_proof_written = false;
int cursor_x = 0;
int cursor_y = 0;
krkrvita::PresentationSurfaceState<kPresentationBuffers> presentation_surface;
krkrvita::PresentationDamageTracker<kPresentationBuffers> presentation_damage;
unsigned int last_presented_texture = 0;
bool last_presented_contentful = false;
bool partial_upload_reported = false;
bool presentation_allocation_failure_reported = false;

struct VideoOverlayPresentation {
    std::mutex mutex;
    std::vector<std::uint8_t> rgba;
    std::array<unsigned int, kPresentationBuffers> textures{};
    int width = 0;
    int height = 0;
    int texture_width = 0;
    int texture_height = 0;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    std::uint64_t serial = 0;
    std::uint64_t uploaded_serial = 0;
    std::size_t texture_index = 0;
    float alpha = 1.0f;
    bool visible = false;
    bool textures_ready = false;
};

VideoOverlayPresentation video_overlay;
bool first_video_frame_presented = false;

void discard_gl_errors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

void delete_texture_set(
    const krkrvita::PresentationSurfaceState<
        kPresentationBuffers>::TextureArray& texture_set) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(static_cast<GLsizei>(texture_set.size()),
                     texture_set.data());
}

bool shader_compiler_available() {
    constexpr const char* paths[] = {
        "ur0:/data/libshacccg.suprx",
        "ur0:data/external/libshacccg.suprx"
    };
    SceIoStat status{};
    bool available = false;
    for (const char* path : paths) {
        const int result = sceIoGetstat(path, &status);
        char trace[160];
        std::snprintf(trace, sizeof trace,
                      "shader-compiler-stat path=%s result=0x%08x",
                      path, static_cast<unsigned int>(result));
        krkrvita_boot_trace(trace);
        available = available || result >= 0;
    }
    return available;
}

void configure_2d() {
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    glViewport(0, 0, kScreenWidth, kScreenHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, kScreenWidth, kScreenHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
}

void draw_cursor(float left, float top, float scale) {
    if (!cursor_visible) return;
    const float x = left + cursor_x * scale;
    const float y = top + cursor_y * scale;
    // A high-contrast arrow rendered after the game surface. Keeping this in
    // the VitaGL presentation pass makes it visible regardless of whether a
    // retail game provides a Windows cursor resource.
    const GLfloat outline[] = {
        x, y,
        x + 2.0f, y + 23.0f,
        x + 16.0f, y + 15.0f,
    };
    const GLfloat fill[] = {
        x + 2.0f, y + 3.0f,
        x + 3.5f, y + 18.0f,
        x + 12.5f, y + 14.0f,
    };
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glColor4f(0, 0, 0, 1);
    glVertexPointer(2, GL_FLOAT, 0, outline);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glColor4f(1, 1, 1, 1);
    glVertexPointer(2, GL_FLOAT, 0, fill);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!cursor_proof_written) {
        krkrvita_boot_trace("vitagl-cursor-overlay-presented");
        cursor_proof_written = true;
    }
}

void release_video_textures_locked() {
    if (video_overlay.textures_ready) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(static_cast<GLsizei>(video_overlay.textures.size()),
                         video_overlay.textures.data());
    }
    video_overlay.textures.fill(0);
    video_overlay.textures_ready = false;
    video_overlay.uploaded_serial = 0;
    video_overlay.texture_index = 0;
    video_overlay.texture_width = 0;
    video_overlay.texture_height = 0;
}

bool allocate_video_textures_locked() {
    release_video_textures_locked();
    if (video_overlay.width <= 0 || video_overlay.height <= 0) return false;

    discard_gl_errors();
    glGenTextures(static_cast<GLsizei>(video_overlay.textures.size()),
                  video_overlay.textures.data());
    std::size_t ready = 0;
    for (const unsigned int texture : video_overlay.textures) {
        if (!texture) break;
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, video_overlay.width,
                     video_overlay.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
        if (glGetError() != GL_NO_ERROR ||
            vglGetTexDataPointer(GL_TEXTURE_2D) == nullptr)
            break;
        ++ready;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (ready != video_overlay.textures.size()) {
        glDeleteTextures(static_cast<GLsizei>(video_overlay.textures.size()),
                         video_overlay.textures.data());
        video_overlay.textures.fill(0);
        return false;
    }
    video_overlay.textures_ready = true;
    video_overlay.texture_width = video_overlay.width;
    video_overlay.texture_height = video_overlay.height;
    krkrvita_boot_trace("vitagl-video-textures-ready");
    return true;
}

void draw_video_overlay(float surface_left, float surface_top,
                        float surface_scale) {
    std::lock_guard<std::mutex> lock(video_overlay.mutex);
    if (!video_overlay.visible || video_overlay.rgba.empty() ||
        video_overlay.width <= 0 || video_overlay.height <= 0)
        return;
    if ((!video_overlay.textures_ready ||
         video_overlay.texture_width != video_overlay.width ||
         video_overlay.texture_height != video_overlay.height) &&
        !allocate_video_textures_locked())
        return;

    if (video_overlay.uploaded_serial != video_overlay.serial) {
        video_overlay.texture_index =
            (video_overlay.texture_index + 1) % video_overlay.textures.size();
        glBindTexture(GL_TEXTURE_2D,
                      video_overlay.textures[video_overlay.texture_index]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, video_overlay.width);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, video_overlay.width,
                        video_overlay.height, GL_RGBA, GL_UNSIGNED_BYTE,
                        video_overlay.rgba.data());
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        if (glGetError() != GL_NO_ERROR) return;
        video_overlay.uploaded_serial = video_overlay.serial;
    }

    const int logical_right = video_overlay.right > video_overlay.left
                                  ? video_overlay.right
                                  : video_overlay.left + video_overlay.width;
    const int logical_bottom = video_overlay.bottom > video_overlay.top
                                   ? video_overlay.bottom
                                   : video_overlay.top + video_overlay.height;
    const float left = surface_left + video_overlay.left * surface_scale;
    const float top = surface_top + video_overlay.top * surface_scale;
    const float right = surface_left + logical_right * surface_scale;
    const float bottom = surface_top + logical_bottom * surface_scale;
    const GLfloat vertices[] = {
        left, top, right, top, left, bottom, right, bottom,
    };
    const GLfloat texture_coordinates[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
    };
    glBindTexture(GL_TEXTURE_2D,
                  video_overlay.textures[video_overlay.texture_index]);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, video_overlay.alpha);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, texture_coordinates);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
    if (!first_video_frame_presented) {
        krkrvita_boot_trace("vitagl-first-movie-frame-presented");
        first_video_frame_presented = true;
    }
}

bool present_bound_texture(unsigned int texture, int width, int height,
                           float max_u, float max_v, bool contentful) {
    const float scale = std::min(static_cast<float>(kScreenWidth) / width,
                                 static_cast<float>(kScreenHeight) / height);
    const float output_width = width * scale;
    const float output_height = height * scale;
    const float left = (kScreenWidth - output_width) * 0.5f;
    const float top = (kScreenHeight - output_height) * 0.5f;

    configure_2d();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1);

    const GLfloat vertices[] = {
        left,                top,
        left + output_width, top,
        left,                top + output_height,
        left + output_width, top + output_height,
    };
    const GLfloat texture_coordinates[] = {
        0.0f, 0.0f,
        max_u, 0.0f,
        0.0f, max_v,
        max_u, max_v,
    };
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, texture_coordinates);
    if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-arrays-ready");
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-draw-submitted");

    draw_video_overlay(left, top, scale);
    draw_cursor(left, top, scale);

    const GLenum draw_error = glGetError();
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (draw_error != GL_NO_ERROR) {
        if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-draw-failed");
        return false;
    }

    if (contentful && contentful_game_frames == 0)
        krkrvita_boot_trace("vitagl-first-contentful-frame-entered");
    if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-swap-entered");
    vglSwapBuffers(GL_FALSE);
    if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-swap-returned");
    if (glGetError() != GL_NO_ERROR) {
        if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-swap-failed");
        return false;
    }
    if (first_present) {
        krkrvita_boot_trace("vitagl-first-game-frame-presented");
        first_present = false;
    }
    ++successful_game_frames;
    if (contentful) {
        ++contentful_game_frames;
        if (contentful_game_frames == 1)
            krkrvita_boot_trace("vitagl-first-contentful-frame-presented");
    }
    if (successful_game_frames == 60)
        krkrvita_boot_trace("vitagl-60-game-frames-presented");
    else if (successful_game_frames == 300)
        krkrvita_boot_trace("vitagl-300-game-frames-presented");
    return true;
}

} // namespace

bool krkrvita_vitagl_initialize() {
    if (initialized) return true;
    krkrvita_boot_trace("vitagl-init-entered");
    if (!shader_compiler_available()) {
        krkrvita_report_launch_error(
            "Cannot access libshacccg.suprx in ur0:/data/ or ur0:data/external/. "
            "Check boot-status.txt for file-access error codes. Enable unsafe "
            "homebrew in HENkaku Settings and install the shader compiler with "
            "ShaRKBR33D or VitaDB Downloader, then launch Kirikiri Vita again.");
        return false;
    }
    krkrvita_boot_trace("vitagl-shader-compiler-found");
    // GTA:SA and YoYo Loader configure VitaGL's deferred resource collector
    // before initialization. Use the same priority and core-affinity contract
    // instead of relying on the library defaults.
    vglSetupGarbageCollector(127, 0x20000);
    krkrvita_boot_trace("vitagl-garbage-collector-configured");
    // Decoded images are CPU-written before upload. This must be selected
    // before vglInitExtended creates VitaGL's USER_RW pool.
    vglUseCachedMem(GL_TRUE);
    krkrvita_boot_trace("vitagl-cached-ram-pool-enabled");
    // Despite its GLboolean type, vitaGL returns whether it had to fall back
    // to a smaller display resolution here. GL_FALSE is the normal result at
    // the Vita's native 960x544 resolution; it does not mean initialization
    // failed. The library has no recoverable failure return from this API.
    const GLboolean resolution_fallback =
        vglInitExtended(0, kScreenWidth, kScreenHeight,
                        application_ram_threshold(),
                        SCE_GXM_MULTISAMPLE_NONE);
    krkrvita_boot_trace("vitagl-init-returned");
    if (resolution_fallback)
        krkrvita_boot_trace("vitagl-resolution-fallback");
    krkrvita_boot_trace("vitagl-initialized");
    initialized = true;
    // The engine loop owns the same 60 Hz whole-frame deadline as Yuri's
    // Android Cocos frontend. A blocking swap here would add a complete vblank
    // after software composition and stall script/timer dispatch on every
    // changed frame.
    vglWaitVblankStart(GL_FALSE);
    krkrvita_boot_trace("vitagl-swap-decoupled-from-engine-tick");
    configure_2d();

    // vitaGL keeps its animated splash active until the first GL scene begins.
    // Submit a real application frame now so a later script error or a slow
    // game load cannot leave the splash looking like the application itself.
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    vglSwapBuffers(GL_FALSE);
    krkrvita_boot_trace("vitagl-bootstrap-frame-presented");
    return true;
}

bool krkrvita_vitagl_resize(int width, int height) {
    if (!initialized && !krkrvita_vitagl_initialize()) return false;
    if (width <= 0 || height <= 0) return false;
    if (!presentation_surface.resize_required(width, height)) return true;

    // The installed VitaGL frees a texture's existing backing store before
    // gpu_alloc_texture attempts its replacement, and a failed mapped-memory
    // allocation does not set GL_OUT_OF_MEMORY. Allocate a complete new set of
    // names instead: the active set remains drawable until every candidate has
    // both an error-free GL call and a non-null VitaGL data pointer.
    using SurfaceState =
        krkrvita::PresentationSurfaceState<kPresentationBuffers>;
    SurfaceState::TextureArray candidates{};
    discard_gl_errors();
    glGenTextures(static_cast<GLsizei>(candidates.size()), candidates.data());
    std::size_t ready_count = 0;
    if (glGetError() == GL_NO_ERROR) {
        for (const unsigned int texture : candidates) {
            if (texture == 0) break;
            discard_gl_errors();
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            // Yuri's mobile compositor stores AABBGGRR words, which are RGBA
            // bytes on the Vita's little-endian ARM. Matching that format
            // selects VitaGL's U8U8U8U8_ABGR direct-copy upload path.
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            const bool storage_ready =
                vglGetTexDataPointer(GL_TEXTURE_2D) != nullptr;
            if (glGetError() != GL_NO_ERROR || !storage_ready) break;
            ++ready_count;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    const SurfaceState::CommitResult commit =
        presentation_surface.complete_resize(width, height, candidates,
                                             ready_count);
    if (!commit.committed) {
        delete_texture_set(candidates);
        if (!presentation_allocation_failure_reported) {
            krkrvita_boot_trace(
                "vitagl-presentation-surface-allocation-failed");
            presentation_allocation_failure_reported = true;
        }
        return false;
    }

    // From this point the new set is the only active set. Its contents are
    // undefined until uploaded, so every rotating texture starts with full
    // damage. Retiring the old names only after the commit preserves a valid
    // redraw target throughout candidate allocation.
    presentation_damage.configure(width, height);
    last_presented_texture = 0;
    last_presented_contentful = false;
    presentation_allocation_failure_reported = false;
    delete_texture_set(commit.retired);
    krkrvita_boot_trace("vitagl-presentation-texture-ready");
    krkrvita_boot_trace("vitagl-presentation-surface-sized");
    return true;
}

bool krkrvita_vitagl_present(const void* pixels, int pitch, int width, int height) {
    krkrvita::FrameDamageRegion full_damage;
    full_damage.mark_full(width, height);
    return krkrvita_vitagl_present_damage(
        pixels, pitch, width, height, full_damage);
}

bool krkrvita_vitagl_present_damage(
    const void* pixels, int pitch, int width, int height,
    const krkrvita::FrameDamageRegion& damage) {
    if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-entered");
    if (!pixels || width <= 0 || height <= 0 || pitch < width * 4 ||
        (pitch & 3) != 0 || damage.empty() ||
        damage.surface_width() != width ||
        damage.surface_height() != height ||
        !krkrvita_vitagl_resize(width, height)) {
        if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-invalid");
        return false;
    }

    bool contentful = contentful_game_frames != 0;
    if (!contentful) {
        const krkrvita::RgbaFrameProbe probe =
            krkrvita::probe_rgba_frame(pixels, pitch, width, height);
        contentful = probe.has_visible_color();
        if (first_source_probe) {
            krkrvita_boot_trace(contentful
                                    ? "vitagl-first-game-frame-source-contentful"
                                    : "vitagl-first-game-frame-source-black");
            first_source_probe = false;
        }
    }

    // Each presentation texture is reused only after four intervening frames,
    // matching VitaGL's deferred-resource lifetime. Accumulate every change
    // since a particular texture was last shown so partial uploads cannot
    // resurrect stale pixels from an older swapchain image.
    presentation_damage.add_frame(damage);
    const krkrvita::FrameDamageRegion& texture_damage =
        presentation_damage.current();
    const krkrvita::FrameDamageRect rect = texture_damage.rect();
    const unsigned int texture =
        presentation_surface.textures()[presentation_damage.current_index()];
    glBindTexture(GL_TEXTURE_2D, texture);
    // The installed VitaGL's matching RGBA fast-store path explicitly uses
    // GL_UNPACK_ROW_LENGTH as its source stride. Supplying the compositor's
    // real pitch lets one glTexSubImage2D copy only the bounding damage rect;
    // there is no temporary full-frame repack and no per-scanline GL call.
    const auto* source = static_cast<const std::uint8_t*>(pixels) +
        static_cast<std::size_t>(rect.top) * pitch +
        static_cast<std::size_t>(rect.left) * 4;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, rect.left, rect.top,
                    rect.width(), rect.height(), GL_RGBA,
                    GL_UNSIGNED_BYTE, source);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (glGetError() != GL_NO_ERROR) {
        if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-upload-failed");
        return false;
    }
    uploaded_pixels += static_cast<std::uint64_t>(rect.width()) *
                       static_cast<std::uint64_t>(rect.height());
    if (texture_damage.full())
        ++full_frame_uploads;
    else
        ++partial_frame_uploads;
    if (!texture_damage.full() && !partial_upload_reported) {
        krkrvita_boot_trace("vitagl-partial-frame-upload-ready");
        partial_upload_reported = true;
    }
    if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-uploaded");

    const bool presented =
        present_bound_texture(texture, width, height, 1.0f, 1.0f, contentful);
    if (!presented) return false;
    last_presented_texture = texture;
    last_presented_contentful = contentful;
    ++uploaded_game_frames;
    presentation_damage.complete_current();
    return true;
}

bool krkrvita_vitagl_redraw() {
    if (!initialized || last_presented_texture == 0 ||
        !presentation_surface.valid())
        return false;
    return present_bound_texture(last_presented_texture,
                                 presentation_surface.width(),
                                 presentation_surface.height(), 1.0f, 1.0f,
                                 last_presented_contentful);
}

#ifdef KRKRVITA_YURI_OPENGL_COMPOSITOR
bool krkrvita_vitagl_present_texture(unsigned int texture, int width, int height,
                                     int internal_width, int internal_height,
                                     float scale_width, float scale_height) {
    if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-entered");
    if (!initialized && !krkrvita_vitagl_initialize()) return false;
    if (!texture || width <= 0 || height <= 0 || internal_width <= 0 ||
        internal_height <= 0 || scale_width <= 0.0f || scale_height <= 0.0f) {
        if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-invalid");
        return false;
    }
    if (first_source_probe) {
        krkrvita_boot_trace("vitagl-first-game-frame-source-gpu-texture");
        first_source_probe = false;
    }
    if (first_present)
        krkrvita_boot_trace("vitagl-first-game-frame-zero-copy-ready");
    const float max_u = std::min(1.0f, width * scale_width / internal_width);
    const float max_v = std::min(1.0f, height * scale_height / internal_height);
    return present_bound_texture(texture, width, height, max_u, max_v, true);
}
#endif

void krkrvita_vitagl_set_cursor(int x, int y, bool visible) {
    cursor_x = std::max(0, x);
    cursor_y = std::max(0, y);
    cursor_visible = visible;
}

std::uint32_t krkrvita_vitagl_presented_frames() {
    return successful_game_frames;
}

std::uint32_t krkrvita_vitagl_contentful_frames() {
    return contentful_game_frames;
}

std::uint32_t krkrvita_vitagl_uploaded_frames() {
    return uploaded_game_frames;
}

std::uint32_t krkrvita_vitagl_full_uploads() {
    return full_frame_uploads;
}

std::uint32_t krkrvita_vitagl_partial_uploads() {
    return partial_frame_uploads;
}

std::uint64_t krkrvita_vitagl_uploaded_pixels() {
    return uploaded_pixels;
}

bool krkrvita_vitagl_submit_video_frame(const void* rgba, int pitch,
                                        int width, int height,
                                        std::uint64_t serial) {
    if (!rgba || width <= 0 || height <= 0 || pitch < width * 4) return false;
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
    if (row_bytes > static_cast<std::size_t>(-1) /
                        static_cast<std::size_t>(height))
        return false;
    std::lock_guard<std::mutex> lock(video_overlay.mutex);
    if (video_overlay.width != width || video_overlay.height != height) {
        // Drawing observes the dimension mismatch and retires the old GL set
        // on the main thread before allocating the replacement.
        video_overlay.width = width;
        video_overlay.height = height;
    }
    try {
        video_overlay.rgba.resize(row_bytes * static_cast<std::size_t>(height));
    } catch (const std::bad_alloc&) {
        return false;
    }
    const auto* source = static_cast<const std::uint8_t*>(rgba);
    for (int y = 0; y < height; ++y) {
        std::memcpy(video_overlay.rgba.data() +
                        static_cast<std::size_t>(y) * row_bytes,
                    source + static_cast<std::size_t>(y) * pitch, row_bytes);
    }
    video_overlay.serial = serial ? serial : video_overlay.serial + 1;
    return true;
}

void krkrvita_vitagl_set_video_rect(int left, int top, int right, int bottom) {
    std::lock_guard<std::mutex> lock(video_overlay.mutex);
    video_overlay.left = left;
    video_overlay.top = top;
    video_overlay.right = right;
    video_overlay.bottom = bottom;
}

void krkrvita_vitagl_set_video_visible(bool visible) {
    std::lock_guard<std::mutex> lock(video_overlay.mutex);
    video_overlay.visible = visible;
}

void krkrvita_vitagl_set_video_alpha(float alpha) {
    std::lock_guard<std::mutex> lock(video_overlay.mutex);
    video_overlay.alpha = std::clamp(alpha, 0.0f, 1.0f);
}

void krkrvita_vitagl_clear_video() {
    std::lock_guard<std::mutex> lock(video_overlay.mutex);
    video_overlay.visible = false;
    video_overlay.rgba.clear();
    video_overlay.serial = 0;
    video_overlay.uploaded_serial = 0;
}

bool krkrvita_vitagl_video_active() {
    std::lock_guard<std::mutex> lock(video_overlay.mutex);
    return video_overlay.visible && !video_overlay.rgba.empty();
}

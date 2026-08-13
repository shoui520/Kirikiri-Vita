#include "krkrvita/vitagl_presenter.hpp"
#include "krkrvita/retail_bootstrap.hpp"

#include <vitaGL.h>
#include <psp2/io/stat.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 544;
constexpr int kVitaGlMemory = 24 * 1024 * 1024;

bool initialized = false;
bool first_present = true;
unsigned int texture = 0;
int texture_width = 0;
int texture_height = 0;
std::vector<std::uint8_t> tightly_packed;

bool shader_compiler_available() {
    SceIoStat status{};
    return sceIoGetstat("ur0:/data/libshacccg.suprx", &status) >= 0 ||
           sceIoGetstat("ur0:data/external/libshacccg.suprx", &status) >= 0;
}

void configure_2d() {
    glViewport(0, 0, kScreenWidth, kScreenHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, kScreenWidth, kScreenHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

} // namespace

bool krkrvita_vitagl_initialize() {
    if (initialized) return true;
    krkrvita_boot_trace("vitagl-init-entered");
    if (!shader_compiler_available()) {
        krkrvita_report_launch_error(
            "VitaGL requires ur0:/data/libshacccg.suprx. Install it with "
            "ShaRKBR33D or VitaDB Downloader, then launch Kirikiri Vita again.");
        return false;
    }
    krkrvita_boot_trace("vitagl-shader-compiler-found");
    // Despite its GLboolean type, vitaGL returns whether it had to fall back
    // to a smaller display resolution here. GL_FALSE is the normal result at
    // the Vita's native 960x544 resolution; it does not mean initialization
    // failed. The library has no recoverable failure return from this API.
    const GLboolean resolution_fallback =
        vglInitExtended(0, kScreenWidth, kScreenHeight, kVitaGlMemory,
                        SCE_GXM_MULTISAMPLE_NONE);
    krkrvita_boot_trace("vitagl-init-returned");
    if (resolution_fallback)
        krkrvita_boot_trace("vitagl-resolution-fallback");
    krkrvita_boot_trace("vitagl-initialized");
    initialized = true;
    configure_2d();
    glGenTextures(1, &texture);
    if (texture == 0) {
        krkrvita_report_launch_error("VitaGL could not allocate the presentation texture.");
        return false;
    }
    krkrvita_boot_trace("vitagl-presentation-texture-ready");

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
    if (texture_width == width && texture_height == height) return true;

    texture_width = width;
    texture_height = height;
    tightly_packed.resize(static_cast<std::size_t>(width) * height * 4u);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR) return false;
    krkrvita_boot_trace("vitagl-presentation-surface-sized");
    return true;
}

bool krkrvita_vitagl_present(const void* pixels, int pitch, int width, int height) {
    if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-entered");
    if (!pixels || width <= 0 || height <= 0 || pitch < width * 4 ||
        !krkrvita_vitagl_resize(width, height)) {
        if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-invalid");
        return false;
    }

    const auto* source = static_cast<const std::uint8_t*>(pixels);
    const std::size_t row_size = static_cast<std::size_t>(width) * 4u;
    for (int y = 0; y < height; ++y) {
        std::memcpy(tightly_packed.data() + static_cast<std::size_t>(y) * row_size,
                    source + static_cast<std::size_t>(y) * pitch, row_size);
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_BGRA, GL_UNSIGNED_BYTE, tightly_packed.data());
    if (glGetError() != GL_NO_ERROR) {
        if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-upload-failed");
        return false;
    }
    if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-uploaded");

    const float scale = std::min(static_cast<float>(kScreenWidth) / width,
                                 static_cast<float>(kScreenHeight) / height);
    const float output_width = width * scale;
    const float output_height = height * scale;
    const float left = (kScreenWidth - output_width) * 0.5f;
    const float top = (kScreenHeight - output_height) * 0.5f;

    configure_2d();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(left, top);
    glTexCoord2f(1, 0); glVertex2f(left + output_width, top);
    glTexCoord2f(1, 1); glVertex2f(left + output_width, top + output_height);
    glTexCoord2f(0, 1); glVertex2f(left, top + output_height);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    vglSwapBuffers(GL_FALSE);
    if (glGetError() != GL_NO_ERROR) {
        if (first_present) krkrvita_boot_trace("vitagl-first-game-frame-draw-failed");
        return false;
    }
    if (first_present) {
        krkrvita_boot_trace("vitagl-first-game-frame-presented");
        first_present = false;
    }
    return true;
}

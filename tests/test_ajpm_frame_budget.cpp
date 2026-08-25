// What an AJPM frame costs on a Cortex-A9, measured rather than guessed.
//
// AlphaMovie playback has a hard budget at 30 fps, so everything an effect
// frame needs must fit in 33.3 ms alongside the rest of the engine. Large
// frames can be 1024x768, and each one
// carries a full-size 8-bit alpha plane compressed with zlib -- 768 KB of
// inflate output per frame, 23 MB/s sustained.
//
// That is the number that decides whether a playback path is worth building,
// and it is a property of the CPU, not of the port. This probe runs on the
// Cortex-A9 Linux board via scripts/run-ajpm-frame-budget.sh.
//
// It uses synthetic data shaped like a real alpha plane -- mostly transparent,
// with a soft-edged blob -- and never touches game content, so nothing from
// the retail corpus is staged on the board.
//
// The board is not a Vita: clocks, memory and DRAM timings differ, and the
// Vita runs this alongside the engine rather than on an idle core. Treat the
// result as a floor on the cost, not as a frame time.

// zlib's entry points are declared here rather than included, so the probe can
// link against whichever build of zlib is being measured -- including the
// VitaSDK one the Vita backend itself uses -- without dragging in a headers
// set that conflicts with the host toolchain's.
extern "C" {
int uncompress(unsigned char* dest, unsigned long* dest_length,
               const unsigned char* source, unsigned long source_length);
int compress2(unsigned char* dest, unsigned long* dest_length,
              const unsigned char* source, unsigned long source_length,
              int level);
unsigned long compressBound(unsigned long source_length);
}

constexpr int Z_OK = 0;
constexpr int Z_DEFAULT_COMPRESSION = -1;
using uLong = unsigned long;
using uLongf = unsigned long;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 1024;
constexpr int kHeight = 768;
constexpr int kFrames = 60;
constexpr double kFrameBudgetMs = 1000.0 / 30.0;

// An effect frame's alpha: transparent almost everywhere, with a soft blob
// where the effect actually is. Compression ratio and inflate cost both depend
// on this shape, so a flat or random buffer would measure the wrong thing.
std::vector<std::uint8_t> synthetic_alpha() {
    std::vector<std::uint8_t> alpha(static_cast<std::size_t>(kWidth) * kHeight, 0);
    const double cx = kWidth * 0.5;
    const double cy = kHeight * 0.55;
    const double radius = kHeight * 0.42;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const double dx = (x - cx) / radius;
            const double dy = (y - cy) / radius;
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d >= 1.0) continue;
            const double falloff = 1.0 - d * d;
            // A little structure so it does not compress like a pure gradient.
            const double ripple = 0.85 + 0.15 * std::sin(d * 24.0);
            alpha[static_cast<std::size_t>(y) * kWidth + x] =
                static_cast<std::uint8_t>(255.0 * falloff * ripple);
        }
    }
    return alpha;
}

double milliseconds_since(std::chrono::steady_clock::time_point start) {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

void report_clock() {
    std::FILE* file =
        std::fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
    if (!file) return;
    long khz = 0;
    if (std::fscanf(file, "%ld", &khz) == 1 && khz > 0)
        std::printf("cpu: %ld MHz (Vita's Cortex-A9 runs at 444 MHz, "
                    "up to 500 on request)\n", khz / 1000);
    std::fclose(file);
}

void report_cpu() {
    std::FILE* file = std::fopen("/proc/cpuinfo", "r");
    if (!file) return;
    char line[256];
    while (std::fgets(line, sizeof(line), file)) {
        const std::string text(line);
        if (text.rfind("model name", 0) == 0 || text.rfind("Hardware", 0) == 0 ||
            text.rfind("BogoMIPS", 0) == 0) {
            std::printf("cpu: %s", text.c_str());
        }
    }
    std::fclose(file);
}

} // namespace

int main() {
    report_cpu();
    report_clock();

    const auto alpha = synthetic_alpha();
    const auto pixels = alpha.size();

    uLongf bound = ::compressBound(static_cast<uLong>(pixels));
    std::vector<std::uint8_t> compressed(bound);
    if (::compress2(compressed.data(), &bound, alpha.data(),
                    static_cast<uLong>(pixels), Z_DEFAULT_COMPRESSION) != Z_OK) {
        std::fprintf(stderr, "could not deflate the probe frame\n");
        return 1;
    }
    compressed.resize(bound);
    std::printf("frame: %dx%d, alpha %zu bytes -> %zu compressed (%.1f%%)\n",
                kWidth, kHeight, pixels, compressed.size(),
                100.0 * static_cast<double>(compressed.size()) /
                    static_cast<double>(pixels));

    // 1. Inflating the alpha plane. This is the unavoidable per-frame cost:
    //    it scales with the frame's pixel count, not with how busy the frame
    //    is, and there is no way to skip it for a frame that is drawn.
    std::vector<std::uint8_t> inflated(pixels);
    {
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) {
            uLongf produced = static_cast<uLongf>(pixels);
            if (::uncompress(inflated.data(), &produced, compressed.data(),
                             static_cast<uLong>(compressed.size())) != Z_OK ||
                produced != pixels) {
                std::fprintf(stderr, "inflate failed\n");
                return 1;
            }
        }
        const double total = milliseconds_since(start);
        std::printf("alpha inflate:   %6.2f ms/frame  (%5.1f%% of the 33.3 ms budget)\n",
                    total / kFrames, 100.0 * (total / kFrames) / kFrameBudgetMs);
    }

    // 2. Merging that alpha into a decoded RGB frame to produce the premultiplied
    //    BGRA a Kirikiri layer holds. The JPEG decode itself is not measured
    //    here: the Vita has a hardware JPEG unit and FFmpeg's MJPEG decoder is
    //    already linked, so it is a known quantity. This step is the part a
    //    playback path would have to write itself.
    std::vector<std::uint8_t> rgb(pixels * 3);
    for (std::size_t i = 0; i < rgb.size(); ++i)
        rgb[i] = static_cast<std::uint8_t>(i * 31);
    std::vector<std::uint32_t> frame(pixels);
    {
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) {
            for (std::size_t p = 0; p < pixels; ++p) {
                const std::uint32_t a = inflated[p];
                const std::uint32_t r = rgb[p * 3 + 0] * a / 255;
                const std::uint32_t g = rgb[p * 3 + 1] * a / 255;
                const std::uint32_t b = rgb[p * 3 + 2] * a / 255;
                frame[p] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        const double total = milliseconds_since(start);
        std::printf("compose, naive:  %6.2f ms/frame  (%5.1f%% of the 33.3 ms budget)"
                    "  <- integer divide per channel\n",
                    total / kFrames, 100.0 * (total / kFrames) / kFrameBudgetMs);
    }

    // 2b. The same premultiply without the divides. x * a / 255 is exactly
    //     (x * a * 257 + 257) >> 16 for all 8-bit x and a, so the divide is
    //     never needed. This is what a real implementation would do before
    //     anyone reached for NEON.
    {
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) {
            for (std::size_t p = 0; p < pixels; ++p) {
                const std::uint32_t a = inflated[p];
                const std::uint32_t r = (rgb[p * 3 + 0] * a * 257u + 257u) >> 16;
                const std::uint32_t g = (rgb[p * 3 + 1] * a * 257u + 257u) >> 16;
                const std::uint32_t b = (rgb[p * 3 + 2] * a * 257u + 257u) >> 16;
                frame[p] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        const double total = milliseconds_since(start);
        std::printf("compose, mulshift:%5.2f ms/frame  (%5.1f%% of the 33.3 ms budget)\n",
                    total / kFrames, 100.0 * (total / kFrames) / kFrameBudgetMs);
    }

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    // 2c. And with NEON, eight pixels at a time. The Vita's Cortex-A9 has
    //     NEON, and Yuri's own blend routines already use it, so this is the
    //     cost a playback path would actually pay.
    {
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) {
            const std::uint8_t* source = rgb.data();
            const std::uint8_t* mask = inflated.data();
            std::uint32_t* out = frame.data();
            for (std::size_t p = 0; p + 8 <= pixels; p += 8) {
                const uint8x8x3_t colour = vld3_u8(source + p * 3);
                const uint8x8_t a = vld1_u8(mask + p);
                const uint16x8_t a16 = vmovl_u8(a);
                // (x * a + 127) / 255, to 8-bit accuracy, via a reciprocal
                // multiply: x*a is at most 65025, so the high half of
                // (x*a) * 0x8081 >> 7 gives the quotient.
                const auto premultiply = [&](uint8x8_t channel) {
                    const uint16x8_t product = vmull_u8(channel, a);
                    const uint16x8_t rounded = vaddq_u16(product, vdupq_n_u16(128));
                    return vshrn_n_u16(
                        vaddq_u16(rounded, vshrq_n_u16(rounded, 8)), 8);
                };
                uint8x8x4_t packed;
                packed.val[0] = premultiply(colour.val[2]);
                packed.val[1] = premultiply(colour.val[1]);
                packed.val[2] = premultiply(colour.val[0]);
                packed.val[3] = a;
                (void)a16;
                vst4_u8(reinterpret_cast<std::uint8_t*>(out + p), packed);
            }
        }
        const double total = milliseconds_since(start);
        std::printf("compose, NEON:   %6.2f ms/frame  (%5.1f%% of the 33.3 ms budget)\n",
                    total / kFrames, 100.0 * (total / kFrames) / kFrameBudgetMs);
    }
#endif

    // 3. Handing the result to a layer. A Kirikiri layer holds its own bitmap,
    //    so a frame reaches the screen as a copy into that bitmap.
    std::vector<std::uint32_t> layer(pixels);
    {
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) {
            for (int y = 0; y < kHeight; ++y) {
                std::memcpy(layer.data() + static_cast<std::size_t>(y) * kWidth,
                            frame.data() + static_cast<std::size_t>(y) * kWidth,
                            static_cast<std::size_t>(kWidth) * 4);
            }
        }
        const double total = milliseconds_since(start);
        std::printf("layer upload:    %6.2f ms/frame  (%5.1f%% of the 33.3 ms budget)\n",
                    total / kFrames, 100.0 * (total / kFrames) / kFrameBudgetMs);
    }

    std::printf("\nJPEG decode is excluded: the Vita has a hardware unit and\n"
                "FFmpeg's MJPEG decoder is already linked into the backend.\n"
                "Everything above is work a playback path would add on top.\n");
    return 0;
}

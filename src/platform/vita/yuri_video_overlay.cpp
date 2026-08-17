#include "tjsCommHead.h"

#include "DebugIntf.h"
#include "LayerBitmapIntf.h"
#include "MsgIntf.h"
#include "VideoOvlImpl.h"
#include "WaveMixer.h"
#include "combase.h"
#include "krmovie.h"
#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/vita_video_frame.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

#ifndef __stdcall
#define __stdcall
#define KRKRVITA_LOCAL_STDCALL 1
#endif

constexpr int kAvioBufferBytes = 32 * 1024;
constexpr int kMovieAudioRate = 48000;
constexpr int kMovieAudioChannels = 2;
constexpr int kMovieAudioBuffers = 24;

std::atomic<bool> first_movie_backend_ready{false};
std::atomic<bool> first_movie_frame_decoded{false};
std::atomic<bool> first_movie_audio_decoded{false};

std::string ffmpeg_error(int result) {
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_make_error_string(text, sizeof(text), result);
    return text;
}

class VitaMovieOverlay final : public iTVPVideoOverlay {
public:
    VitaMovieOverlay(tTJSNI_VideoOverlay* callback, IStream* stream,
                     std::uint64_t stream_size, bool layer_mode)
        : callback_(callback), stream_(stream), stream_size_(stream_size),
          layer_mode_(layer_mode) {
        if (stream_) stream_->AddRef();
    }

    ~VitaMovieOverlay() {
        terminate_.store(true);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            playing_ = false;
        }
        state_cv_.notify_all();
        frame_cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        if (!layer_mode_) krkrvita_vitagl_clear_video();
        if (audio_) audio_->Release();
        if (swr_) swr_free(&swr_);
        if (sws_) sws_freeContext(sws_);
        if (audio_codec_) avcodec_free_context(&audio_codec_);
        if (video_codec_) avcodec_free_context(&video_codec_);
        if (packet_) av_packet_free(&packet_);
        if (frame_) av_frame_free(&frame_);
        if (format_) avformat_close_input(&format_);
        if (avio_) {
            av_freep(&avio_->buffer);
            avio_context_free(&avio_);
        }
        if (stream_) stream_->Release();
    }

    bool initialize(std::string& error) {
        if (!stream_ || stream_size_ == 0) {
            error = "empty KiriKiri movie stream";
            return false;
        }

        auto* avio_buffer = static_cast<unsigned char*>(av_malloc(kAvioBufferBytes));
        if (!avio_buffer) {
            error = "cannot allocate FFmpeg stream buffer";
            return false;
        }
        avio_ = avio_alloc_context(avio_buffer, kAvioBufferBytes, 0, this,
                                   &read_packet, nullptr, &seek_stream);
        if (!avio_) {
            av_free(avio_buffer);
            error = "cannot create FFmpeg stream adapter";
            return false;
        }
        avio_->seekable = AVIO_SEEKABLE_NORMAL;

        format_ = avformat_alloc_context();
        if (!format_) {
            error = "cannot allocate FFmpeg format context";
            return false;
        }
        format_->pb = avio_;
        format_->flags |= AVFMT_FLAG_CUSTOM_IO;
        int result = avformat_open_input(&format_, nullptr, nullptr, nullptr);
        if (result < 0) {
            error = "cannot open movie container: " + ffmpeg_error(result);
            return false;
        }
        result = avformat_find_stream_info(format_, nullptr);
        if (result < 0) {
            error = "cannot read movie stream information: " +
                    ffmpeg_error(result);
            return false;
        }

        video_index_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1,
                                           -1, nullptr, 0);
        if (video_index_ < 0) {
            error = "movie has no supported video stream";
            return false;
        }
        if (!open_codec(video_index_, &video_codec_, error)) return false;
        video_stream_ = format_->streams[video_index_];
        width_ = video_codec_->width;
        height_ = video_codec_->height;
        if (width_ <= 0 || height_ <= 0 ||
            width_ > 4096 || height_ > 4096) {
            error = "movie reports an invalid video size";
            return false;
        }

        AVRational guessed_rate = av_guess_frame_rate(
            format_, video_stream_, nullptr);
        if (guessed_rate.num > 0 && guessed_rate.den > 0)
            fps_ = av_q2d(guessed_rate);
        if (!(fps_ > 0.0) || !std::isfinite(fps_)) fps_ = 30.0;
        if (format_->duration != AV_NOPTS_VALUE && format_->duration > 0)
            total_time_ms_ = av_rescale_q(
                format_->duration, AVRational{1, AV_TIME_BASE},
                AVRational{1, 1000});

        audio_index_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1,
                                           video_index_, nullptr, 0);
        if (audio_index_ >= 0) {
            std::string audio_error;
            if (!open_codec(audio_index_, &audio_codec_, audio_error)) {
                TVPAddImportantLog(
                    TJS_W("(warning) Movie audio disabled: unsupported stream"));
                audio_index_ = -1;
                audio_codec_ = nullptr;
            } else {
                initialize_audio_output();
            }
        }

        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (!packet_ || !frame_) {
            error = "cannot allocate FFmpeg decode frames";
            return false;
        }
        rgba_.resize(static_cast<std::size_t>(width_) * height_ * 4);
        rect_right_ = width_;
        rect_bottom_ = height_;
        if (!layer_mode_)
            krkrvita_vitagl_set_video_rect(0, 0, width_, height_);

        worker_ = std::thread(&VitaMovieOverlay::decode_loop, this);
        if (!first_movie_backend_ready.exchange(true))
            krkrvita_boot_trace("yuri-ffmpeg-movie-backend-ready");
        return true;
    }

    void __stdcall AddRef() override { ++ref_count_; }

    void __stdcall Release() override {
        if (--ref_count_ == 0) delete this;
    }

    void __stdcall SetWindow(tTJSNI_Window* window) override {
        window_ = window;
    }

    void __stdcall SetMessageDrainWindow(void*) override {}

    void __stdcall SetRect(int left, int top, int right, int bottom) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        rect_left_ = left;
        rect_top_ = top;
        rect_right_ = right;
        rect_bottom_ = bottom;
        if (!layer_mode_)
            krkrvita_vitagl_set_video_rect(left, top, right, bottom);
    }

    void __stdcall SetVisible(bool visible) override {
        visible_.store(visible);
        if (!layer_mode_) krkrvita_vitagl_set_video_visible(visible);
    }

    void __stdcall Play() override {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (ended_) {
                seek_position_ms_ = 0;
                seek_pending_ = true;
                ended_ = false;
            }
            playing_ = true;
            status_ = vsPlaying;
            clock_position_ms_ = position_ms_;
            clock_started_ = std::chrono::steady_clock::now();
        }
        if (audio_) audio_->Play();
        state_cv_.notify_all();
    }

    void __stdcall Stop() override {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            playing_ = false;
            status_ = vsStopped;
            seek_position_ms_ = 0;
            seek_pending_ = true;
            position_ms_ = 0;
            current_frame_ = 0;
        }
        if (audio_) audio_->Stop();
        state_cv_.notify_all();
        frame_cv_.notify_all();
    }

    void __stdcall Pause() override {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            update_clock_position_locked();
            playing_ = false;
            status_ = vsPaused;
        }
        if (audio_) audio_->Pause();
        state_cv_.notify_all();
    }

    void __stdcall SetPosition(std::uint64_t tick) override {
        request_seek(std::min<std::uint64_t>(tick, total_time_ms_ > 0
                                                      ? total_time_ms_
                                                      : tick));
    }

    void __stdcall GetPosition(std::uint64_t* tick) override {
        if (!tick) return;
        std::lock_guard<std::mutex> lock(state_mutex_);
        update_clock_position_locked();
        *tick = position_ms_;
    }

    void __stdcall GetStatus(tTVPVideoStatus* status) override {
        if (!status) return;
        std::lock_guard<std::mutex> lock(state_mutex_);
        *status = status_;
    }

    void __stdcall Rewind() override { request_seek(0); }

    void __stdcall SetFrame(int frame) override {
        const int safe_frame = std::max(0, frame);
        request_seek(static_cast<std::uint64_t>(
            std::llround(safe_frame * 1000.0 / fps_)));
    }

    void __stdcall GetFrame(int* frame) override {
        if (!frame) return;
        std::lock_guard<std::mutex> lock(state_mutex_);
        *frame = current_frame_;
    }

    void __stdcall GetFPS(double* fps) override {
        if (fps) *fps = fps_;
    }

    void __stdcall GetNumberOfFrame(int* frames) override {
        if (!frames) return;
        const double count = total_time_ms_ * fps_ / 1000.0;
        *frames = count >= std::numeric_limits<int>::max()
                      ? std::numeric_limits<int>::max()
                      : std::max(0, static_cast<int>(std::llround(count)));
    }

    void __stdcall GetTotalTime(std::int64_t* time) override {
        if (time) *time = total_time_ms_;
    }

    void __stdcall GetVideoSize(long* width, long* height) override {
        if (width) *width = width_;
        if (height) *height = height_;
    }

    tTVPBaseTexture* GetFrontBuffer() override {
        if (!layer_mode_ || !video_buffers_[0] || !video_buffers_[1])
            return nullptr;
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            if (!layer_frame_ready_) return video_buffers_[front_buffer_];
            display_rgba_.swap(layer_rgba_);
            layer_frame_ready_ = false;
        }
        frame_cv_.notify_all();
        front_buffer_ ^= 1;
        video_buffers_[front_buffer_]->Update(
            display_rgba_.data(), static_cast<unsigned int>(width_ * 4),
            0, 0, width_, height_);
        return video_buffers_[front_buffer_];
    }

    void __stdcall SetVideoBuffer(tTVPBaseTexture* first,
                                  tTVPBaseTexture* second, long) override {
        video_buffers_[0] = first;
        video_buffers_[1] = second;
        front_buffer_ = 0;
    }

    void __stdcall SetStopFrame(int frame) override {
        stop_frame_.store(frame);
    }

    void __stdcall GetStopFrame(int* frame) override {
        if (frame) *frame = stop_frame_.load();
    }

    void __stdcall SetDefaultStopFrame() override { stop_frame_.store(-1); }

    void __stdcall SetPlayRate(double rate) override {
        if (!(rate > 0.0) || !std::isfinite(rate)) return;
        std::lock_guard<std::mutex> lock(state_mutex_);
        update_clock_position_locked();
        play_rate_ = rate;
        clock_position_ms_ = position_ms_;
        clock_started_ = std::chrono::steady_clock::now();
        state_cv_.notify_all();
    }

    void __stdcall GetPlayRate(double* rate) override {
        if (!rate) return;
        std::lock_guard<std::mutex> lock(state_mutex_);
        *rate = play_rate_;
    }

    void __stdcall SetAudioBalance(long balance) override {
        audio_balance_ = std::clamp(balance, -100000L, 100000L);
        if (audio_) audio_->SetPan(audio_balance_ / 100000.0f);
    }

    void __stdcall GetAudioBalance(long* balance) override {
        if (balance) *balance = audio_balance_;
    }

    void __stdcall SetAudioVolume(long volume) override {
        audio_volume_ = std::max(0L, volume);
        if (audio_) audio_->SetVolume(audio_volume_ / 100000.0f);
    }

    void __stdcall GetAudioVolume(long* volume) override {
        if (volume) *volume = audio_volume_;
    }

    void __stdcall GetNumberOfAudioStream(unsigned long* count) override {
        if (count) *count = audio_index_ >= 0 ? 1 : 0;
    }

    void __stdcall SelectAudioStream(unsigned long number) override {
        audio_disabled_.store(number != 0 || audio_index_ < 0);
    }

    void __stdcall GetEnableAudioStreamNum(long* number) override {
        if (number) *number = audio_disabled_.load() ? -1 :
                              (audio_index_ >= 0 ? 0 : -1);
    }

    void __stdcall DisableAudioStream() override {
        audio_disabled_.store(true);
        if (audio_) audio_->Stop();
    }

    void __stdcall GetNumberOfVideoStream(unsigned long* count) override {
        if (count) *count = 1;
    }

    void __stdcall SelectVideoStream(unsigned long) override {}

    void __stdcall GetEnableVideoStreamNum(long* number) override {
        if (number) *number = 0;
    }

    void __stdcall SetMixingBitmap(tTVPBaseTexture*, float alpha) override {
        SetMixingMovieAlpha(alpha);
    }

    void __stdcall ResetMixingBitmap() override {}

    void __stdcall SetMixingMovieAlpha(float alpha) override {
        mixing_alpha_ = std::clamp(alpha, 0.0f, 1.0f);
        if (!layer_mode_) krkrvita_vitagl_set_video_alpha(mixing_alpha_);
    }

    void __stdcall GetMixingMovieAlpha(float* alpha) override {
        if (alpha) *alpha = mixing_alpha_;
    }

    void __stdcall SetMixingMovieBGColor(unsigned long color) override {
        mixing_bg_color_ = color;
    }

    void __stdcall GetMixingMovieBGColor(unsigned long* color) override {
        if (color) *color = mixing_bg_color_;
    }

    void __stdcall PresentVideoImage() override {}

    void __stdcall GetContrastRangeMin(float* value) override {
        if (value) *value = 0.0f;
    }
    void __stdcall GetContrastRangeMax(float* value) override {
        if (value) *value = 2.0f;
    }
    void __stdcall GetContrastDefaultValue(float* value) override {
        if (value) *value = 1.0f;
    }
    void __stdcall GetContrastStepSize(float* value) override {
        if (value) *value = 0.01f;
    }
    void __stdcall GetContrast(float* value) override {
        if (value) *value = contrast_;
    }
    void __stdcall SetContrast(float value) override { contrast_ = value; }

    void __stdcall GetBrightnessRangeMin(float* value) override {
        if (value) *value = -1.0f;
    }
    void __stdcall GetBrightnessRangeMax(float* value) override {
        if (value) *value = 1.0f;
    }
    void __stdcall GetBrightnessDefaultValue(float* value) override {
        if (value) *value = 0.0f;
    }
    void __stdcall GetBrightnessStepSize(float* value) override {
        if (value) *value = 0.01f;
    }
    void __stdcall GetBrightness(float* value) override {
        if (value) *value = brightness_;
    }
    void __stdcall SetBrightness(float value) override { brightness_ = value; }

    void __stdcall GetHueRangeMin(float* value) override {
        if (value) *value = -180.0f;
    }
    void __stdcall GetHueRangeMax(float* value) override {
        if (value) *value = 180.0f;
    }
    void __stdcall GetHueDefaultValue(float* value) override {
        if (value) *value = 0.0f;
    }
    void __stdcall GetHueStepSize(float* value) override {
        if (value) *value = 1.0f;
    }
    void __stdcall GetHue(float* value) override {
        if (value) *value = hue_;
    }
    void __stdcall SetHue(float value) override { hue_ = value; }

    void __stdcall GetSaturationRangeMin(float* value) override {
        if (value) *value = 0.0f;
    }
    void __stdcall GetSaturationRangeMax(float* value) override {
        if (value) *value = 2.0f;
    }
    void __stdcall GetSaturationDefaultValue(float* value) override {
        if (value) *value = 1.0f;
    }
    void __stdcall GetSaturationStepSize(float* value) override {
        if (value) *value = 0.01f;
    }
    void __stdcall GetSaturation(float* value) override {
        if (value) *value = saturation_;
    }
    void __stdcall SetSaturation(float value) override { saturation_ = value; }

    void SetLoopSegement(int begin_frame, int end_frame) override {
        loop_begin_frame_.store(std::max(0, begin_frame));
        loop_end_frame_.store(end_frame);
        loop_enabled_.store(true);
    }

private:
    static int read_packet(void* opaque, std::uint8_t* buffer, int size) {
        auto* self = static_cast<VitaMovieOverlay*>(opaque);
        ULONG read = 0;
        const HRESULT result = self->stream_->Read(
            buffer, static_cast<ULONG>(size), &read);
        if (result != S_OK) return AVERROR(EIO);
        return read ? static_cast<int>(read) : AVERROR_EOF;
    }

    static std::int64_t seek_stream(void* opaque, std::int64_t offset,
                                    int whence) {
        auto* self = static_cast<VitaMovieOverlay*>(opaque);
        if (whence == AVSEEK_SIZE)
            return static_cast<std::int64_t>(self->stream_size_);
        LARGE_INTEGER move{};
        move.QuadPart = offset;
        ULARGE_INTEGER result{};
        const DWORD origin = (whence & ~AVSEEK_FORCE) == SEEK_SET
                                 ? STREAM_SEEK_SET
                             : (whence & ~AVSEEK_FORCE) == SEEK_CUR
                                 ? STREAM_SEEK_CUR
                                 : STREAM_SEEK_END;
        if (self->stream_->Seek(move, origin, &result) != S_OK) return -1;
        return static_cast<std::int64_t>(result.QuadPart);
    }

    bool open_codec(int stream_index, AVCodecContext** output,
                    std::string& error) {
        AVStream* stream = format_->streams[stream_index];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            error = "decoder is not compiled into the Vita FFmpeg build";
            return false;
        }
        AVCodecContext* context = avcodec_alloc_context3(codec);
        if (!context) {
            error = "cannot allocate FFmpeg codec context";
            return false;
        }
        int result = avcodec_parameters_to_context(context, stream->codecpar);
        if (result >= 0) {
            // MPEG-1/2 and WMV3 are software-decoded on Vita. Two decoder
            // threads use the otherwise-idle CPU cores without contending
            // with the Yuri event thread for all three application cores.
            context->thread_count = 2;
            result = avcodec_open2(context, codec, nullptr);
        }
        if (result < 0) {
            error = "cannot initialize decoder: " + ffmpeg_error(result);
            avcodec_free_context(&context);
            return false;
        }
        *output = context;
        return true;
    }

    void initialize_audio_output() {
        if (!audio_codec_ || audio_codec_->sample_rate <= 0) return;
        AVChannelLayout input_layout = audio_codec_->ch_layout;
        AVChannelLayout fallback_layout{};
        if (input_layout.nb_channels <= 0) {
            av_channel_layout_default(&fallback_layout, 2);
            input_layout = fallback_layout;
        }
        AVChannelLayout output_layout{};
        av_channel_layout_default(&output_layout, kMovieAudioChannels);
        const int result = swr_alloc_set_opts2(
            &swr_, &output_layout, AV_SAMPLE_FMT_S16, kMovieAudioRate,
            &input_layout, audio_codec_->sample_fmt,
            audio_codec_->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&output_layout);
        av_channel_layout_uninit(&fallback_layout);
        if (result < 0 || !swr_ || swr_init(swr_) < 0) {
            if (swr_) swr_free(&swr_);
            return;
        }
        tTVPWaveFormat format{};
        format.SamplesPerSec = kMovieAudioRate;
        format.Channels = kMovieAudioChannels;
        format.BitsPerSample = 16;
        format.BytesPerSample = 2;
        format.TotalSamples = 0;
        format.TotalTime = total_time_ms_;
        format.SpeakerConfig = 0;
        format.IsFloat = false;
        format.Seekable = true;
        audio_ = TVPCreateSoundBuffer(format, kMovieAudioBuffers);
    }

    void request_seek(std::uint64_t position_ms) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            seek_position_ms_ = position_ms;
            seek_pending_ = true;
            position_ms_ = position_ms;
            current_frame_ = static_cast<int>(
                std::llround(position_ms * fps_ / 1000.0));
            clock_position_ms_ = position_ms;
            clock_started_ = std::chrono::steady_clock::now();
            ended_ = false;
        }
        state_cv_.notify_all();
        frame_cv_.notify_all();
    }

    void update_clock_position_locked() {
        if (!playing_) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - clock_started_).count();
        const double advanced = elapsed * play_rate_;
        position_ms_ = clock_position_ms_ +
            static_cast<std::uint64_t>(std::max(0.0, advanced));
        if (total_time_ms_ > 0)
            position_ms_ = std::min<std::uint64_t>(position_ms_, total_time_ms_);
    }

    bool wait_for_timestamp(std::uint64_t timestamp_ms) {
        std::unique_lock<std::mutex> lock(state_mutex_);
        for (;;) {
            if (terminate_.load() || seek_pending_) return false;
            if (!playing_) {
                state_cv_.wait(lock, [this] {
                    return terminate_.load() || playing_ || seek_pending_;
                });
                continue;
            }
            const double delta_ms = timestamp_ms > clock_position_ms_
                                        ? (timestamp_ms - clock_position_ms_) /
                                              play_rate_
                                        : 0.0;
            const auto target = clock_started_ +
                std::chrono::milliseconds(static_cast<std::int64_t>(delta_ms));
            const auto now = std::chrono::steady_clock::now();
            if (now >= target) {
                position_ms_ = timestamp_ms;
                return true;
            }
            state_cv_.wait_until(lock, target);
        }
    }

    std::uint64_t frame_timestamp_ms(const AVFrame* frame) const {
        std::int64_t timestamp = frame->best_effort_timestamp;
        if (timestamp == AV_NOPTS_VALUE)
            return static_cast<std::uint64_t>(
                std::llround(decoded_frames_ * 1000.0 / fps_));
        if (video_stream_->start_time != AV_NOPTS_VALUE)
            timestamp -= video_stream_->start_time;
        if (timestamp < 0) timestamp = 0;
        return static_cast<std::uint64_t>(av_rescale_q(
            timestamp, video_stream_->time_base, AVRational{1, 1000}));
    }

    bool publish_video_frame(AVFrame* frame) {
        sws_ = sws_getCachedContext(
            sws_, frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format), width_, height_,
            AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) return false;
        std::uint8_t* output[] = {rgba_.data(), nullptr, nullptr, nullptr};
        int output_pitch[] = {width_ * 4, 0, 0, 0};
        if (sws_scale(sws_, frame->data, frame->linesize, 0, frame->height,
                      output, output_pitch) != height_)
            return false;

        const std::uint64_t timestamp_ms = frame_timestamp_ms(frame);
        if (!wait_for_timestamp(timestamp_ms)) return false;
        const int frame_number = static_cast<int>(
            std::llround(timestamp_ms * fps_ / 1000.0));
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            current_frame_ = frame_number;
            position_ms_ = timestamp_ms;
        }

        if (layer_mode_) {
            std::unique_lock<std::mutex> lock(frame_mutex_);
            frame_cv_.wait(lock, [this] {
                return terminate_.load() || !layer_frame_ready_ ||
                       !playing_snapshot();
            });
            if (terminate_.load()) return false;
            layer_rgba_ = rgba_;
            layer_frame_ready_ = true;
        } else {
            const std::uint64_t serial = ++frame_serial_;
            if (!krkrvita_vitagl_submit_video_frame(
                    rgba_.data(), width_ * 4, width_, height_, serial))
                return false;
        }
        if (!first_movie_frame_decoded.exchange(true))
            krkrvita_boot_trace("yuri-first-movie-frame-decoded");

        if (callback_) {
            NativeEvent event(WM_GRAPHNOTIFY);
            event.WParam = EC_UPDATE;
            event.LParam = frame_number;
            callback_->PostEvent(event);
        }
        const int stop_frame = stop_frame_.load();
        if (stop_frame >= 0 && frame_number >= stop_frame) {
            post_complete();
            return false;
        }
        const int loop_end = loop_end_frame_.load();
        if (loop_enabled_.load() && loop_end >= 0 && frame_number >= loop_end) {
            request_seek(static_cast<std::uint64_t>(std::llround(
                loop_begin_frame_.load() * 1000.0 / fps_)));
            return false;
        }
        return true;
    }

    bool playing_snapshot() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return playing_;
    }

    void publish_audio_frame(AVFrame* frame) {
        if (!audio_ || !swr_ || audio_disabled_.load()) return;
        const std::int64_t delay = swr_get_delay(swr_, audio_codec_->sample_rate);
        const int maximum = static_cast<int>(av_rescale_rnd(
            delay + frame->nb_samples, kMovieAudioRate,
            audio_codec_->sample_rate, AV_ROUND_UP));
        if (maximum <= 0) return;
        audio_pcm_.resize(static_cast<std::size_t>(maximum) *
                          kMovieAudioChannels * sizeof(std::int16_t));
        std::uint8_t* destination = audio_pcm_.data();
        const int converted = swr_convert(
            swr_, &destination, maximum,
            const_cast<const std::uint8_t**>(frame->extended_data),
            frame->nb_samples);
        if (converted <= 0) return;

        while (!terminate_.load() && playing_snapshot() &&
               !audio_->IsBufferValid())
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (terminate_.load() || !playing_snapshot()) return;
        audio_->AppendBuffer(audio_pcm_.data(),
            static_cast<unsigned int>(converted * kMovieAudioChannels *
                                      sizeof(std::int16_t)));
        if (!first_movie_audio_decoded.exchange(true))
            krkrvita_boot_trace("yuri-first-movie-audio-decoded");
    }

    void decode_packet(AVPacket* packet) {
        AVCodecContext* codec = nullptr;
        const bool video = packet->stream_index == video_index_;
        if (video)
            codec = video_codec_;
        else if (packet->stream_index == audio_index_)
            codec = audio_codec_;
        else
            return;
        if (!codec || avcodec_send_packet(codec, packet) < 0) return;
        while (!terminate_.load()) {
            const int result = avcodec_receive_frame(codec, frame_);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
            if (result < 0) break;
            if (video) {
                ++decoded_frames_;
                if (!publish_video_frame(frame_)) {
                    av_frame_unref(frame_);
                    break;
                }
            } else {
                publish_audio_frame(frame_);
            }
            av_frame_unref(frame_);
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (seek_pending_) break;
        }
    }

    void perform_seek(std::uint64_t position_ms) {
        std::int64_t target = static_cast<std::int64_t>(position_ms) * 1000;
        if (format_->start_time != AV_NOPTS_VALUE) target += format_->start_time;
        av_seek_frame(format_, -1, target, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(video_codec_);
        if (audio_codec_) avcodec_flush_buffers(audio_codec_);
        if (swr_) {
            swr_close(swr_);
            swr_init(swr_);
        }
        if (audio_) {
            audio_->Reset();
            if (playing_snapshot() && !audio_disabled_.load()) audio_->Play();
        }
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            layer_frame_ready_ = false;
        }
        frame_cv_.notify_all();
        decoded_frames_ = static_cast<std::uint64_t>(
            std::llround(position_ms * fps_ / 1000.0));
    }

    void post_complete() {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            playing_ = false;
            ended_ = true;
            status_ = vsEnded;
        }
        if (audio_) audio_->Stop();
        if (callback_) {
            NativeEvent event(WM_GRAPHNOTIFY);
            event.WParam = EC_COMPLETE;
            event.LParam = 0;
            callback_->PostEvent(event);
        }
    }

    void handle_end_of_stream() {
        if (loop_enabled_.load()) {
            request_seek(static_cast<std::uint64_t>(std::llround(
                loop_begin_frame_.load() * 1000.0 / fps_)));
        } else {
            post_complete();
        }
    }

    void decode_loop() {
        try {
            while (!terminate_.load()) {
                std::uint64_t seek_position = 0;
                bool perform_pending_seek = false;
                bool should_decode = false;
                {
                    std::unique_lock<std::mutex> lock(state_mutex_);
                    state_cv_.wait(lock, [this] {
                        return terminate_.load() || playing_ || seek_pending_;
                    });
                    if (terminate_.load()) break;
                    if (seek_pending_) {
                        seek_position = seek_position_ms_;
                        seek_pending_ = false;
                        perform_pending_seek = true;
                    }
                    should_decode = playing_;
                }
                // Zero is a real seek target. Keep an explicit flag rather
                // than using the target value as a sentinel, and never read
                // mutable clock state outside state_mutex_.
                if (perform_pending_seek) perform_seek(seek_position);
                if (!should_decode || !playing_snapshot()) continue;

                const int result = av_read_frame(format_, packet_);
                if (result == AVERROR_EOF) {
                    handle_end_of_stream();
                    continue;
                }
                if (result < 0) {
                    TVPAddImportantLog(
                        TJS_W("(error) FFmpeg movie demux failed"));
                    post_complete();
                    continue;
                }
                decode_packet(packet_);
                av_packet_unref(packet_);
            }
        } catch (const std::bad_alloc&) {
            TVPAddImportantLog(
                TJS_W("(error) FFmpeg movie decode ran out of memory"));
            if (!terminate_.load()) post_complete();
        } catch (...) {
            TVPAddImportantLog(
                TJS_W("(error) FFmpeg movie decode failed unexpectedly"));
            if (!terminate_.load()) post_complete();
        }
    }

    std::atomic<unsigned int> ref_count_{1};
    tTJSNI_VideoOverlay* callback_ = nullptr;
    tTJSNI_Window* window_ = nullptr;
    IStream* stream_ = nullptr;
    std::uint64_t stream_size_ = 0;
    bool layer_mode_ = false;

    AVFormatContext* format_ = nullptr;
    AVIOContext* avio_ = nullptr;
    AVCodecContext* video_codec_ = nullptr;
    AVCodecContext* audio_codec_ = nullptr;
    AVStream* video_stream_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwsContext* sws_ = nullptr;
    SwrContext* swr_ = nullptr;
    int video_index_ = -1;
    int audio_index_ = -1;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 30.0;
    std::uint64_t total_time_ms_ = 0;
    std::uint64_t decoded_frames_ = 0;
    std::uint64_t frame_serial_ = 0;

    std::thread worker_;
    std::atomic<bool> terminate_{false};
    std::mutex state_mutex_;
    std::condition_variable state_cv_;
    bool playing_ = false;
    bool ended_ = false;
    bool seek_pending_ = false;
    std::uint64_t seek_position_ms_ = 0;
    std::uint64_t position_ms_ = 0;
    std::uint64_t clock_position_ms_ = 0;
    std::chrono::steady_clock::time_point clock_started_{};
    double play_rate_ = 1.0;
    int current_frame_ = 0;
    tTVPVideoStatus status_ = vsStopped;

    std::vector<std::uint8_t> rgba_;
    std::vector<std::uint8_t> layer_rgba_;
    std::vector<std::uint8_t> display_rgba_;
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    bool layer_frame_ready_ = false;
    tTVPBaseTexture* video_buffers_[2]{};
    int front_buffer_ = 0;

    iTVPSoundBuffer* audio_ = nullptr;
    std::vector<std::uint8_t> audio_pcm_;
    std::atomic<bool> audio_disabled_{false};
    long audio_balance_ = 0;
    long audio_volume_ = 100000;

    std::atomic<int> stop_frame_{-1};
    std::atomic<bool> loop_enabled_{false};
    std::atomic<int> loop_begin_frame_{0};
    std::atomic<int> loop_end_frame_{-1};
    std::atomic<bool> visible_{false};
    int rect_left_ = 0;
    int rect_top_ = 0;
    int rect_right_ = 0;
    int rect_bottom_ = 0;
    float mixing_alpha_ = 1.0f;
    unsigned long mixing_bg_color_ = 0;
    float contrast_ = 1.0f;
    float brightness_ = 0.0f;
    float hue_ = 0.0f;
    float saturation_ = 1.0f;
};

void create_movie(tTJSNI_VideoOverlay* callback, IStream* stream,
                  std::uint64_t size, bool layer_mode,
                  iTVPVideoOverlay** output) {
    if (output) *output = nullptr;
    auto* movie = new VitaMovieOverlay(callback, stream, size, layer_mode);
    std::string error;
    if (!movie->initialize(error)) {
        TVPAddImportantLog(TJS_W("(error) Vita movie backend initialization failed"));
        delete movie;
        TVPThrowExceptionMessage(
            TJS_W("The Vita FFmpeg movie backend could not open this movie"));
    }
    if (output) *output = movie;
}

} // namespace

#ifdef KRKRVITA_LOCAL_STDCALL
#undef __stdcall
#undef KRKRVITA_LOCAL_STDCALL
#endif

void GetVideoOverlayObject(tTJSNI_VideoOverlay* callback, IStream* stream,
                           const tjs_char*, const tjs_char*, std::uint64_t size,
                           iTVPVideoOverlay** output) {
    create_movie(callback, stream, size, false, output);
}

void GetVideoLayerObject(tTJSNI_VideoOverlay* callback, IStream* stream,
                         const tjs_char*, const tjs_char*, std::uint64_t size,
                         iTVPVideoOverlay** output) {
    create_movie(callback, stream, size, true, output);
}

void GetMixingVideoOverlayObject(tTJSNI_VideoOverlay* callback, IStream* stream,
                                 const tjs_char*, const tjs_char*,
                                 std::uint64_t size,
                                 iTVPVideoOverlay** output) {
    create_movie(callback, stream, size, false, output);
}

void GetMFVideoOverlayObject(tTJSNI_VideoOverlay* callback, IStream* stream,
                             const tjs_char*, const tjs_char*,
                             std::uint64_t size,
                             iTVPVideoOverlay** output) {
    create_movie(callback, stream, size, false, output);
}

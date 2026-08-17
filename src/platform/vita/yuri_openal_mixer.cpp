#include "tjsCommHead.h"

#include "DebugIntf.h"
#include "WaveMixer.h"
#include "krkrvita/audio_playback_state.hpp"
#include "krkrvita/retail_bootstrap.hpp"

#include <AL/al.h>
#include <AL/alc.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

std::mutex renderer_mutex;
ALCdevice* renderer_device = nullptr;
ALCcontext* renderer_context = nullptr;
std::atomic<bool> first_buffer_queued{false};
std::atomic<bool> first_play_started{false};
std::atomic<bool> first_fresh_stream_rewound{false};

void clear_openal_error() {
    while (alGetError() != AL_NO_ERROR) {}
}

bool make_context_current() {
    if (!renderer_context) return false;
    if (alcGetCurrentContext() == renderer_context) return true;
    return alcMakeContextCurrent(renderer_context) == ALC_TRUE;
}

bool initialize_renderer() {
    std::lock_guard<std::mutex> lock(renderer_mutex);
    if (renderer_context) return make_context_current();

    renderer_device = alcOpenDevice(nullptr);
    if (!renderer_device) {
        TVPAddImportantLog(TJS_W("(error) Vita OpenAL device initialization failed"));
        return false;
    }
    renderer_context = alcCreateContext(renderer_device, nullptr);
    if (!renderer_context || !make_context_current()) {
        if (renderer_context) alcDestroyContext(renderer_context);
        alcCloseDevice(renderer_device);
        renderer_context = nullptr;
        renderer_device = nullptr;
        TVPAddImportantLog(TJS_W("(error) Vita OpenAL context initialization failed"));
        return false;
    }
    TVPAddImportantLog(TJS_W("(info) Vita OpenAL sound backend initialized"));
    krkrvita_boot_trace("yuri-openal-initialized");
    return true;
}

ALenum openal_format(const tTVPWaveFormat& format) {
    if (format.IsFloat || (format.BitsPerSample != 8 &&
                           format.BitsPerSample != 16))
        return 0;
    if (format.Channels == 1)
        return format.BitsPerSample == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
    if (format.Channels == 2)
        return format.BitsPerSample == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
    return 0;
}

class VitaOpenALSoundBuffer final : public iTVPSoundBuffer {
public:
    VitaOpenALSoundBuffer(const tTVPWaveFormat& format, int buffer_count,
                          ALenum al_format)
        : format_(format), al_format_(al_format),
          frame_size_(format.BytesPerSample * format.Channels),
          buffer_ids_(std::max(2, buffer_count)) {
        make_context_current();
        alGenSources(1, &source_);
        alGenBuffers(static_cast<ALsizei>(buffer_ids_.size()),
                     buffer_ids_.data());
        if (alGetError() != AL_NO_ERROR) return;
        for (ALuint id : buffer_ids_) free_ids_.push_back(id);
        alSourcei(source_, AL_LOOPING, AL_FALSE);
        // Kirikiri audio is screen-relative. In particular, mono voices must
        // not inherit OpenAL's world-space listener transform.
        alSourcei(source_, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcef(source_, AL_GAIN, 1.0f);
        valid_ = alGetError() == AL_NO_ERROR;
    }

    ~VitaOpenALSoundBuffer() override {
        std::lock_guard<std::mutex> lock(mutex_);
        make_context_current();
        reset_locked();
        if (!buffer_ids_.empty())
            alDeleteBuffers(static_cast<ALsizei>(buffer_ids_.size()),
                            buffer_ids_.data());
        if (source_) alDeleteSources(1, &source_);
    }

    bool valid() const { return valid_; }
    void Release() override { delete this; }

    void Play() override {
        std::lock_guard<std::mutex> lock(mutex_);
        playback_.play();
        if (!valid_ || !make_context_current()) return;
        if (start_if_requested_locked() &&
            !first_play_started.exchange(true)) {
            krkrvita_boot_trace("yuri-audio-first-play-started");
        }
    }

    void Pause() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid_ || !make_context_current()) return;
        alSourcePause(source_);
        playback_.pause();
    }

    void Stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid_ || !make_context_current()) return;
        reset_locked();
    }

    void Reset() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid_ || !make_context_current()) return;
        reset_locked();
    }

    bool IsPlaying() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return playback_.requested();
    }

    void SetVolume(float volume) override {
        std::lock_guard<std::mutex> lock(mutex_);
        volume_ = std::max(0.0f, volume);
        if (valid_ && make_context_current())
            alSourcef(source_, AL_GAIN, volume_);
    }

    float GetVolume() override { return volume_; }

    void SetPan(float pan) override {
        std::lock_guard<std::mutex> lock(mutex_);
        pan_ = std::clamp(pan, -1.0f, 1.0f);
        if (valid_ && make_context_current())
            alSource3f(source_, AL_POSITION, pan_, 0.0f, 0.0f);
    }

    float GetPan() override { return pan_; }

    void AppendBuffer(const void* data, unsigned int length) override {
        if (!data || !length) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid_ || !make_context_current()) return;
        reclaim_processed_locked();
        if (free_ids_.empty()) return;

        const ALuint id = free_ids_.front();
        free_ids_.pop_front();
        alBufferData(id, al_format_, data, static_cast<ALsizei>(length),
                     static_cast<ALsizei>(format_.SamplesPerSec));
        if (alGetError() != AL_NO_ERROR) {
            free_ids_.push_front(id);
            return;
        }
        alSourceQueueBuffers(source_, 1, &id);
        if (alGetError() != AL_NO_ERROR) {
            free_ids_.push_front(id);
            return;
        }
        queued_ids_.push_back(id);
        buffer_sizes_[id] = length;
        if (!first_buffer_queued.exchange(true)) {
            krkrvita_boot_trace("yuri-audio-first-buffer-queued");
        }

        start_if_requested_locked();
    }

    bool IsBufferValid() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid_ || !make_context_current()) return false;
        reclaim_processed_locked();
        return !free_ids_.empty();
    }

    tjs_uint GetCurrentPlaySamples() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid_ || !make_context_current()) return played_samples_;
        reclaim_processed_locked();
        ALint offset = 0;
        alGetSourcei(source_, AL_SAMPLE_OFFSET, &offset);
        return played_samples_ + std::max(0, offset);
    }

    tjs_uint GetLatencySamples() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid_ || !make_context_current() || frame_size_ <= 0) return 0;
        reclaim_processed_locked();
        std::uint64_t bytes = 0;
        for (ALuint id : queued_ids_) bytes += buffer_sizes_[id];
        ALint offset = 0;
        alGetSourcei(source_, AL_BYTE_OFFSET, &offset);
        if (offset > 0 && bytes >= static_cast<std::uint64_t>(offset))
            bytes -= static_cast<std::uint64_t>(offset);
        return static_cast<tjs_uint>(bytes / frame_size_);
    }

    float GetLatencySeconds() override {
        return format_.SamplesPerSec
                   ? static_cast<float>(GetLatencySamples()) /
                         format_.SamplesPerSec
                   : 0.0f;
    }

    int GetRemainBuffers() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid_ || !make_context_current()) return 0;
        reclaim_processed_locked();
        return static_cast<int>(queued_ids_.size());
    }

    void SetPosition(float x, float y, float z) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (valid_ && make_context_current())
            alSource3f(source_, AL_POSITION, x, y, z);
    }

private:
    bool start_if_requested_locked() {
        if (queued_ids_.empty()) return false;
        ALint device_state = AL_STOPPED;
        alGetSourcei(source_, AL_SOURCE_STATE, &device_state);
        const krkrvita::AudioStartAction action = playback_.start_action(
            true, device_state == AL_PLAYING);
        if (action == krkrvita::AudioStartAction::none)
            return playback_.requested() && device_state == AL_PLAYING;

        // alSourceStop leaves a streaming source positioned at the end of its
        // old queue. Detaching and re-queueing buffers does not provide the
        // strong sample-zero guarantee needed by short voice clips, so a new
        // line is explicitly rewound. Pause/resume deliberately skips this.
        clear_openal_error();
        if (action == krkrvita::AudioStartAction::rewind_and_start) {
            alSourceRewind(source_);
            alSourcei(source_, AL_SAMPLE_OFFSET, 0);
            if (alGetError() != AL_NO_ERROR) return false;
            if (!first_fresh_stream_rewound.exchange(true))
                krkrvita_boot_trace("yuri-audio-fresh-stream-rewound");
        }
        alSourcePlay(source_);
        if (alGetError() != AL_NO_ERROR) return false;
        playback_.started();
        return true;
    }

    void reclaim_processed_locked() {
        ALint processed = 0;
        alGetSourcei(source_, AL_BUFFERS_PROCESSED, &processed);
        while (processed-- > 0 && !queued_ids_.empty()) {
            ALuint id = 0;
            alSourceUnqueueBuffers(source_, 1, &id);
            if (alGetError() != AL_NO_ERROR) break;
            const auto size = buffer_sizes_.find(id);
            if (size != buffer_sizes_.end()) {
                played_samples_ += size->second / frame_size_;
                buffer_sizes_.erase(size);
            }
            const auto queued = std::find(queued_ids_.begin(), queued_ids_.end(), id);
            if (queued != queued_ids_.end()) queued_ids_.erase(queued);
            free_ids_.push_back(id);
        }
    }

    void reset_locked() {
        if (!source_) return;
        alSourceStop(source_);
        alSourceRewind(source_);
        alSourcei(source_, AL_BUFFER, 0);
        queued_ids_.clear();
        free_ids_.clear();
        buffer_sizes_.clear();
        for (ALuint id : buffer_ids_) free_ids_.push_back(id);
        played_samples_ = 0;
        playback_.reset();
    }

    std::mutex mutex_;
    tTVPWaveFormat format_{};
    ALenum al_format_ = 0;
    int frame_size_ = 0;
    ALuint source_ = 0;
    std::vector<ALuint> buffer_ids_;
    std::deque<ALuint> free_ids_;
    std::deque<ALuint> queued_ids_;
    std::unordered_map<ALuint, unsigned int> buffer_sizes_;
    tjs_uint played_samples_ = 0;
    float volume_ = 1.0f;
    float pan_ = 0.0f;
    krkrvita::AudioPlaybackState playback_;
    bool valid_ = false;
};

} // namespace

void TVPInitDirectSound(int) { initialize_renderer(); }

void TVPUninitDirectSound() {
    std::lock_guard<std::mutex> lock(renderer_mutex);
    if (renderer_context) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(renderer_context);
        renderer_context = nullptr;
    }
    if (renderer_device) {
        alcCloseDevice(renderer_device);
        renderer_device = nullptr;
    }
}

iTVPSoundBuffer* TVPCreateSoundBuffer(tTVPWaveFormat& format,
                                       int buffer_count) {
    const ALenum al_format = openal_format(format);
    if (!al_format || !initialize_renderer()) return nullptr;
    auto* buffer = new VitaOpenALSoundBuffer(format, buffer_count, al_format);
    if (!buffer->valid()) {
        delete buffer;
        return nullptr;
    }
    return buffer;
}

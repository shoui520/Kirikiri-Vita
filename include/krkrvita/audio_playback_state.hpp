#pragma once

namespace krkrvita {

enum class AudioStartAction {
    none,
    resume,
    rewind_and_start,
};

// Kirikiri's playback state describes the engine's intent, not a transient
// device snapshot. A fresh stream must explicitly start at sample zero, while
// a stream resumed from pause must retain its current sample position.
class AudioPlaybackState {
public:
    void play() { requested_ = true; }
    void pause() { requested_ = false; }

    void reset() {
        requested_ = false;
        needs_rewind_ = true;
    }

    bool requested() const { return requested_; }

    AudioStartAction start_action(bool has_queued_audio,
                                  bool device_is_playing) const {
        if (!requested_ || !has_queued_audio || device_is_playing)
            return AudioStartAction::none;
        return needs_rewind_ ? AudioStartAction::rewind_and_start
                             : AudioStartAction::resume;
    }

    void started() { needs_rewind_ = false; }

private:
    bool requested_ = false;
    bool needs_rewind_ = true;
};

} // namespace krkrvita

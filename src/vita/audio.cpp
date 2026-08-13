#include "krkrvita/runtime.hpp"

#include <AL/al.h>
#include <AL/alc.h>

namespace krkrvita {

VitaAudio::~VitaAudio() {
    if (context_) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(static_cast<ALCcontext*>(context_));
    }
    if (device_) alcCloseDevice(static_cast<ALCdevice*>(device_));
}

bool VitaAudio::initialize(std::string* error) {
    auto* device = alcOpenDevice(nullptr);
    if (!device) {
        if (error) *error = "OpenAL device initialization failed";
        return false;
    }
    auto* context = alcCreateContext(device, nullptr);
    if (!context || !alcMakeContextCurrent(context)) {
        if (context) alcDestroyContext(context);
        alcCloseDevice(device);
        if (error) *error = "OpenAL context initialization failed";
        return false;
    }
    device_ = device;
    context_ = context;
    return true;
}

} // namespace krkrvita


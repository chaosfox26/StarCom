#ifndef _WIN32
#include "audio.h"

namespace sc {

struct AudioIO::Impl {
    bool muted = false;
};

AudioIO::AudioIO() : p_(new Impl) {}
AudioIO::~AudioIO(){ delete p_; }

bool AudioIO::start(CaptureFn, RenderFn, std::string& err){
    err = "voice calls are not available on this system yet";
    return false;
}

void AudioIO::stop(){}
void AudioIO::setMuted(bool m){ p_->muted = m; }
bool AudioIO::muted() const { return p_->muted; }

}
#endif

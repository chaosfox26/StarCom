#ifndef _WIN32
#include "audio.h"
#include "voice.h"
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

namespace sc {

namespace {

struct PaSampleSpec {
    int format;
    uint32_t rate;
    uint8_t channels;
};

struct PaBufferAttr {
    uint32_t maxlength, tlength, prebuf, minreq, fragsize;
};

const int kPaFloat32LE = 5, kPaPlayback = 1, kPaRecord = 2;
const int kSndPlayback = 0, kSndCapture = 1, kSndFloatLE = 14, kSndRwInterleaved = 3;
const uint32_t kPaDefault = (uint32_t)-1;
const uint32_t kFrameBytes = kVoiceFrame * sizeof(float);

using PaNew = void* (*)(const char*, const char*, int, const char*, const char*, const PaSampleSpec*, const void*, const PaBufferAttr*, int*);
using PaRead = int (*)(void*, void*, size_t, int*);
using PaWrite = int (*)(void*, const void*, size_t, int*);
using PaFree = void (*)(void*);
using SndOpen = int (*)(void**, const char*, int, int);
using SndSetParams = int (*)(void*, int, int, unsigned int, unsigned int, int, unsigned int);
using SndReadi = long (*)(void*, void*, unsigned long);
using SndWritei = long (*)(void*, const void*, unsigned long);
using SndRecover = int (*)(void*, int, int);
using SndClose = int (*)(void*);

template<class T> bool sym(void* lib, const char* name, T& out){
    out = reinterpret_cast<T>(dlsym(lib, name));
    return out != nullptr;
}

struct Pulse {
    bool ok = false;
    PaNew open = nullptr;
    PaRead read = nullptr;
    PaWrite write = nullptr;
    PaFree close = nullptr;
};

struct Alsa {
    bool ok = false;
    SndOpen open = nullptr;
    SndSetParams setParams = nullptr;
    SndReadi read = nullptr;
    SndWritei write = nullptr;
    SndRecover recover = nullptr;
    SndClose close = nullptr;
};

const Pulse& pulse(){
    static const Pulse p = []{
        Pulse x;
        void* lib = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
        if(!lib) return x;
        x.ok = sym(lib, "pa_simple_new", x.open) && sym(lib, "pa_simple_read", x.read) && sym(lib, "pa_simple_write", x.write) && sym(lib, "pa_simple_free", x.close);
        if(!x.ok) dlclose(lib);
        return x;
    }();
    return p;
}

const Alsa& alsa(){
    static const Alsa a = []{
        Alsa x;
        void* lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
        if(!lib) return x;
        x.ok = sym(lib, "snd_pcm_open", x.open) && sym(lib, "snd_pcm_set_params", x.setParams) && sym(lib, "snd_pcm_readi", x.read)
            && sym(lib, "snd_pcm_writei", x.write) && sym(lib, "snd_pcm_recover", x.recover) && sym(lib, "snd_pcm_close", x.close);
        if(!x.ok) dlclose(lib);
        return x;
    }();
    return a;
}

struct Device {
    int kind = 0;
    void* h = nullptr;
};

bool openDevice(bool capture, Device& d){
    const Pulse& p = pulse();
    if(p.ok){
        PaSampleSpec spec{kPaFloat32LE, (uint32_t)kVoiceRate, 1};
        PaBufferAttr attr{kPaDefault, kPaDefault, kPaDefault, kPaDefault, kPaDefault};
        if(capture) attr.fragsize = kFrameBytes;
        else { attr.tlength = 2 * kFrameBytes; attr.minreq = kFrameBytes; }
        int e = 0;
        void* h = p.open(nullptr, "StarCom", capture ? kPaRecord : kPaPlayback, nullptr, capture ? "microphone" : "voice call", &spec, nullptr, &attr, &e);
        if(h){ d.kind = 1; d.h = h; return true; }
    }
    const Alsa& a = alsa();
    if(a.ok){
        void* h = nullptr;
        if(a.open(&h, "default", capture ? kSndCapture : kSndPlayback, 0) == 0){
            if(a.setParams(h, kSndFloatLE, kSndRwInterleaved, 1, kVoiceRate, 1, 40000) == 0){ d.kind = 2; d.h = h; return true; }
            a.close(h);
        }
    }
    return false;
}

void closeDevice(Device& d){
    if(d.kind == 1) pulse().close(d.h);
    else if(d.kind == 2) alsa().close(d.h);
    d.kind = 0;
    d.h = nullptr;
}

bool readFrame(Device& d, float* frame){
    if(d.kind == 1){
        int e = 0;
        return pulse().read(d.h, frame, kFrameBytes, &e) >= 0;
    }
    const Alsa& a = alsa();
    unsigned long got = 0;
    int recovers = 0;
    while(got < (unsigned long)kVoiceFrame){
        long r = a.read(d.h, frame + got, (unsigned long)kVoiceFrame - got);
        if(r > 0){ got += (unsigned long)r; continue; }
        if(r == 0 || recovers++ >= 2 || a.recover(d.h, (int)r, 1) < 0) return false;
    }
    return true;
}

bool writeFrame(Device& d, const float* frame){
    if(d.kind == 1){
        int e = 0;
        return pulse().write(d.h, frame, kFrameBytes, &e) >= 0;
    }
    const Alsa& a = alsa();
    unsigned long put = 0;
    int recovers = 0;
    while(put < (unsigned long)kVoiceFrame){
        long r = a.write(d.h, frame + put, (unsigned long)kVoiceFrame - put);
        if(r > 0){ put += (unsigned long)r; continue; }
        if(r == 0 || recovers++ >= 2 || a.recover(d.h, (int)r, 1) < 0) return false;
    }
    return true;
}

void napMs(int ms){
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}

struct AudioIO::Impl {
    std::atomic<bool> run{false};
    std::atomic<bool> muted{false};
    std::atomic<int> capState{0}, renState{0};
    std::thread cap, ren;
    CaptureFn onFrame;
    RenderFn fill;
};

static void captureLoop(AudioIO::Impl* p){
    Device d;
    float frame[kVoiceFrame];
    if(!openDevice(true, d)){ p->capState = -1; return; }
    p->capState = 1;
    while(p->run){
        if(!d.h && !openDevice(true, d)){ napMs(500); continue; }
        if(!readFrame(d, frame)){ closeDevice(d); napMs(100); continue; }
        if(p->muted) std::memset(frame, 0, sizeof frame);
        if(p->onFrame) p->onFrame(frame);
    }
    closeDevice(d);
}

static void renderLoop(AudioIO::Impl* p){
    Device d;
    float frame[kVoiceFrame];
    if(!openDevice(false, d)){ p->renState = -1; return; }
    p->renState = 1;
    while(p->run){
        if(!d.h && !openDevice(false, d)){ napMs(500); continue; }
        if(p->fill) p->fill(frame); else std::memset(frame, 0, sizeof frame);
        if(!writeFrame(d, frame)){ closeDevice(d); napMs(100); }
    }
    closeDevice(d);
}

AudioIO::AudioIO() : p_(new Impl) {}

AudioIO::~AudioIO(){
    stop();
    delete p_;
}

bool AudioIO::start(CaptureFn onFrame, RenderFn fillFrame, std::string& err){
    stop();
    if(!pulse().ok && !alsa().ok){
        err = "no sound system found (PulseAudio, PipeWire or ALSA)";
        return false;
    }
    p_->onFrame = std::move(onFrame);
    p_->fill = std::move(fillFrame);
    p_->run = true;
    p_->capState = 0;
    p_->renState = 0;
    p_->cap = std::thread(captureLoop, p_);
    p_->ren = std::thread(renderLoop, p_);
    for(int i = 0; i < 300 && (p_->capState == 0 || p_->renState == 0) && p_->capState >= 0 && p_->renState >= 0; i++) napMs(10);
    if(p_->capState != 1 || p_->renState != 1){
        err = p_->capState < 0 ? "no microphone found" : p_->renState < 0 ? "no speakers or headphones found" : "audio did not start";
        stop();
        return false;
    }
    return true;
}

void AudioIO::stop(){
    p_->run = false;
    if(p_->cap.joinable()) p_->cap.join();
    if(p_->ren.joinable()) p_->ren.join();
}

void AudioIO::setMuted(bool m){ p_->muted = m; }
bool AudioIO::muted() const { return p_->muted; }

}
#endif

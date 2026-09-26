#ifdef _WIN32
#include "audio.h"
#include "voice.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

namespace sc {

struct AudioIO::Impl {
    std::atomic<bool> run{false};
    std::atomic<bool> muted{false};
    std::thread cap, ren;
    CaptureFn onFrame;
    RenderFn fill;
    std::string capErr, renErr;
    std::atomic<int> ready{0};
};

static WAVEFORMATEX wantFormat(){
    WAVEFORMATEX f;
    std::memset(&f, 0, sizeof f);
    f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    f.nChannels = 1;
    f.nSamplesPerSec = kVoiceRate;
    f.wBitsPerSample = 32;
    f.nBlockAlign = 4;
    f.nAvgBytesPerSec = kVoiceRate * 4;
    return f;
}

static bool openClient(EDataFlow flow, IMMDevice** dev, IAudioClient** ac, HANDLE ev, std::string& err){
    IMMDeviceEnumerator* en = nullptr;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))){ err = "no audio system"; return false; }
    HRESULT hr = en->GetDefaultAudioEndpoint(flow, eCommunications, dev);
    en->Release();
    if(FAILED(hr)){ err = flow == eCapture ? "no microphone found" : "no speakers or headphones found"; return false; }
    if(FAILED((*dev)->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)ac))){ err = "cannot open the audio device"; return false; }
    WAVEFORMATEX f = wantFormat();
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if(FAILED((*ac)->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 400000, 0, &f, nullptr))){ err = "the audio device refused 48 kHz mono"; return false; }
    if(FAILED((*ac)->SetEventHandle(ev))){ err = "audio event setup failed"; return false; }
    return true;
}

static void captureLoop(AudioIO::Impl* p){
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    IMMDevice* dev = nullptr;
    IAudioClient* ac = nullptr;
    IAudioCaptureClient* cc = nullptr;
    std::vector<float> acc;
    float frame[kVoiceFrame];
    if(!openClient(eCapture, &dev, &ac, ev, p->capErr) || FAILED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cc)) || FAILED(ac->Start())){
        if(p->capErr.empty()) p->capErr = "cannot start the microphone";
        p->ready = -1;
    } else {
        p->ready++;
        while(p->run){
            if(WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) continue;
            for(;;){
                BYTE* data = nullptr;
                UINT32 n = 0;
                DWORD flags = 0;
                if(cc->GetBuffer(&data, &n, &flags, nullptr, nullptr) != S_OK) break;
                const float* s = (const float*)data;
                for(UINT32 i = 0; i < n; i++) acc.push_back((flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.0f : s[i]);
                cc->ReleaseBuffer(n);
            }
            while(acc.size() >= (size_t)kVoiceFrame){
                std::memcpy(frame, acc.data(), sizeof frame);
                acc.erase(acc.begin(), acc.begin() + kVoiceFrame);
                if(p->muted) std::memset(frame, 0, sizeof frame);
                if(p->onFrame) p->onFrame(frame);
            }
        }
        ac->Stop();
    }
    if(cc) cc->Release();
    if(ac) ac->Release();
    if(dev) dev->Release();
    CloseHandle(ev);
    CoUninitialize();
}

static void renderLoop(AudioIO::Impl* p){
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    IMMDevice* dev = nullptr;
    IAudioClient* ac = nullptr;
    IAudioRenderClient* rc = nullptr;
    UINT32 bufFrames = 0;
    std::vector<float> pend;
    float frame[kVoiceFrame];
    if(!openClient(eRender, &dev, &ac, ev, p->renErr) || FAILED(ac->GetBufferSize(&bufFrames)) || FAILED(ac->GetService(__uuidof(IAudioRenderClient), (void**)&rc)) || FAILED(ac->Start())){
        if(p->renErr.empty()) p->renErr = "cannot start the speakers";
        p->ready = -1;
    } else {
        p->ready++;
        while(p->run){
            if(WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) continue;
            UINT32 padding = 0;
            if(FAILED(ac->GetCurrentPadding(&padding))) continue;
            UINT32 space = bufFrames - padding;
            while(pend.size() < space){
                if(p->fill) p->fill(frame); else std::memset(frame, 0, sizeof frame);
                pend.insert(pend.end(), frame, frame + kVoiceFrame);
                if(pend.size() >= (size_t)bufFrames) break;
            }
            UINT32 n = space < (UINT32)pend.size() ? space : (UINT32)pend.size();
            if(n == 0) continue;
            BYTE* data = nullptr;
            if(rc->GetBuffer(n, &data) != S_OK) continue;
            std::memcpy(data, pend.data(), (size_t)n * sizeof(float));
            rc->ReleaseBuffer(n, 0);
            pend.erase(pend.begin(), pend.begin() + n);
        }
        ac->Stop();
    }
    if(rc) rc->Release();
    if(ac) ac->Release();
    if(dev) dev->Release();
    CloseHandle(ev);
    CoUninitialize();
}

AudioIO::AudioIO() : p_(new Impl) {}

AudioIO::~AudioIO(){
    stop();
    delete p_;
}

bool AudioIO::start(CaptureFn onFrame, RenderFn fillFrame, std::string& err){
    p_->onFrame = std::move(onFrame);
    p_->fill = std::move(fillFrame);
    p_->run = true;
    p_->ready = 0;
    p_->cap = std::thread(captureLoop, p_);
    p_->ren = std::thread(renderLoop, p_);
    for(int i = 0; i < 300 && p_->ready != 2 && p_->ready >= 0; i++) Sleep(10);
    if(p_->ready != 2){
        err = !p_->capErr.empty() ? p_->capErr : !p_->renErr.empty() ? p_->renErr : "audio did not start";
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

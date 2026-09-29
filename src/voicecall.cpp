#include "voicecall.h"
#include "wire.h"
#include <chrono>
#include <cstring>

namespace sc {

namespace {

const uint8_t kRing = 1, kAccept = 2, kHangup = 3;
const uint32_t kRingTimeoutMs = 45000;
const uint32_t kLostMs = 15000;

}

VoiceCall::VoiceCall(Engine& e, bool lowerKey) : e_(e), lower_(lowerKey), play_(60) {
    e_.onReceive(kChanVoiceCtl, [this](Lane, const uint8_t* p, size_t n){ onControl(p, n); });
    e_.onReceive(kChanVoice, [this](Lane, const uint8_t* p, size_t n){
        if(state() == CallState::InCall) play_.push(p, n);
    });
    tick_ = std::thread([this]{ tickLoop(); });
}

VoiceCall::~VoiceCall(){
    if(state() == CallState::Calling || state() == CallState::Ringing || state() == CallState::InCall){
        uint64_t id = callId_;
        for(int i = 0; i < 3; i++) control(kHangup, id);
    }
    quit_ = true;
    if(tick_.joinable()) tick_.join();
    if(audioOn_){ audio_.stop(); audioOn_ = false; }
    e_.onReceive(kChanVoiceCtl, nullptr);
    e_.onReceive(kChanVoice, nullptr);
}

void VoiceCall::setOnChange(std::function<void()> fn){
    std::lock_guard<std::mutex> g(mu_);
    onChange_ = std::move(fn);
}

std::string VoiceCall::note() const {
    std::lock_guard<std::mutex> g(mu_);
    return note_;
}

int VoiceCall::seconds() const {
    if(state() != CallState::InCall) return 0;
    return (int)((engineNowMs() - callStartMs_.load()) / 1000);
}

VoiceStats VoiceCall::stats(){ return play_.stats(); }

void VoiceCall::setState(CallState s, const std::string& note){
    std::function<void()> fn;
    {
        std::lock_guard<std::mutex> g(mu_);
        state_ = (int)s;
        note_ = note;
        fn = onChange_;
    }
    if(s == CallState::InCall) callStartMs_ = engineNowMs();
    if(fn) fn();
}

void VoiceCall::control(uint8_t kind, uint64_t id){
    uint8_t b[9];
    b[0] = kind;
    putU64(b + 1, id);
    e_.send(Lane::Signal, kChanVoiceCtl, b, sizeof b);
}

void VoiceCall::call(){
    CallState s = state();
    if(s == CallState::Calling || s == CallState::InCall) return;
    if(s == CallState::Ringing){ accept(); return; }
    uint64_t id = 0;
    while(id == 0) sysRandom((uint8_t*)&id, sizeof id);
    callId_ = id;
    ringStartMs_ = lastRingMs_ = engineNowMs();
    setState(CallState::Calling, "Calling...");
    control(kRing, id);
}

void VoiceCall::accept(){
    if(state() != CallState::Ringing) return;
    control(kAccept, callId_);
    setState(CallState::InCall, "In call");
}

void VoiceCall::hangup(){
    CallState s = state();
    if(s != CallState::Calling && s != CallState::Ringing && s != CallState::InCall) return;
    uint64_t id = callId_;
    for(int i = 0; i < 3; i++) control(kHangup, id);
    setState(CallState::Ended, s == CallState::Ringing ? "You declined the call." : "You hung up.");
}

void VoiceCall::setMuted(bool m){
    muted_ = m;
    audio_.setMuted(m);
    std::function<void()> fn;
    { std::lock_guard<std::mutex> g(mu_); fn = onChange_; }
    if(fn) fn();
}

void VoiceCall::setDeafened(bool d){
    deafened_ = d;
    std::function<void()> fn;
    { std::lock_guard<std::mutex> g(mu_); fn = onChange_; }
    if(fn) fn();
}

void VoiceCall::onControl(const uint8_t* p, size_t n){
    if(n < 9) return;
    uint8_t kind = p[0];
    uint64_t id = getU64(p + 1);
    CallState s = state();
    if(kind == kRing){
        if(s == CallState::Idle || s == CallState::Ended){
            callId_ = id;
            ringStartMs_ = lastRingMs_ = engineNowMs();
            setState(CallState::Ringing, "Incoming call");
        } else if(s == CallState::Ringing && id == callId_){
            lastRingMs_ = engineNowMs();
        } else if(s == CallState::InCall && id == callId_){
            control(kAccept, id);
        } else if(s == CallState::Calling){
            if(!lower_){
                callId_ = id;
                control(kAccept, id);
                setState(CallState::InCall, "In call");
            }
        }
        return;
    }
    if(kind == kAccept){
        if(s == CallState::Calling && id == callId_) setState(CallState::InCall, "In call");
        return;
    }
    if(kind == kHangup){
        if(id != callId_) return;
        if(s == CallState::InCall) setState(CallState::Ended, "Your friend hung up.");
        else if(s == CallState::Calling) setState(CallState::Ended, "Your friend declined the call.");
        else if(s == CallState::Ringing) setState(CallState::Ended, "Missed call.");
    }
}

void VoiceCall::tickLoop(){
    uint32_t lastTune = 0;
    while(!quit_){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        bool wantAudio = state() == CallState::InCall;
        if(wantAudio && !audioOn_){
            std::string err;
            audioOn_ = audio_.start(
                [this](const float* frame){
                    uint8_t body[kVoiceHeader + kMaxOpusPacket];
                    size_t n = enc_.packet(frame, body, sizeof body);
                    if(n) e_.send(Lane::Voice, kChanVoice, body, n);
                },
                [this](float* frame){
                    play_.pull(frame);
                    if(deafened_) std::memset(frame, 0, sizeof(float) * kVoiceFrame);
                },
                err);
            if(audioOn_) audio_.setMuted(muted_);
            else { for(int i = 0; i < 3; i++) control(kHangup, callId_); setState(CallState::Ended, err.empty() ? "Your microphone or speakers could not start." : err); }
        }
        if(!wantAudio && audioOn_){ audio_.stop(); audioOn_ = false; }
        uint32_t now = engineNowMs();
        CallState s = state();
        if(s == CallState::Calling){
            if(now - ringStartMs_.load() >= kRingTimeoutMs){
                for(int i = 0; i < 3; i++) control(kHangup, callId_);
                setState(CallState::Ended, "No answer.");
            } else if(now - lastRingMs_.load() >= 1000){
                control(kRing, callId_);
                lastRingMs_ = now;
            }
        } else if(s == CallState::Ringing){
            if(now - lastRingMs_.load() >= 5000) setState(CallState::Ended, "Missed call.");
        } else if(s == CallState::InCall && now - lastTune >= 1000){
            lastTune = now;
            EngineStats st = e_.stats();
            pingMs_ = st.rttMs;
            enc_.setBitrate(st.voiceTargetKbps * 1000);
            enc_.setLossHint((int)st.lossPct);
            enc_.setFrameMs(st.voiceFrameMs);
            int target = 2 * st.jitterMs + st.voiceFrameMs;
            play_.setTargetMs(target < 40 ? 40 : target > 200 ? 200 : target);
            if(st.lastRecvAgeMs > (int)kLostMs){
                for(int i = 0; i < 3; i++) control(kHangup, callId_);
                setState(CallState::Ended, "Call lost: nothing heard from your friend for 15 seconds.");
            }
            std::function<void()> fn;
            { std::lock_guard<std::mutex> g(mu_); fn = onChange_; }
            if(fn) fn();
        }
    }
}

}

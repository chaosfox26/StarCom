#pragma once
#include "audio.h"
#include "engine.h"
#include "voice.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace sc {

enum class CallState { Idle, Calling, Ringing, InCall, Ended };

class VoiceCall {
public:
    VoiceCall(Engine& e, bool lowerKey);
    ~VoiceCall();
    VoiceCall(const VoiceCall&) = delete;
    VoiceCall& operator=(const VoiceCall&) = delete;
    void call();
    void accept();
    void hangup();
    void setMuted(bool m);
    bool muted() const { return muted_; }
    void setDeafened(bool d);
    bool deafened() const { return deafened_; }
    CallState state() const { return (CallState)state_.load(); }
    int seconds() const;
    int pingMs() const { return pingMs_; }
    std::string note() const;
    VoiceStats stats();
    void setOnChange(std::function<void()> fn);

private:
    Engine& e_;
    bool lower_;
    std::atomic<int> state_{(int)CallState::Idle};
    std::atomic<uint64_t> callId_{0};
    std::atomic<bool> muted_{false};
    std::atomic<bool> deafened_{false};
    std::atomic<bool> wantAudio_{false};
    std::atomic<int> pingMs_{0};
    std::atomic<uint32_t> ringStartMs_{0};
    std::atomic<uint32_t> lastRingMs_{0};
    std::atomic<uint32_t> callStartMs_{0};
    VoiceEncoder enc_;
    VoicePlayout play_;
    AudioIO audio_;
    bool audioOn_ = false;
    mutable std::mutex mu_;
    std::string note_;
    std::function<void()> onChange_;
    std::thread tick_;
    std::atomic<bool> quit_{false};
    void control(uint8_t kind, uint64_t id);
    void onControl(const uint8_t* p, size_t n);
    void setState(CallState s, const std::string& note);
    void tickLoop();
};

}

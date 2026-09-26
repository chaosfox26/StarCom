#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

namespace sc {

constexpr int kVoiceRate = 48000;
constexpr int kVoiceFrame = 960;
constexpr size_t kVoiceHeader = 9;
constexpr size_t kMaxOpusPacket = 400;

class VoiceEncoder {
public:
    VoiceEncoder();
    ~VoiceEncoder();
    VoiceEncoder(const VoiceEncoder&) = delete;
    VoiceEncoder& operator=(const VoiceEncoder&) = delete;
    bool ok() const { return enc_ != nullptr; }
    void setBitrate(int bps);
    void setLossHint(int pct);
    void setFrameMs(int ms);
    size_t encode(const float* pcm, int samples, uint8_t* out, size_t cap);
    size_t packet(const float* block20ms, uint8_t* out, size_t cap);
private:
    void* enc_ = nullptr;
    uint32_t seq_ = 0;
    uint32_t ts_ = 0;
    std::atomic<int> wantFrame_{kVoiceFrame};
    int frame_ = kVoiceFrame;
    int gathered_ = 0;
    float gather_[2 * kVoiceFrame];
    std::atomic<int> wantBps_{24000};
    std::atomic<int> wantLoss_{5};
    int haveBps_ = 24000;
    int haveLoss_ = 5;
};

struct VoiceStats {
    uint64_t received = 0;
    uint64_t played = 0;
    uint64_t concealed = 0;
    uint64_t recoveredFec = 0;
    uint64_t late = 0;
};

class VoicePlayout {
public:
    explicit VoicePlayout(int targetMs = 60);
    ~VoicePlayout();
    VoicePlayout(const VoicePlayout&) = delete;
    VoicePlayout& operator=(const VoicePlayout&) = delete;
    bool ok() const { return dec_ != nullptr; }
    bool push(const uint8_t* body, size_t n);
    void pull(float* block20ms);
    void setTargetMs(int ms);
    VoiceStats stats();
private:
    void* dec_ = nullptr;
    std::mutex mu_;
    std::map<uint32_t, std::vector<uint8_t>> q_;
    std::deque<float> pcm_;
    uint32_t next_ = 0;
    bool started_ = false;
    int targetMs_;
    int lastSamples_ = kVoiceFrame;
    int starving_ = 0;
    VoiceStats st_;
    int queuedSamples() const;
    void decodeNext();
};

}

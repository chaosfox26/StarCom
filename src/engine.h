#pragma once
#include "handshake.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace sc {

enum EngineChannel : uint8_t { kChanEngine = 0, kChanChat = 1, kChanVoice = 2, kChanVoiceCtl = 3 };

struct EngineStats {
    int rttMs = 0;
    int baseRttMs = 0;
    int jitterMs = 0;
    int peerJitterMs = 0;
    double lossPct = 0;
    int budgetKbps = 0;
    int laneKbps[4] = {0, 0, 0, 0};
    int voiceTargetKbps = 0;
    int voiceFrameMs = 20;
    const char* state = "clear";
    int lastRecvAgeMs = 0;
};

class Engine {
public:
    Engine(UdpSocket& s, Session& ses);
    bool send(Lane lane, uint8_t channel, const uint8_t* p, size_t n);
    void onReceive(uint8_t channel, std::function<void(Lane, const uint8_t*, size_t)> fn);
    bool poll(int timeoutMs, std::string& err);
    bool flush(int maxMs);
    EngineStats stats() const;
    int voiceTargetKbps() const;
    int voiceFrameMs() const;
    static constexpr size_t kFrameOverhead = 5;
    static constexpr size_t kMaxPayload = kMaxCircuitInner - kDataHeaderLen - kTagLen - kFrameOverhead;

private:
    struct Out {
        std::vector<uint8_t> plain;
        uint32_t queuedMs;
    };
    UdpSocket& s_;
    Session& ses_;
    mutable std::mutex mu_;
    std::deque<Out> q_[4];
    std::recursive_mutex rxMu_;
    std::function<void(Lane, const uint8_t*, size_t)> rx_[8];
    double tokens_ = 0;
    uint32_t lastRefillMs_ = 0;
    int budgetKbps_ = 4000;
    uint64_t laneBytes_[4] = {0, 0, 0, 0};
    int laneKbps_[4] = {0, 0, 0, 0};
    uint32_t laneWindowMs_ = 0;
    uint32_t lastMediaMs_ = 0;
    uint32_t lastReportMs_ = 0;
    uint32_t lastRecvMs_ = 0;
    uint32_t peerSendMs_ = 0;
    uint32_t peerSendArrivalMs_ = 0;
    bool havePeerSend_ = false;
    bool haveTransit_ = false;
    int32_t lastTransit_ = 0;
    double jitter_ = 0;
    int peerJitterMs_ = 0;
    double srtt_ = 0;
    struct RttSample { uint32_t atMs; int rtt; };
    std::deque<RttSample> rttHist_;
    double lossPct_ = 0;
    uint64_t highestNonce_ = 0;
    bool haveNonce_ = false;
    uint64_t nonceAtReport_ = 0;
    uint32_t recvSinceReport_ = 0;
    int voiceKbps_ = 32;
    int voiceFrameMs_ = 20;
    uint32_t lastVoiceRaiseMs_ = 0;
    uint32_t congestedSinceMs_ = 0;
    bool congested_ = false;
    uint32_t lastBudgetStepMs_ = 0;
    uint32_t lastBgForcedMs_ = 0;
    const char* state_ = "clear";

    bool transmitLocked(Lane lane, const std::vector<uint8_t>& plain);
    void scheduleLocked(uint32_t now);
    void refillLocked(uint32_t now);
    void sendReportLocked(uint32_t now);
    void handleReport(const uint8_t* p, size_t n, uint32_t now);
    void adapt(uint32_t now);
    int baseRtt(uint32_t now) const;
    size_t bucketDepth() const;
};

uint32_t engineNowMs();

}

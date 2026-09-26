#include "engine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace sc {

namespace {

const uint8_t kReport = 1;
const size_t kReportLen = 20;
const uint32_t kSilentMs = 75000;
const uint32_t kBaseWindowMs = 30000;
const int kMinBudget = 1000;
const int kMaxBudget = 20000;
const int kVoiceCap = 32;
const int kVoiceFloor = 8;
const size_t kWireExtra = 28 + kDataHeaderLen + kTagLen;
const size_t kLaneLimit[4] = {5, 256, 64, 256};
const bool kDropOldest[4] = {true, false, true, false};

void put32(uint8_t* p, uint32_t v){
    for(int i = 0; i < 4; i++){ p[i] = (uint8_t)v; v >>= 8; }
}

uint32_t get32(const uint8_t* p){
    uint32_t v = 0;
    for(int i = 3; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

}

uint32_t engineNowMs(){
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Engine::Engine(UdpSocket& s, Session& ses) : s_(s), ses_(ses){
    uint32_t now = engineNowMs();
    lastRefillMs_ = now;
    laneWindowMs_ = now;
    lastRecvMs_ = now;
    lastReportMs_ = now - 20000;
    lastMediaMs_ = now - 20000;
    lastVoiceRaiseMs_ = now;
    lastBudgetStepMs_ = now;
    tokens_ = (double)bucketDepth();
}

size_t Engine::bucketDepth() const {
    size_t d = (size_t)budgetKbps_ * 20 / 8;
    return std::max(d, (size_t)3 * kMaxPacket);
}

void Engine::refillLocked(uint32_t now){
    uint32_t dt = now - lastRefillMs_;
    lastRefillMs_ = now;
    tokens_ = std::min((double)bucketDepth(), tokens_ + (double)dt * budgetKbps_ / 8.0);
}

bool Engine::transmitLocked(Lane lane, const std::vector<uint8_t>& plainIn){
    std::vector<uint8_t> plain = plainIn;
    put32(plain.data() + 1, engineNowMs());
    uint8_t pkt[kMaxPacket];
    size_t len = 0;
    if(!sessionSeal(ses_, lane, plain.data(), plain.size(), pkt, sizeof pkt, len)) return false;
    s_.sendTo(pkt, len, ses_.peer);
    laneBytes_[(int)lane] += len + 28;
    return true;
}

void Engine::scheduleLocked(uint32_t now){
    refillLocked(now);
    for(;;){
        int pick = -1;
        bool forceBg = !q_[3].empty() && now - q_[3].front().queuedMs > 200 && now - lastBgForcedMs_ >= 50;
        if(forceBg) pick = 3;
        else for(int l = 0; l < 4; l++) if(!q_[l].empty()){ pick = l; break; }
        if(pick < 0) return;
        double need = (double)(q_[pick].front().plain.size() + kWireExtra);
        if(tokens_ < need) return;
        tokens_ -= need;
        if(forceBg) lastBgForcedMs_ = now;
        Out o = std::move(q_[pick].front());
        q_[pick].pop_front();
        transmitLocked((Lane)pick, o.plain);
    }
}

bool Engine::send(Lane lane, uint8_t channel, const uint8_t* p, size_t n){
    if(n > kMaxPayload || (int)lane > 3 || channel >= 8) return false;
    uint32_t now = engineNowMs();
    std::lock_guard<std::mutex> g(mu_);
    int l = (int)lane;
    if(q_[l].size() >= kLaneLimit[l]){
        if(!kDropOldest[l]) return false;
        q_[l].pop_front();
    }
    Out o;
    o.plain.resize(kFrameOverhead + n);
    o.plain[0] = channel;
    if(n) std::memcpy(o.plain.data() + kFrameOverhead, p, n);
    o.queuedMs = now;
    q_[l].push_back(std::move(o));
    if(lane == Lane::Voice || lane == Lane::Video) lastMediaMs_ = now;
    scheduleLocked(now);
    return true;
}

void Engine::onReceive(uint8_t channel, std::function<void(Lane, const uint8_t*, size_t)> fn){
    if(channel < 8) rx_[channel] = std::move(fn);
}

void Engine::sendReportLocked(uint32_t now){
    uint8_t r[kReportLen] = {0};
    r[0] = kReport;
    r[1] = havePeerSend_ ? 1 : 0;
    put32(r + 2, peerSendMs_);
    put32(r + 6, havePeerSend_ ? now - peerSendArrivalMs_ : 0);
    uint64_t expected = haveNonce_ ? highestNonce_ - nonceAtReport_ : 0;
    put32(r + 10, recvSinceReport_);
    put32(r + 14, (uint32_t)std::min<uint64_t>(expected, 0xffffffffu));
    int j = (int)std::lround(jitter_);
    r[18] = (uint8_t)std::min(j, 255);
    r[19] = 0;
    nonceAtReport_ = highestNonce_;
    recvSinceReport_ = 0;
    lastReportMs_ = now;
    Out o;
    o.plain.resize(kFrameOverhead + kReportLen);
    o.plain[0] = kChanEngine;
    std::memcpy(o.plain.data() + kFrameOverhead, r, kReportLen);
    o.queuedMs = now;
    if(q_[1].size() < kLaneLimit[1]) q_[1].push_back(std::move(o));
}

int Engine::baseRtt(uint32_t now) const {
    int b = 0;
    for(const RttSample& s : rttHist_) if(now - s.atMs <= kBaseWindowMs && (b == 0 || s.rtt < b)) b = s.rtt;
    return b;
}

void Engine::handleReport(const uint8_t* p, size_t n, uint32_t now){
    if(n < kReportLen || p[0] != kReport) return;
    std::lock_guard<std::mutex> g(mu_);
    if(p[1] & 1){
        uint32_t echo = get32(p + 2), hold = get32(p + 6);
        uint32_t rtt = now - echo - hold;
        if(rtt < 60000){
            srtt_ = srtt_ == 0 ? (double)rtt : srtt_ * 0.75 + rtt * 0.25;
            rttHist_.push_back({now, (int)rtt});
            while(!rttHist_.empty() && now - rttHist_.front().atMs > kBaseWindowMs) rttHist_.pop_front();
        }
    }
    uint32_t received = get32(p + 10), expected = get32(p + 14);
    lossPct_ = expected > received ? 100.0 * (expected - received) / expected : 0.0;
    peerJitterMs_ = p[18];
    adapt(now);
}

void Engine::adapt(uint32_t now){
    int base = baseRtt(now);
    int rtt = (int)std::lround(srtt_);
    bool cong = lossPct_ > 10.0 || (base > 0 && rtt > 2 * base + 50 && rtt > 150) || peerJitterMs_ > 80;
    bool strained = lossPct_ >= 2.0 || peerJitterMs_ >= 30;
    if(cong){
        if(!congested_) congestedSinceMs_ = now;
        congested_ = true;
        budgetKbps_ = std::max(kMinBudget, budgetKbps_ / 2);
        voiceKbps_ = std::max(kVoiceFloor, voiceKbps_ / 2);
        if(now - congestedSinceMs_ >= 5000) voiceFrameMs_ = 40;
        state_ = "congested";
    } else if(congested_ && strained){
        if(now - congestedSinceMs_ >= 5000) voiceFrameMs_ = 40;
        state_ = "congested";
    } else {
        congested_ = false;
        voiceFrameMs_ = 20;
        if(strained){
            state_ = "strained";
        } else {
            double secs = (now - lastBudgetStepMs_) / 1000.0;
            budgetKbps_ = std::min(kMaxBudget, (int)std::lround(budgetKbps_ * std::pow(1.05, std::max(0.0, secs))));
            if(now - lastVoiceRaiseMs_ >= 5000 && voiceKbps_ < kVoiceCap){
                voiceKbps_ = std::min(kVoiceCap, (int)std::ceil(voiceKbps_ * 1.1));
                lastVoiceRaiseMs_ = now;
            }
            state_ = "clear";
        }
    }
    if(voiceKbps_ >= kVoiceCap) lastVoiceRaiseMs_ = now;
    lastBudgetStepMs_ = now;
}

bool Engine::poll(int timeoutMs, std::string& err){
    uint32_t now = engineNowMs();
    int wait = timeoutMs;
    {
        std::lock_guard<std::mutex> g(mu_);
        scheduleLocked(now);
        for(int l = 0; l < 4; l++){
            if(q_[l].empty()) continue;
            double need = (double)(q_[l].front().plain.size() + kWireExtra) - tokens_;
            int ms = need <= 0 ? 0 : (int)std::ceil(need * 8.0 / budgetKbps_);
            wait = std::min(wait, std::max(1, ms));
            break;
        }
        uint32_t interval = now - lastMediaMs_ < 5000 ? 1000 : 10000;
        uint32_t due = now - lastReportMs_ >= interval ? 0 : interval - (now - lastReportMs_);
        wait = std::min(wait, (int)due);
    }
    uint8_t in[kMaxPacket], plain[kMaxPacket];
    Addr from;
    int r = s_.recvFrom(in, sizeof in, from, wait < 0 ? 0 : wait);
    now = engineNowMs();
    if(r > 0){
        Lane lane;
        size_t n = 0;
        bool ok;
        uint64_t nonce = 0;
        {
            std::lock_guard<std::mutex> g(mu_);
            ok = sessionOpen(ses_, in, (size_t)r, from, lane, plain, sizeof plain, n);
            if(ok){
                nonce = getU64(in + kHeaderLen + 8);
                lastRecvMs_ = now;
                if(!haveNonce_){ haveNonce_ = true; highestNonce_ = nonce; nonceAtReport_ = nonce - 1; }
                else if(nonce > highestNonce_) highestNonce_ = nonce;
                recvSinceReport_++;
                if(n >= kFrameOverhead){
                    int32_t transit = (int32_t)(now - get32(plain + 1));
                    if(haveTransit_){
                        double d = std::abs((double)(transit - lastTransit_));
                        jitter_ += (d - jitter_) / 16.0;
                    }
                    lastTransit_ = transit;
                    haveTransit_ = true;
                    peerSendMs_ = get32(plain + 1);
                    peerSendArrivalMs_ = now;
                    havePeerSend_ = true;
                    if(lane == Lane::Voice || lane == Lane::Video) lastMediaMs_ = now;
                }
            }
        }
        if(ok && n >= kFrameOverhead){
            uint8_t ch = plain[0];
            if(ch == kChanEngine) handleReport(plain + kFrameOverhead, n - kFrameOverhead, now);
            else if(ch < 8 && rx_[ch]) rx_[ch](lane, plain + kFrameOverhead, n - kFrameOverhead);
        }
    }
    {
        std::lock_guard<std::mutex> g(mu_);
        uint32_t interval = now - lastMediaMs_ < 5000 ? 1000 : 10000;
        if(now - lastReportMs_ >= interval) sendReportLocked(now);
        if(now - laneWindowMs_ >= 1000){
            uint32_t dt = now - laneWindowMs_;
            for(int l = 0; l < 4; l++){ laneKbps_[l] = (int)(laneBytes_[l] * 8 / dt); laneBytes_[l] = 0; }
            laneWindowMs_ = now;
        }
        scheduleLocked(now);
        if(now - lastRecvMs_ >= kSilentMs){
            err = "connection lost (no reply for " + std::to_string(kSilentMs / 1000) + " s)";
            return false;
        }
    }
    return true;
}

bool Engine::flush(int maxMs){
    uint32_t start = engineNowMs();
    for(;;){
        {
            std::lock_guard<std::mutex> g(mu_);
            bool empty = true;
            for(int l = 0; l < 4; l++) if(!q_[l].empty()) empty = false;
            if(empty) return true;
        }
        if((int)(engineNowMs() - start) >= maxMs) return false;
        std::string err;
        if(!poll(10, err)) return false;
    }
}

EngineStats Engine::stats() const {
    std::lock_guard<std::mutex> g(mu_);
    EngineStats s;
    uint32_t now = engineNowMs();
    s.rttMs = (int)std::lround(srtt_);
    s.baseRttMs = baseRtt(now);
    s.jitterMs = (int)std::lround(jitter_);
    s.peerJitterMs = peerJitterMs_;
    s.lossPct = lossPct_;
    s.budgetKbps = budgetKbps_;
    for(int l = 0; l < 4; l++) s.laneKbps[l] = laneKbps_[l];
    s.voiceTargetKbps = voiceKbps_;
    s.voiceFrameMs = voiceFrameMs_;
    s.state = state_;
    return s;
}

int Engine::voiceTargetKbps() const {
    std::lock_guard<std::mutex> g(mu_);
    return voiceKbps_;
}

int Engine::voiceFrameMs() const {
    std::lock_guard<std::mutex> g(mu_);
    return voiceFrameMs_;
}

}

#include "voice.h"
#include "wire.h"
#include <cstring>

extern "C" {
#include "../third_party/opus/include/opus.h"
}

namespace sc {

static void put32(uint8_t* p, uint32_t v){ for(int i = 0; i < 4; i++){ p[i] = (uint8_t)v; v >>= 8; } }
static uint32_t get32(const uint8_t* p){ uint32_t v = 0; for(int i = 3; i >= 0; i--) v = (v << 8) | p[i]; return v; }

VoiceEncoder::VoiceEncoder(){
    int err = 0;
    OpusEncoder* e = opus_encoder_create(kVoiceRate, 1, OPUS_APPLICATION_VOIP, &err);
    if(err != OPUS_OK || !e) return;
    opus_encoder_ctl(e, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(e, OPUS_SET_VBR(1));
    opus_encoder_ctl(e, OPUS_SET_VBR_CONSTRAINT(1));
    opus_encoder_ctl(e, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(e, OPUS_SET_DTX(1));
    opus_encoder_ctl(e, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(e, OPUS_SET_PACKET_LOSS_PERC(5));
    opus_encoder_ctl(e, OPUS_SET_BITRATE(24000));
    enc_ = e;
}

VoiceEncoder::~VoiceEncoder(){
    if(enc_) opus_encoder_destroy((OpusEncoder*)enc_);
}

void VoiceEncoder::setBitrate(int bps){
    if(bps < 6000) bps = 6000;
    if(bps > 510000) bps = 510000;
    wantBps_ = bps;
}

void VoiceEncoder::setLossHint(int pct){
    if(pct < 0) pct = 0;
    if(pct > 100) pct = 100;
    wantLoss_ = pct;
}

void VoiceEncoder::setFrameMs(int ms){
    wantFrame_ = ms >= 40 ? 2 * kVoiceFrame : kVoiceFrame;
}

size_t VoiceEncoder::encode(const float* pcm, int samples, uint8_t* out, size_t cap){
    if(!enc_) return 0;
    int b = wantBps_, l = wantLoss_;
    if(b != haveBps_){ opus_encoder_ctl((OpusEncoder*)enc_, OPUS_SET_BITRATE(b)); haveBps_ = b; }
    if(l != haveLoss_){ opus_encoder_ctl((OpusEncoder*)enc_, OPUS_SET_PACKET_LOSS_PERC(l)); haveLoss_ = l; }
    int n = opus_encode_float((OpusEncoder*)enc_, pcm, samples, out, (opus_int32)cap);
    return n > 0 ? (size_t)n : 0;
}

size_t VoiceEncoder::packet(const float* block20ms, uint8_t* out, size_t cap){
    if(cap < kVoiceHeader + 1) return 0;
    if(gathered_ == 0) frame_ = wantFrame_;
    std::memcpy(gather_ + gathered_, block20ms, sizeof(float) * kVoiceFrame);
    gathered_ += kVoiceFrame;
    if(gathered_ < frame_) return 0;
    size_t n = encode(gather_, frame_, out + kVoiceHeader, cap - kVoiceHeader);
    uint32_t seq = seq_++;
    uint32_t ts = ts_;
    ts_ += (uint32_t)frame_;
    gathered_ = 0;
    if(n <= 2) return 0;
    put32(out, seq);
    put32(out + 4, ts);
    out[8] = 0;
    return kVoiceHeader + n;
}

VoicePlayout::VoicePlayout(int targetMs) : targetMs_(targetMs < 20 ? 20 : targetMs) {
    int err = 0;
    OpusDecoder* d = opus_decoder_create(kVoiceRate, 1, &err);
    if(err == OPUS_OK && d) dec_ = d;
}

VoicePlayout::~VoicePlayout(){
    if(dec_) opus_decoder_destroy((OpusDecoder*)dec_);
}

void VoicePlayout::setTargetMs(int ms){
    std::lock_guard<std::mutex> lk(mu_);
    targetMs_ = ms < 20 ? 20 : ms > 200 ? 200 : ms;
}

bool VoicePlayout::push(const uint8_t* body, size_t n){
    if(n <= kVoiceHeader || n - kVoiceHeader > kMaxOpusPacket) return false;
    int samples = opus_packet_get_nb_samples(body + kVoiceHeader, (opus_int32)(n - kVoiceHeader), kVoiceRate);
    if(samples != kVoiceFrame && samples != 2 * kVoiceFrame) return false;
    uint32_t seq = get32(body);
    std::lock_guard<std::mutex> lk(mu_);
    st_.received++;
    if(started_ && (int32_t)(seq - next_) < 0){ st_.late++; return true; }
    if(started_ && (int32_t)(seq - next_) > 250){ q_.clear(); pcm_.clear(); started_ = false; }
    if(q_.size() >= 64) q_.erase(q_.begin());
    q_[seq].assign(body + kVoiceHeader, body + n);
    return true;
}

int VoicePlayout::queuedSamples() const {
    int total = 0;
    for(const auto& kv : q_){
        int s = opus_packet_get_nb_samples(kv.second.data(), (opus_int32)kv.second.size(), kVoiceRate);
        total += s > 0 ? s : kVoiceFrame;
    }
    return total + (int)pcm_.size();
}

void VoicePlayout::decodeNext(){
    OpusDecoder* d = (OpusDecoder*)dec_;
    float buf[2 * kVoiceFrame];
    int got = 0;
    auto it = q_.find(next_);
    if(it != q_.end()){
        int s = opus_packet_get_nb_samples(it->second.data(), (opus_int32)it->second.size(), kVoiceRate);
        got = opus_decode_float(d, it->second.data(), (opus_int32)it->second.size(), buf, s > 0 ? s : 2 * kVoiceFrame, 0);
        q_.erase(it);
        starving_ = 0;
    } else {
        auto nx = q_.find(next_ + 1);
        if(nx != q_.end()){
            int s = opus_packet_get_nb_samples(nx->second.data(), (opus_int32)nx->second.size(), kVoiceRate);
            int lost = s > 0 ? s : lastSamples_;
            got = opus_decode_float(d, nx->second.data(), (opus_int32)nx->second.size(), buf, lost, 1);
            st_.recoveredFec++;
        } else {
            got = opus_decode_float(d, nullptr, 0, buf, lastSamples_, 0);
            st_.concealed++;
            if(q_.empty() && ++starving_ > 25){ started_ = false; starving_ = 0; pcm_.clear(); return; }
        }
    }
    if(got <= 0){
        got = lastSamples_;
        std::memset(buf, 0, sizeof(float) * (size_t)got);
    }
    lastSamples_ = got;
    pcm_.insert(pcm_.end(), buf, buf + got);
    next_++;
    while(!q_.empty() && (int32_t)(q_.begin()->first - next_) < 0) q_.erase(q_.begin());
}

void VoicePlayout::pull(float* block20ms){
    std::lock_guard<std::mutex> lk(mu_);
    if(!started_){
        if(!dec_ || q_.empty() || queuedSamples() < targetMs_ * (kVoiceRate / 1000)){ std::memset(block20ms, 0, sizeof(float) * kVoiceFrame); return; }
        next_ = q_.begin()->first;
        started_ = true;
    }
    int limit = (2 * targetMs_ + 100) * (kVoiceRate / 1000);
    while(started_ && queuedSamples() > limit && !q_.empty() && pcm_.empty()){
        q_.erase(q_.begin());
        next_ = q_.empty() ? next_ : q_.begin()->first;
        st_.late++;
    }
    while(started_ && (int)pcm_.size() < kVoiceFrame) decodeNext();
    if(!started_ || (int)pcm_.size() < kVoiceFrame){ std::memset(block20ms, 0, sizeof(float) * kVoiceFrame); return; }
    for(int i = 0; i < kVoiceFrame; i++){ block20ms[i] = pcm_.front(); pcm_.pop_front(); }
    st_.played++;
}

VoiceStats VoicePlayout::stats(){
    std::lock_guard<std::mutex> lk(mu_);
    return st_;
}

}

#include "call.h"
#include "audio.h"
#include "console.h"
#include "voice.h"
#include <chrono>
#include <cstdio>
#include <mutex>

namespace sc {

namespace {

using Clock = std::chrono::steady_clock;

const uint8_t kCtlClose = 2;
const uint8_t kCtlPing = 3;

long long msSince(Clock::time_point t){
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

class Sender {
public:
    Sender(UdpSocket& s, Session& ses) : s_(s), ses_(ses) {}
    void send(Lane lane, const uint8_t* body, size_t n){
        std::lock_guard<std::mutex> lk(mu_);
        uint8_t pkt[kMaxPacket];
        size_t len = 0;
        if(sessionSeal(ses_, lane, body, n, pkt, sizeof pkt, len)) s_.sendTo(pkt, len, ses_.peer);
    }
    bool open(const uint8_t* pkt, size_t n, const Addr& from, Lane& lane, uint8_t* out, size_t cap, size_t& outLen){
        std::lock_guard<std::mutex> lk(mu_);
        return sessionOpen(ses_, pkt, n, from, lane, out, cap, outLen);
    }
    void control(uint8_t kind){
        uint8_t body[9] = {0};
        body[0] = kind;
        send(Lane::Signal, body, sizeof body);
    }
private:
    UdpSocket& s_;
    Session& ses_;
    std::mutex mu_;
};

}

int runCall(UdpSocket& s, Session& ses, const std::string& peerTag){
    VoiceEncoder enc;
    VoicePlayout play(3);
    if(!enc.ok() || !play.ok()){ consolePrint("* the voice codec could not start\n"); return 1; }
    Sender tx(s, ses);
    AudioIO audio;
    std::string err;
    bool ok = audio.start(
        [&](const float* frame){
            uint8_t body[kVoiceHeader + kMaxOpusPacket];
            size_t n = enc.packet(frame, body, sizeof body);
            if(n) tx.send(Lane::Voice, body, n);
        },
        [&](float* frame){ play.pull(frame); },
        err);
    if(!ok){
        consolePrint("* " + err + "\n");
        tx.control(kCtlClose);
        return 1;
    }
    consolePrint("\nCall open with " + peerTag + ". Your microphone is ON.\n"
                 "Type m and Enter to mute or unmute, q and Enter to hang up.\n"
                 "Use headphones: there is no echo cancellation yet.\n");
    LineInput input;
    input.start();
    auto lastRecv = Clock::now(), lastPing = Clock::now(), lastStats = Clock::now();
    uint8_t in[kMaxPacket], plain[kMaxPacket];
    int rc = 0;
    for(;;){
        std::string line;
        bool quit = false;
        while(input.next(line)){
            if(line == "m" || line == "M"){ audio.setMuted(!audio.muted()); consolePrint(audio.muted() ? "* muted\n" : "* unmuted\n"); }
            else if(line == "q" || line == "Q" || line == "/quit"){ quit = true; break; }
        }
        if(quit || input.closed()){ tx.control(kCtlClose); tx.control(kCtlClose); consolePrint("* you hung up\n"); break; }
        if(msSince(lastPing) >= 5000){ tx.control(kCtlPing); lastPing = Clock::now(); }
        if(msSince(lastRecv) >= 30000){ consolePrint("* call lost (nothing heard from your friend for 30 s)\n"); rc = 1; break; }
        if(msSince(lastStats) >= 5000){
            VoiceStats v = play.stats();
            char b[200];
            std::snprintf(b, sizeof b, "* call quality: %llu packets in, %llu filled in (lost packets and silences), %llu rebuilt by FEC, %llu late\n",
                (unsigned long long)v.received, (unsigned long long)v.concealed, (unsigned long long)v.recoveredFec, (unsigned long long)v.late);
            consolePrint(b);
            lastStats = Clock::now();
        }
        Addr from;
        int r = s.recvFrom(in, sizeof in, from, 50);
        if(r <= 0) continue;
        Lane lane;
        size_t n = 0;
        if(!tx.open(in, (size_t)r, from, lane, plain, sizeof plain, n)) continue;
        lastRecv = Clock::now();
        if(lane == Lane::Voice){ play.push(plain, n); continue; }
        if(lane == Lane::Signal && n >= 1 && plain[0] == kCtlClose){ consolePrint("* your friend hung up\n"); break; }
    }
    audio.stop();
    return rc;
}

}

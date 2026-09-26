#include "handshake.h"
#include "../third_party/monocypher/monocypher.h"
#include <chrono>
#include <cstring>
#include <vector>

namespace sc {

namespace {

using Clock = std::chrono::steady_clock;

int msLeft(Clock::time_point until){
    auto d = std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
    return d > 0 ? (int)d : 0;
}

uint64_t freshId(){
    uint64_t v = 0;
    while(v == 0){
        uint8_t b[8];
        if(!sysRandom(b, 8)) return 0;
        v = getU64(b);
    }
    return v;
}

bool staticOf(const Identity& me, StaticKey& k){
    return staticFromSeed(me.seed, k);
}

bool sendKeepalive(UdpSocket& s, Session& ses){
    uint8_t pkt[kDataHeaderLen + kTagLen];
    size_t n = 0;
    if(!sessionSeal(ses, Lane::Signal, nullptr, 0, pkt, sizeof pkt, n)) return false;
    return s.sendTo(pkt, n, ses.peer) == (int)n;
}

struct Bucket {
    double tokens = 10;
    Clock::time_point last = Clock::now();
    bool take(){
        auto now = Clock::now();
        tokens += std::chrono::duration<double>(now - last).count() * 10.0;
        if(tokens > 10) tokens = 10;
        last = now;
        if(tokens < 1) return false;
        tokens -= 1;
        return true;
    }
};

}

bool hsInitiate(UdpSocket& s, const Addr& to, const Identity& me, const uint8_t peerPub[32], const std::string& myCaps, Session& out, std::string& peerCaps, std::string& err, int timeoutMs){
    if(myCaps.size() + 40 > kMaxHsPayload){ err = "capability data is too large"; return false; }
    StaticKey mine;
    uint8_t theirs[32];
    if(!staticOf(me, mine)){ err = "identity key is unusable"; return false; }
    x25519FromEdPub(peerPub, theirs);
    uint64_t myId = freshId();
    if(!myId){ err = "the operating system random generator failed"; return false; }
    std::vector<uint8_t> payload(40 + myCaps.size());
    putU64(payload.data(), myId);
    std::memcpy(payload.data() + 8, me.pub, 32);
    std::memcpy(payload.data() + 40, myCaps.data(), myCaps.size());
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    int interval = 500;
    uint8_t pkt[kMaxPacket], in[kMaxPacket], body[kMaxHsPayload];
    bool done = false;
    while(!done && msLeft(deadline) > 0){
        NoiseHandshake hs;
        hs.startInitiator(mine, theirs, kPrologue, kPrologueLen);
        writeHeader(pkt, PacketType::Hello, Lane::Signal);
        putU64(pkt + kHeaderLen, myId);
        size_t n = 0;
        if(!hs.writeFirst(pkt + kHeaderLen + 8, sizeof pkt - kHeaderLen - 8, n, payload.data(), payload.size())){ err = "could not build the handshake"; break; }
        if(s.sendTo(pkt, kHeaderLen + 8 + n, to) != (int)(kHeaderLen + 8 + n)){ err = "could not send to " + to.str(); break; }
        auto retry = Clock::now() + std::chrono::milliseconds(interval);
        if(retry > deadline) retry = deadline;
        while(msLeft(retry) > 0){
            Addr from;
            int r = s.recvFrom(in, sizeof in, from, msLeft(retry));
            if(r <= 0) continue;
            Header h;
            if(!readHeader(in, (size_t)r, h) || h.type != PacketType::Welcome) continue;
            if((size_t)r < kHeaderLen + 8 + kSecondOverhead + 8 || getU64(in + kHeaderLen) != myId) continue;
            NoiseHandshake trial = hs;
            size_t bn = 0;
            if(!trial.readSecond(in + kHeaderLen + 8, (size_t)r - kHeaderLen - 8, body, sizeof body, bn) || bn < 8) continue;
            out.id = myId;
            out.peerId = getU64(body);
            std::memcpy(out.peerPub, peerPub, 32);
            out.peer = from;
            if(!trial.split(out.channel)){ err = "handshake split failed"; break; }
            peerCaps.assign((const char*)body + 8, bn - 8);
            crypto_wipe(body, sizeof body);
            done = true;
            break;
        }
        if(interval < 4000) interval *= 2;
    }
    crypto_wipe(&mine, sizeof mine);
    if(!done){
        if(err.empty()) err = "no answer from " + to.str() + " (wrong friend code, peer offline, or UDP blocked)";
        return false;
    }
    for(int i = 0; i < 3; i++) sendKeepalive(s, out);
    return true;
}

bool hsRespond(UdpSocket& s, const Identity& me, const std::string& myCaps, Session& out, std::string& peerCaps, std::string& err, int timeoutMs){
    if(myCaps.size() + 8 > kMaxHsPayload){ err = "capability data is too large"; return false; }
    StaticKey mine;
    if(!staticOf(me, mine)){ err = "identity key is unusable"; return false; }
    struct Seen { uint8_t key[32]; uint64_t stamp; };
    std::vector<Seen> seen;
    Session pending;
    std::string pendingCaps;
    bool havePending = false, done = false;
    Bucket bucket;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    uint8_t in[kMaxPacket], pkt[kMaxPacket], body[kMaxHsPayload], plain[kMaxPacket];
    while(!done && msLeft(deadline) > 0){
        Addr from;
        int r = s.recvFrom(in, sizeof in, from, msLeft(deadline) < 250 ? msLeft(deadline) : 250);
        if(r <= 0) continue;
        Header h;
        if(!readHeader(in, (size_t)r, h)) continue;
        if(h.type == PacketType::Hello){
            if((size_t)r < kHeaderLen + 8 + kFirstOverhead + 40 || !bucket.take()) continue;
            NoiseHandshake hs;
            hs.startResponder(mine, kPrologue, kPrologueLen);
            uint64_t stamp = 0;
            size_t bn = 0;
            if(!hs.readFirst(in + kHeaderLen + 8, (size_t)r - kHeaderLen - 8, stamp, body, sizeof body, bn) || bn < 40) continue;
            uint64_t theirId = getU64(body);
            uint8_t conv[32];
            x25519FromEdPub(body + 8, conv);
            if(theirId == 0 || theirId != getU64(in + kHeaderLen) || crypto_verify32(conv, hs.remoteStatic()) != 0) continue;
            Seen* prior = nullptr;
            for(Seen& e : seen) if(crypto_verify32(e.key, hs.remoteStatic()) == 0) prior = &e;
            if(prior && stamp <= prior->stamp) continue;
            uint64_t myId = freshId();
            if(!myId) continue;
            uint8_t reply[kMaxHsPayload];
            putU64(reply, myId);
            std::memcpy(reply + 8, myCaps.data(), myCaps.size());
            writeHeader(pkt, PacketType::Welcome, Lane::Signal);
            putU64(pkt + kHeaderLen, theirId);
            size_t n = 0;
            if(!hs.writeSecond(pkt + kHeaderLen + 8, sizeof pkt - kHeaderLen - 8, n, reply, 8 + myCaps.size())) continue;
            Session fresh;
            if(!hs.split(fresh.channel)) continue;
            if(prior) prior->stamp = stamp;
            else { Seen e; std::memcpy(e.key, hs.remoteStatic(), 32); e.stamp = stamp; seen.push_back(e); }
            s.sendTo(pkt, kHeaderLen + 8 + n, from);
            sessionWipe(pending);
            pending = fresh;
            sessionWipe(fresh);
            pending.id = myId;
            pending.peerId = theirId;
            std::memcpy(pending.peerPub, body + 8, 32);
            pending.peer = from;
            pendingCaps.assign((const char*)body + 40, bn - 40);
            havePending = true;
            crypto_wipe(body, sizeof body);
        } else if(h.type == PacketType::Data && havePending){
            Lane lane;
            size_t pn = 0;
            if(sessionOpen(pending, in, (size_t)r, from, lane, plain, sizeof plain, pn)) done = true;
        }
    }
    crypto_wipe(&mine, sizeof mine);
    if(!done){
        sessionWipe(pending);
        err = havePending ? "the peer never confirmed the session" : "no valid handshake arrived before the timeout";
        return false;
    }
    out = pending;
    sessionWipe(pending);
    peerCaps = pendingCaps;
    return true;
}

bool sessionSeal(Session& s, Lane lane, const uint8_t* plain, size_t n, uint8_t* out, size_t cap, size_t& outLen){
    if(cap < kDataHeaderLen + n + kTagLen) return false;
    writeHeader(out, PacketType::Data, lane);
    putU64(out + kHeaderLen, s.peerId);
    uint64_t nonce = 0;
    size_t cn = 0;
    if(!s.channel.peekNonce(nonce)) return false;
    putU64(out + kHeaderLen + 8, nonce);
    if(!s.channel.seal(out + kDataHeaderLen, cap - kDataHeaderLen, cn, nonce, out, kDataHeaderLen, plain, n)) return false;
    outLen = kDataHeaderLen + cn;
    return true;
}

bool sessionOpen(Session& s, const uint8_t* pkt, size_t n, const Addr& from, Lane& lane, uint8_t* out, size_t cap, size_t& outLen){
    Header h;
    if(!readHeader(pkt, n, h) || h.type != PacketType::Data || n < kDataHeaderLen + kTagLen) return false;
    if(getU64(pkt + kHeaderLen) != s.id || n - kDataHeaderLen - kTagLen > cap) return false;
    bool newest = false;
    if(!s.channel.open(getU64(pkt + kHeaderLen + 8), pkt, kDataHeaderLen, pkt + kDataHeaderLen, n - kDataHeaderLen, out, outLen, newest)) return false;
    if(newest && !(from == s.peer)) s.peer = from;
    lane = h.lane;
    return true;
}

void sessionWipe(Session& s){
    crypto_wipe(&s.channel, sizeof s.channel);
    s.id = 0;
    s.peerId = 0;
}

}

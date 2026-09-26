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

bool HsDialer::begin(const Identity& me, const uint8_t peerPub[32], const std::string& myCaps, uint8_t* pkt, size_t cap, size_t& n){
    active_ = false;
    if(myCaps.size() + 40 > kMaxHsPayload || cap < kHeaderLen + 8 + kFirstOverhead + 40 + myCaps.size()) return false;
    StaticKey mine;
    uint8_t theirs[32];
    if(!staticOf(me, mine)) return false;
    x25519FromEdPub(peerPub, theirs);
    id_ = freshId();
    if(!id_){ crypto_wipe(&mine, sizeof mine); return false; }
    std::memcpy(peerPub_, peerPub, 32);
    std::vector<uint8_t> payload(40 + myCaps.size());
    putU64(payload.data(), id_);
    std::memcpy(payload.data() + 8, me.pub, 32);
    std::memcpy(payload.data() + 40, myCaps.data(), myCaps.size());
    hs_ = NoiseHandshake();
    hs_.startInitiator(mine, theirs, kPrologue, kPrologueLen);
    crypto_wipe(&mine, sizeof mine);
    writeHeader(pkt, PacketType::Hello, Lane::Signal);
    putU64(pkt + kHeaderLen, id_);
    size_t m = 0;
    if(!hs_.writeFirst(pkt + kHeaderLen + 8, cap - kHeaderLen - 8, m, payload.data(), payload.size())) return false;
    n = kHeaderLen + 8 + m;
    active_ = true;
    return true;
}

bool HsDialer::finish(const uint8_t* pkt, size_t n, Session& out, std::string& peerCaps){
    if(!active_) return false;
    Header h;
    if(!readHeader(pkt, n, h) || h.type != PacketType::Welcome) return false;
    if(n < kHeaderLen + 8 + kSecondOverhead + 8 || getU64(pkt + kHeaderLen) != id_) return false;
    NoiseHandshake trial = hs_;
    uint8_t body[kMaxHsPayload];
    size_t bn = 0;
    if(!trial.readSecond(pkt + kHeaderLen + 8, n - kHeaderLen - 8, body, sizeof body, bn) || bn < 8) return false;
    Session s;
    if(!trial.split(s.channel)){ crypto_wipe(body, sizeof body); return false; }
    s.id = id_;
    s.peerId = getU64(body);
    std::memcpy(s.peerPub, peerPub_, 32);
    peerCaps.assign((const char*)body + 8, bn - 8);
    crypto_wipe(body, sizeof body);
    out = s;
    sessionWipe(s);
    active_ = false;
    return true;
}

HsDialer::~HsDialer(){
    crypto_wipe(peerPub_, sizeof peerPub_);
}

namespace {

bool readHello(const uint8_t* pkt, size_t n, const Identity& me, NoiseHandshake& hs, uint8_t body[kMaxHsPayload], size_t& bn, uint64_t& stamp){
    Header h;
    if(!readHeader(pkt, n, h) || h.type != PacketType::Hello || n < kHeaderLen + 8 + kFirstOverhead + 40) return false;
    StaticKey mine;
    if(!staticOf(me, mine)) return false;
    hs.startResponder(mine, kPrologue, kPrologueLen);
    crypto_wipe(&mine, sizeof mine);
    if(!hs.readFirst(pkt + kHeaderLen + 8, n - kHeaderLen - 8, stamp, body, kMaxHsPayload, bn) || bn < 40) return false;
    uint64_t theirId = getU64(body);
    uint8_t conv[32];
    x25519FromEdPub(body + 8, conv);
    if(theirId == 0 || theirId != getU64(pkt + kHeaderLen) || crypto_verify32(conv, hs.remoteStatic()) != 0){ crypto_wipe(body, kMaxHsPayload); return false; }
    return true;
}

}

bool hsPeek(const uint8_t* pkt, size_t n, const Identity& me, uint8_t peerEdPub[32], std::string& peerCaps, uint64_t& stamp){
    NoiseHandshake hs;
    uint8_t body[kMaxHsPayload];
    size_t bn = 0;
    if(!readHello(pkt, n, me, hs, body, bn, stamp)) return false;
    std::memcpy(peerEdPub, body + 8, 32);
    peerCaps.assign((const char*)body + 40, bn - 40);
    crypto_wipe(body, sizeof body);
    return true;
}

bool hsAccept(const uint8_t* pkt, size_t n, const Identity& me, const std::string& myCaps, Session& out, std::string& peerCaps, uint64_t& stamp, uint8_t* reply, size_t cap, size_t& replyLen){
    if(myCaps.size() + 8 > kMaxHsPayload || cap < kHeaderLen + 8 + kSecondOverhead + 8 + myCaps.size()) return false;
    NoiseHandshake hs;
    uint8_t body[kMaxHsPayload];
    size_t bn = 0;
    if(!readHello(pkt, n, me, hs, body, bn, stamp)) return false;
    uint64_t theirId = getU64(body);
    uint64_t myId = freshId();
    if(!myId){ crypto_wipe(body, sizeof body); return false; }
    uint8_t second[kMaxHsPayload];
    putU64(second, myId);
    std::memcpy(second + 8, myCaps.data(), myCaps.size());
    writeHeader(reply, PacketType::Welcome, Lane::Signal);
    putU64(reply + kHeaderLen, theirId);
    size_t m = 0;
    Session s;
    if(!hs.writeSecond(reply + kHeaderLen + 8, cap - kHeaderLen - 8, m, second, 8 + myCaps.size()) || !hs.split(s.channel)){ crypto_wipe(body, sizeof body); return false; }
    s.id = myId;
    s.peerId = theirId;
    std::memcpy(s.peerPub, body + 8, 32);
    peerCaps.assign((const char*)body + 40, bn - 40);
    crypto_wipe(body, sizeof body);
    replyLen = kHeaderLen + 8 + m;
    out = s;
    sessionWipe(s);
    return true;
}

bool hsInitiate(UdpSocket& s, const Addr& to, const Identity& me, const uint8_t peerPub[32], const std::string& myCaps, Session& out, std::string& peerCaps, std::string& err, int timeoutMs){
    if(myCaps.size() + 40 > kMaxHsPayload){ err = "capability data is too large"; return false; }
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    int interval = 500;
    uint8_t pkt[kMaxPacket], in[kMaxPacket];
    bool done = false;
    while(!done && msLeft(deadline) > 0){
        HsDialer dial;
        size_t n = 0;
        if(!dial.begin(me, peerPub, myCaps, pkt, sizeof pkt, n)){ err = "could not build the handshake"; break; }
        if(s.sendTo(pkt, n, to) != (int)n){ err = "could not send the handshake"; break; }
        auto retry = Clock::now() + std::chrono::milliseconds(interval);
        if(retry > deadline) retry = deadline;
        while(msLeft(retry) > 0){
            Addr from;
            int r = s.recvFrom(in, sizeof in, from, msLeft(retry));
            if(r <= 0) continue;
            if(!dial.finish(in, (size_t)r, out, peerCaps)) continue;
            out.peer = from;
            done = true;
            break;
        }
        if(interval < 4000) interval *= 2;
    }
    if(!done){
        if(err.empty()) err = "no answer (wrong friend code, peer offline, or UDP blocked)";
        return false;
    }
    for(int i = 0; i < 3; i++) sendKeepalive(s, out);
    return true;
}

bool hsRespond(UdpSocket& s, const Identity& me, const std::string& myCaps, Session& out, std::string& peerCaps, std::string& err, int timeoutMs, const uint8_t* onlyPeer){
    if(myCaps.size() + 8 > kMaxHsPayload){ err = "capability data is too large"; return false; }
    struct Seen { uint8_t key[32]; uint64_t stamp; };
    std::vector<Seen> seen;
    Session pending;
    std::string pendingCaps;
    bool havePending = false, done = false;
    Bucket bucket;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    uint8_t in[kMaxPacket], pkt[kMaxPacket], plain[kMaxPacket];
    while(!done && msLeft(deadline) > 0){
        Addr from;
        int r = s.recvFrom(in, sizeof in, from, msLeft(deadline) < 250 ? msLeft(deadline) : 250);
        if(r <= 0) continue;
        Header h;
        if(!readHeader(in, (size_t)r, h)) continue;
        if(h.type == PacketType::Hello){
            if(!bucket.take()) continue;
            Session fresh;
            std::string caps;
            uint64_t stamp = 0;
            size_t n = 0;
            if(!hsAccept(in, (size_t)r, me, myCaps, fresh, caps, stamp, pkt, sizeof pkt, n)) continue;
            if(onlyPeer && crypto_verify32(onlyPeer, fresh.peerPub) != 0){ sessionWipe(fresh); continue; }
            uint8_t conv[32];
            x25519FromEdPub(fresh.peerPub, conv);
            Seen* prior = nullptr;
            for(Seen& e : seen) if(crypto_verify32(e.key, conv) == 0) prior = &e;
            if(prior && stamp <= prior->stamp){ sessionWipe(fresh); continue; }
            if(prior) prior->stamp = stamp;
            else { Seen e; std::memcpy(e.key, conv, 32); e.stamp = stamp; seen.push_back(e); }
            s.sendTo(pkt, n, from);
            sessionWipe(pending);
            pending = fresh;
            sessionWipe(fresh);
            pending.peer = from;
            pendingCaps = caps;
            havePending = true;
        } else if(h.type == PacketType::Data && havePending){
            Lane lane;
            size_t pn = 0;
            if(sessionOpen(pending, in, (size_t)r, from, lane, plain, sizeof plain, pn)) done = true;
        }
    }
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

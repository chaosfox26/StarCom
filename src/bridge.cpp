#include "bridge.h"
#include "seed.h"
#include "wire.h"
#include "../third_party/monocypher/monocypher.h"
#include "../third_party/monocypher/monocypher-ed25519.h"
#include <chrono>
#include <cstring>

namespace sc {

namespace {

const uint8_t kData = 4, kClose = 6, kPing = 10, kHello = 13, kBridgeOpen = 14, kBridgeIn = 15, kBridgeAck = 16, kWelcome = 17;
const uint8_t kKeyReq = 1, kKeyRep = 2;
const uint8_t kBridgeVersion = 1;
const size_t kKeyReqLen = 64;
const size_t kEnvelope = 96;
const size_t kMaxPairs = 64;
const size_t kMaxRelays = 8;
const uint32_t kRefusedRetryMs = 600000;
const uint32_t kRetryMs = 5000;

uint32_t nowMs(){
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

struct BridgeHost::Link {
    Addr relay;
    UdpSocket sock;
    uint8_t relayPub[32] = {0};
    std::atomic<bool> havePub{false};
    std::atomic<bool> dup{false};
    std::mutex sessMu;
    Session ses;
    std::atomic<bool> linked{false};
    std::atomic<bool> welcomed{false};
    std::atomic<bool> refused{false};
    std::atomic<uint32_t> lastRecv{0};
    std::thread t;
};

BridgeHost::BridgeHost() {}

BridgeHost::~BridgeHost(){ stop(); }

void BridgeHost::start(int capKbps){
    stop();
    capKbps_ = capKbps < 64 ? 64 : capKbps;
    if(!sysRandom(id_.seed, 32)) return;
    uint8_t seedCopy[32];
    std::memcpy(seedCopy, id_.seed, 32);
    crypto_ed25519_key_pair(id_.secret, id_.pub, seedCopy);
    crypto_wipe(seedCopy, sizeof seedCopy);
    {
        std::lock_guard<std::mutex> g(mu_);
        tokens_ = capKbps_ * 125.0;
        lastRefill_ = windowStart_ = nowMs();
        bytes_ = dropped_ = windowBytes_ = 0;
        kbps_ = 0;
    }
    quit_ = false;
    running_ = true;
    ctl_ = std::thread([this]{ ctlLoop(); });
}

void BridgeHost::stop(){
    quit_ = true;
    if(ctl_.joinable()) ctl_.join();
    running_ = false;
    identityWipe(id_);
}

BridgeStats BridgeHost::stats(){
    BridgeStats s;
    std::lock_guard<std::mutex> g(mu_);
    for(auto& l : links_) if(l->welcomed) s.relays++;
    for(auto& kv : live_) if(kv.second) s.pairs++;
    s.pairs /= 2;
    s.kbps = nowMs() - windowStart_ > 2000 ? 0 : kbps_;
    s.bytes = bytes_;
    s.dropped = dropped_;
    return s;
}

void BridgeHost::ctlLoop(){
    std::vector<Addr> seeds;
    std::string err;
    if(!seedNodes(seeds, err) || seeds.empty()){ running_ = false; return; }
    std::vector<Addr> use;
    for(const Addr& a : seeds) if(a.family() == familyV4()) use.push_back(a);
    if(use.empty()) use = seeds;
    if(use.size() > kMaxRelays) use.resize(kMaxRelays);
    {
        std::lock_guard<std::mutex> g(mu_);
        for(const Addr& a : use){
            std::unique_ptr<Link> l(new Link());
            l->relay = a;
            links_.push_back(std::move(l));
        }
    }
    for(size_t i = 0; i < links_.size(); i++) links_[i]->t = std::thread([this, i]{ linkLoop((int)i); });
    while(!quit_) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::lock_guard<std::mutex> g(mu_);
        for(auto& kv : partner_){
            uint8_t f[9];
            f[0] = kClose;
            putU64(f + 1, kv.first.second);
            sendFrame(*links_[(size_t)kv.first.first], f, sizeof f, Lane::Signal);
        }
        partner_.clear();
        live_.clear();
    }
    for(auto& l : links_) if(l->t.joinable()) l->t.join();
    std::lock_guard<std::mutex> g(mu_);
    for(auto& l : links_){
        {
            std::lock_guard<std::mutex> s(l->sessMu);
            sessionWipe(l->ses);
            l->linked = false;
        }
        l->sock.close();
    }
    links_.clear();
}

bool BridgeHost::sendFrame(Link& l, const uint8_t* p, size_t n, Lane lane){
    uint8_t pkt[kMaxPacket];
    size_t len = 0;
    bool ok;
    {
        std::lock_guard<std::mutex> g(l.sessMu);
        if(!l.linked) return false;
        ok = sessionSeal(l.ses, lane, p, n, pkt, sizeof pkt, len);
    }
    return ok && l.sock.sendTo(pkt, len, l.relay) == (int)len;
}

void BridgeHost::sendHelloAll(){
    std::vector<uint8_t> f;
    f.push_back(kHello);
    f.push_back(kBridgeVersion);
    uint32_t cap = (uint32_t)capKbps_;
    for(int i = 0; i < 4; i++) f.push_back((uint8_t)(cap >> (8 * i)));
    std::vector<const uint8_t*> pubs;
    for(auto& l : links_) if(l->linked && l->havePub && !l->refused) pubs.push_back(l->relayPub);
    f.push_back((uint8_t)pubs.size());
    for(const uint8_t* p : pubs) f.insert(f.end(), p, p + 32);
    for(auto& l : links_) if(l->linked) sendFrame(*l, f.data(), f.size(), Lane::Signal);
}

bool BridgeHost::linkUp(Link& l){
    if(!l.havePub){
        uint8_t req[kKeyReqLen] = {0};
        writeHeader(req, PacketType::Link, Lane::Signal);
        req[kHeaderLen] = kKeyReq;
        for(int t = 0; t < 3 && !l.havePub && !quit_; t++){
            l.sock.sendTo(req, sizeof req, l.relay);
            uint32_t until = nowMs() + 1000;
            while(!l.havePub && !quit_ && (int32_t)(nowMs() - until) < 0){
                uint8_t in[kMaxPacket];
                Addr from;
                int r = l.sock.recvFrom(in, sizeof in, from, 200);
                Header h;
                if(r < (int)(kHeaderLen + 1 + 32) || !(from == l.relay) || !readHeader(in, (size_t)r, h) || h.type != PacketType::Link || in[kHeaderLen] != kKeyRep) continue;
                std::memcpy(l.relayPub, in + kHeaderLen + 1, 32);
                l.havePub = true;
            }
        }
        if(!l.havePub) return false;
        for(auto& o : links_){
            if(o.get() == &l || !o->havePub) continue;
            if(std::memcmp(o->relayPub, l.relayPub, 32) == 0){ l.dup = true; return false; }
        }
    }
    Session s;
    std::string caps, e;
    if(!hsInitiate(l.sock, l.relay, id_, l.relayPub, "{}", s, caps, e, 8000)){ sessionWipe(s); return false; }
    {
        std::lock_guard<std::mutex> g(l.sessMu);
        l.ses = s;
        l.linked = true;
    }
    sessionWipe(s);
    l.welcomed = false;
    l.refused = false;
    l.lastRecv = nowMs();
    sendHelloAll();
    uint32_t until = nowMs() + 5000;
    uint8_t buf[kMaxPacket], plain[kMaxPacket];
    while(!quit_ && (int32_t)(nowMs() - until) < 0){
        Addr from;
        int r = l.sock.recvFrom(buf, sizeof buf, from, 200);
        Header h;
        if(r <= 0 || !(from == l.relay) || !readHeader(buf, (size_t)r, h) || h.type != PacketType::Data) continue;
        Lane lane;
        size_t n = 0;
        bool ok;
        {
            std::lock_guard<std::mutex> g(l.sessMu);
            ok = sessionOpen(l.ses, buf, (size_t)r, from, lane, plain, sizeof plain, n);
        }
        if(!ok || n < 2 || plain[0] != kWelcome) continue;
        l.lastRecv = nowMs();
        if(plain[1]){
            l.welcomed = true;
            return true;
        }
        l.refused = true;
        break;
    }
    std::lock_guard<std::mutex> g(l.sessMu);
    sessionWipe(l.ses);
    l.linked = false;
    return false;
}

void BridgeHost::dropLink(int idx){
    Link& l = *links_[(size_t)idx];
    {
        std::lock_guard<std::mutex> g(l.sessMu);
        sessionWipe(l.ses);
        l.linked = false;
    }
    l.welcomed = false;
    std::lock_guard<std::mutex> g(mu_);
    for(auto it = partner_.begin(); it != partner_.end();){
        if(it->first.first != idx){ ++it; continue; }
        End other = it->second;
        uint8_t f[9];
        f[0] = kClose;
        putU64(f + 1, other.second);
        sendFrame(*links_[(size_t)other.first], f, sizeof f, Lane::Signal);
        live_.erase(it->first);
        live_.erase(other);
        partner_.erase(other);
        it = partner_.erase(it);
    }
    sendHelloAll();
}

bool BridgeHost::takeTokens(size_t n){
    uint32_t now = nowMs();
    double rate = capKbps_ * 125.0;
    tokens_ += (double)(now - lastRefill_) * rate / 1000.0;
    if(tokens_ > rate) tokens_ = rate;
    lastRefill_ = now;
    if(now - windowStart_ >= 1000){
        kbps_ = (int)(windowBytes_ * 8 / (now - windowStart_));
        windowBytes_ = 0;
        windowStart_ = now;
    }
    if(tokens_ < (double)n) return false;
    tokens_ -= (double)n;
    return true;
}

void BridgeHost::dispatch(int idx, const uint8_t* p, size_t n){
    if(n < 1) return;
    uint8_t kind = p[0];
    if(kind == kWelcome){
        if(n >= 2 && p[1]) links_[(size_t)idx]->welcomed = true;
        return;
    }
    if(n < 9) return;
    End me{ idx, getU64(p + 1) };
    if(kind == kBridgeOpen){
        if(n < 1 + 8 + 32 + kEnvelope) return;
        const uint8_t* target = p + 9;
        const uint8_t* env = p + 41;
        int ti = -1;
        for(size_t i = 0; i < links_.size(); i++) if(links_[i]->welcomed && std::memcmp(links_[i]->relayPub, target, 32) == 0) ti = (int)i;
        std::lock_guard<std::mutex> g(mu_);
        int currentKbps = (int32_t)(nowMs() - windowStart_) > 2000 ? 0 : kbps_;
        bool full = partner_.size() / 2 >= kMaxPairs || currentKbps * 10 > capKbps_ * 9;
        uint64_t co = 0;
        if(ti >= 0 && !full && !partner_.count(me)){
            while(co == 0 || partner_.count(End{ ti, co })) if(!sysRandom((uint8_t*)&co, sizeof co)) break;
        }
        uint8_t f[1 + 8 + kEnvelope];
        f[0] = kBridgeIn;
        putU64(f + 1, co);
        std::memcpy(f + 9, env, kEnvelope);
        if(co == 0 || !sendFrame(*links_[(size_t)ti], f, sizeof f, Lane::Signal)){
            uint8_t a[10];
            a[0] = kBridgeAck;
            putU64(a + 1, me.second);
            a[9] = 0;
            sendFrame(*links_[(size_t)idx], a, sizeof a, Lane::Signal);
            return;
        }
        End other{ ti, co };
        partner_[me] = other;
        partner_[other] = me;
        live_[me] = false;
        live_[other] = false;
        return;
    }
    std::lock_guard<std::mutex> g(mu_);
    auto it = partner_.find(me);
    if(it == partner_.end()) return;
    End other = it->second;
    if(kind == kBridgeAck){
        if(n < 10) return;
        uint8_t a[10];
        a[0] = kBridgeAck;
        putU64(a + 1, other.second);
        a[9] = p[9] ? 1 : 0;
        sendFrame(*links_[(size_t)other.first], a, sizeof a, Lane::Signal);
        if(p[9]){
            live_[me] = true;
            live_[other] = true;
        } else {
            partner_.erase(me);
            partner_.erase(other);
            live_.erase(me);
            live_.erase(other);
        }
        return;
    }
    if(kind == kData){
        size_t pn = n - 9;
        if(pn > kMaxCircuitInner) return;
        if(!takeTokens(pn)){ dropped_++; return; }
        uint8_t f[kMaxPacket];
        f[0] = kData;
        putU64(f + 1, other.second);
        std::memcpy(f + 9, p + 9, pn);
        Lane lane = Lane::Signal;
        Header h;
        if(readHeader(p + 9, pn, h)) lane = h.lane;
        if(sendFrame(*links_[(size_t)other.first], f, 9 + pn, lane)){
            bytes_ += pn;
            windowBytes_ += pn;
        }
        return;
    }
    if(kind == kClose){
        uint8_t f[9];
        f[0] = kClose;
        putU64(f + 1, other.second);
        sendFrame(*links_[(size_t)other.first], f, sizeof f, Lane::Signal);
        partner_.erase(me);
        partner_.erase(other);
        live_.erase(me);
        live_.erase(other);
    }
}

void BridgeHost::linkLoop(int idx){
    Link& l = *links_[(size_t)idx];
    std::string err;
    if(!l.sock.open(l.relay.family(), 0, err)) return;
    uint32_t lastPing = 0, retryAt = 0;
    uint8_t buf[kMaxPacket], plain[kMaxPacket];
    while(!quit_){
        if(!l.linked){
            if(l.dup) return;
            if(retryAt && (int32_t)(nowMs() - retryAt) < 0){ std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }
            if(!linkUp(l)){
                retryAt = nowMs() + (l.refused ? kRefusedRetryMs : kRetryMs);
                continue;
            }
            retryAt = 0;
            lastPing = nowMs();
            {
                std::lock_guard<std::mutex> g(mu_);
                sendHelloAll();
            }
            continue;
        }
        Addr from;
        int r = l.sock.recvFrom(buf, sizeof buf, from, 200);
        uint32_t now = nowMs();
        if(now - lastPing >= 25000){
            uint8_t f[1] = { kPing };
            sendFrame(l, f, 1, Lane::Signal);
            lastPing = now;
        }
        if(now - l.lastRecv.load() >= 75000){ dropLink(idx); continue; }
        Header h;
        if(r <= 0 || !(from == l.relay) || !readHeader(buf, (size_t)r, h) || h.type != PacketType::Data) continue;
        Lane lane;
        size_t n = 0;
        bool ok;
        {
            std::lock_guard<std::mutex> g(l.sessMu);
            ok = sessionOpen(l.ses, buf, (size_t)r, from, lane, plain, sizeof plain, n);
        }
        if(!ok || n == 0) continue;
        l.lastRecv = now;
        dispatch(idx, plain, n);
    }
}

}

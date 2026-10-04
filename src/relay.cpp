#include "relay.h"
#include "noise.h"
#include "../third_party/monocypher/monocypher.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace sc {

namespace {

const uint32_t kLinkSilentMs = 75000;
const uint32_t kCircuitIdleMs = 120000;
const uint32_t kPeerPingMs = 25000;
const size_t kMaxJobsPerConn = 4;
const size_t kMaxBridgeOpens = 2048;
const size_t kMaxConns = 2000;
const size_t kMaxCircuitsPerConn = 64;
const size_t kMaxQueued = 64;
const double kCircuitBytesPerMs = 256.0;
const double kCircuitBurst = 512.0 * 1024.0;
const size_t kMaxStamps = 20000;
const size_t kMaxPeers = 256;
const int kMaxNewPeersPerMinute = 4;
const size_t kMaxRates = 8192;
const uint32_t kBridgeConfirmMs = 2000;
const uint32_t kBridgeAvoidMs = 600000;
const uint32_t kBridgeIdleMs = 600000;
const uint32_t kBridgeShareWindowMs = 600000;
const uint32_t kEnvSeenMs = 600000;
const uint32_t kEnvelopeSkewSec = 60;
const size_t kMaxBridges = 256;
const double kBridgeFloorKbps = 64.0;

uint32_t nowMs(){
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string keyStr(const uint8_t* k){ return std::string((const char*)k, 32); }

std::string remoteKey(const std::string& peerKey, uint64_t circ){
    uint8_t c[8];
    putU64(c, circ);
    return peerKey + std::string((const char*)c, 8);
}

std::string ipKey(const Addr& a){
    uint8_t e[19];
    if(!encodeAddr(a, e)) return std::string();
    return std::string((const char*)e, 1) + std::string((const char*)e + 3, 16);
}

Lane laneOf(const uint8_t* payload, size_t n){
    if(n >= kHeaderLen && payload[0] == 'S' && payload[1] == 'C' && payload[4] <= (uint8_t)Lane::Background) return (Lane)payload[4];
    return Lane::Background;
}

uint64_t random64(){
    uint64_t v = 0;
    while(v == 0) sysRandom((uint8_t*)&v, 8);
    return v;
}

}

RelayServer::RelayServer(UdpSocket& s, int socketFamily, const Identity& me, const std::vector<Addr>& dhtSeeds) : s_(s), family_(socketFamily), me_(me), caps_("{}"), dhtSeeds_(dhtSeeds){
    StaticKey sk;
    if(staticFromSeed(me_.seed, sk)) std::memcpy(xSec_, sk.sec, 32);
    crypto_wipe(&sk, sizeof sk);
    worker_ = std::thread([this]{ workerLoop(); });
}

RelayServer::~RelayServer(){
    {
        std::lock_guard<std::mutex> g(jobMu_);
        stop_ = true;
    }
    jobCv_.notify_all();
    if(worker_.joinable()) worker_.join();
    for(auto& kv : conns_) sessionWipe(kv.second.ses);
    for(auto& kv : pendingBridge_) crypto_wipe(kv.second.owner, 32);
    crypto_wipe(xSec_, sizeof xSec_);
    identityWipe(me_);
}

size_t RelayServer::bridges() const {
    size_t n = 0;
    for(auto& kv : conns_) if(kv.second.bridge) n++;
    return n;
}

bool RelayServer::sealEnvelope(const uint8_t yEdPub[32], const uint8_t owner[32], uint8_t out[kEnvelopeLen]){
    uint8_t eSec[32], ePub[32], yX[32], shared[32], k[32], plain[48], nonce[24] = {0};
    sysRandom(eSec, 32);
    crypto_x25519_public_key(ePub, eSec);
    x25519FromEdPub(yEdPub, yX);
    crypto_x25519(shared, eSec, yX);
    uint8_t acc = 0;
    for(uint8_t b : shared) acc |= b;
    static const char label[] = "starcom-bridge";
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 32);
    crypto_blake2b_update(&ctx, (const uint8_t*)label, sizeof label - 1);
    crypto_blake2b_update(&ctx, shared, 32);
    crypto_blake2b_update(&ctx, ePub, 32);
    crypto_blake2b_update(&ctx, yEdPub, 32);
    crypto_blake2b_final(&ctx, k);
    std::memcpy(plain, owner, 32);
    putU32(plain + 32, wallSeconds());
    sysRandom(plain + 36, 12);
    std::memcpy(out, ePub, 32);
    crypto_aead_lock(out + 32, out + 80, k, nonce, nullptr, 0, plain, 48);
    crypto_wipe(eSec, 32);
    crypto_wipe(shared, 32);
    crypto_wipe(k, 32);
    crypto_wipe(plain, 48);
    return acc != 0;
}

bool RelayServer::openEnvelope(const uint8_t env[kEnvelopeLen], uint8_t owner[32]){
    uint8_t shared[32], k[32], plain[48], nonce[24] = {0};
    crypto_x25519(shared, xSec_, env);
    uint8_t acc = 0;
    for(uint8_t b : shared) acc |= b;
    if(acc == 0){
        crypto_wipe(shared, 32);
        return false;
    }
    static const char label[] = "starcom-bridge";
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 32);
    crypto_blake2b_update(&ctx, (const uint8_t*)label, sizeof label - 1);
    crypto_blake2b_update(&ctx, shared, 32);
    crypto_blake2b_update(&ctx, env, 32);
    crypto_blake2b_update(&ctx, me_.pub, 32);
    crypto_blake2b_final(&ctx, k);
    bool ok = crypto_aead_unlock(plain, env + 80, k, nonce, nullptr, 0, env + 32, 48) == 0;
    if(ok){
        uint32_t ts = getU32(plain + 32), now = wallSeconds();
        ok = (ts > now ? ts - now : now - ts) <= kEnvelopeSkewSec;
        if(ok) std::memcpy(owner, plain, 32);
    }
    crypto_wipe(shared, 32);
    crypto_wipe(k, 32);
    crypto_wipe(plain, 48);
    return ok;
}

bool RelayServer::allow(const Addr& a){
    std::string k = ipKey(a);
    if(k.empty()) return false;
    if(rate_.size() > kMaxRates) rate_.clear();
    Rate& r = rate_[k];
    uint32_t now = nowMs();
    if(r.last) r.tokens = std::min(40.0, r.tokens + (now - r.last) * 0.02);
    r.last = now;
    if(r.tokens < 1) return false;
    r.tokens -= 1;
    return true;
}

bool RelayServer::sendFrame(uint64_t connId, Lane lane, const uint8_t* body, size_t n){
    auto it = conns_.find(connId);
    if(it == conns_.end()) return false;
    uint8_t pkt[kMaxPacket];
    size_t len = 0;
    if(!sessionSeal(it->second.ses, lane, body, n, pkt, sizeof pkt, len)) return false;
    s_.sendTo(pkt, len, it->second.ses.peer);
    it->second.lastSendMs = nowMs();
    return true;
}

void RelayServer::sendToPeer(Peer& p, const std::vector<uint8_t>& frame){
    if(p.conn && conns_.count(p.conn)){
        sendFrame(p.conn, frame[0] == kLinkData ? laneOf(frame.data() + 9, frame.size() - 9) : Lane::Signal, frame.data(), frame.size());
        return;
    }
    if(p.queued.size() < kMaxQueued) p.queued.push_back(frame);
    if(!p.dialing) dialPeer(p);
}

void RelayServer::sendFrameTo(const End& e, uint8_t kind, uint64_t circ, const uint8_t* payload, size_t n, Lane lane){
    std::vector<uint8_t> f(9 + n);
    f[0] = kind;
    putU64(f.data() + 1, circ);
    if(n) std::memcpy(f.data() + 9, payload, n);
    if(!e.peerKey.empty()){
        auto it = peers_.find(e.peerKey);
        if(it != peers_.end()) sendToPeer(it->second, f);
        return;
    }
    sendFrame(e.conn, lane, f.data(), f.size());
}

uint64_t RelayServer::newCircuit(uint64_t connId){
    for(;;){
        uint64_t c = random64();
        if(!circs_.count({connId, c})) return c;
    }
}

RelayServer::Peer& RelayServer::peerFor(const Addr& a, const uint8_t pub[32]){
    std::string k = keyStr(pub);
    Peer& p = peers_[k];
    if(p.addr.len == 0){
        p.addr = mapForSocket(a, family_);
        std::memcpy(p.pub, pub, 32);
    }
    return p;
}

void RelayServer::dialPeer(Peer& p){
    uint8_t pkt[kMaxPacket];
    size_t n = 0;
    if(!p.dial.begin(me_, p.pub, caps_, pkt, sizeof pkt, n)) return;
    s_.sendTo(pkt, n, p.addr);
    p.dialing = true;
    p.tries++;
    p.lastDialMs = nowMs();
}

void RelayServer::openLocal(uint64_t fromConn, uint64_t fromCirc, const std::string& fromPeer, const uint8_t owner[32]){
    End from;
    from.conn = fromConn;
    from.circ = fromCirc;
    from.peerKey = fromPeer;
    auto t = byOwner_.find(keyStr(owner));
    if(t == byOwner_.end() || !conns_.count(t->second) || conns_[t->second].circuitCount >= kMaxCircuitsPerConn){
        sendFrameTo(from, kLinkClose, fromCirc, nullptr, 0, Lane::Signal);
        return;
    }
    uint64_t target = t->second;
    uint64_t cb = newCircuit(target);
    uint32_t now = nowMs();
    Circ a;
    a.other.conn = target;
    a.other.circ = cb;
    a.opener = true;
    a.lastMs = now;
    a.tokens = kCircuitBurst;
    a.refillMs = now;
    Circ b;
    b.other = from;
    b.lastMs = now;
    b.tokens = kCircuitBurst;
    b.refillMs = now;
    circs_[{fromConn, fromCirc}] = a;
    circs_[{target, cb}] = b;
    if(conns_.count(fromConn)) conns_[fromConn].circuitCount++;
    conns_[target].circuitCount++;
    uint8_t f[9];
    f[0] = kLinkIncoming;
    putU64(f + 1, cb);
    sendFrame(target, Lane::Signal, f, sizeof f);
}

void RelayServer::dropCircuit(uint64_t connId, uint64_t circ, bool notifyOther){
    forgetPending(connId, circ);
    auto cit = conns_.find(connId);
    if(cit != conns_.end() && cit->second.dialed){
        auto r = remoteIn_.find(remoteKey(cit->second.peerKey, circ));
        if(r == remoteIn_.end()) return;
        std::pair<uint64_t, uint64_t> local = r->second;
        remoteIn_.erase(r);
        auto lc = circs_.find(local);
        if(lc != circs_.end()){
            circs_.erase(lc);
            if(conns_.count(local.first) && conns_[local.first].circuitCount) conns_[local.first].circuitCount--;
        }
        if(notifyOther){
            End e;
            e.conn = local.first;
            e.circ = local.second;
            sendFrameTo(e, kLinkClose, local.second, nullptr, 0, Lane::Signal);
        }
        return;
    }
    auto it = circs_.find({connId, circ});
    if(it == circs_.end()) return;
    End other = it->second.other;
    circs_.erase(it);
    if(cit != conns_.end() && cit->second.circuitCount) cit->second.circuitCount--;
    if(other.peerKey.empty()){
        auto o = circs_.find({other.conn, other.circ});
        if(o != circs_.end()){
            circs_.erase(o);
            if(conns_.count(other.conn) && conns_[other.conn].circuitCount) conns_[other.conn].circuitCount--;
        }
    } else {
        remoteIn_.erase(remoteKey(other.peerKey, other.circ));
    }
    if(notifyOther) sendFrameTo(other, kLinkClose, other.circ, nullptr, 0, Lane::Signal);
}

void RelayServer::dropConn(uint64_t connId){
    auto it = conns_.find(connId);
    if(it == conns_.end()) return;
    std::vector<uint64_t> mine;
    for(auto& kv : circs_) if(kv.first.first == connId) mine.push_back(kv.first.second);
    for(uint64_t c : mine) dropCircuit(connId, c, true);
    if(it->second.dialed){
        std::string pk = it->second.peerKey;
        std::vector<std::string> gone;
        for(auto& kv : remoteIn_) if(kv.first.compare(0, 32, pk) == 0) gone.push_back(kv.first);
        for(const std::string& g : gone){
            std::pair<uint64_t, uint64_t> local = remoteIn_[g];
            remoteIn_.erase(g);
            auto lc = circs_.find(local);
            if(lc != circs_.end()){
                circs_.erase(lc);
                if(conns_.count(local.first) && conns_[local.first].circuitCount) conns_[local.first].circuitCount--;
                End e;
                e.conn = local.first;
                e.circ = local.second;
                sendFrameTo(e, kLinkClose, local.second, nullptr, 0, Lane::Signal);
            }
        }
        auto p = peers_.find(pk);
        if(p != peers_.end() && p->second.conn == connId) p->second.conn = 0;
    }
    auto o = byOwner_.find(keyStr(it->second.ses.peerPub));
    if(o != byOwner_.end() && o->second == connId) byOwner_.erase(o);
    sessionWipe(it->second.ses);
    conns_.erase(connId);
}

void RelayServer::replaceLink(uint64_t oldId, uint64_t newId){
    std::vector<uint64_t> opened, answered;
    for(auto& kv : circs_){
        if(kv.first.first != oldId) continue;
        if(kv.second.opener) opened.push_back(kv.first.second);
        else answered.push_back(kv.first.second);
    }
    for(uint64_t c : opened) dropCircuit(oldId, c, true);
    for(uint64_t c : answered){
        auto it = circs_.find({oldId, c});
        if(it == circs_.end()) continue;
        Circ moved = it->second;
        circs_.erase(it);
        if(conns_.count(oldId) && conns_[oldId].circuitCount) conns_[oldId].circuitCount--;
        if(circs_.count({newId, c}) || !conns_.count(newId) || conns_[newId].circuitCount >= kMaxCircuitsPerConn){
            if(moved.other.peerKey.empty()){
                auto o = circs_.find({moved.other.conn, moved.other.circ});
                if(o != circs_.end()){
                    circs_.erase(o);
                    if(conns_.count(moved.other.conn) && conns_[moved.other.conn].circuitCount) conns_[moved.other.conn].circuitCount--;
                }
            } else {
                remoteIn_.erase(remoteKey(moved.other.peerKey, moved.other.circ));
            }
            sendFrameTo(moved.other, kLinkClose, moved.other.circ, nullptr, 0, Lane::Signal);
            continue;
        }
        circs_[{newId, c}] = moved;
        conns_[newId].circuitCount++;
        if(moved.other.peerKey.empty()){
            auto o = circs_.find({moved.other.conn, moved.other.circ});
            if(o != circs_.end()) o->second.other.conn = newId;
        }
        uint8_t f[9];
        f[0] = kLinkIncoming;
        putU64(f + 1, c);
        sendFrame(newId, Lane::Signal, f, sizeof f);
    }
    for(auto& kv : remoteIn_) if(kv.second.first == oldId) kv.second.first = newId;
}

void RelayServer::routeOpen(uint64_t connId, uint64_t circ, const Addr& relay, const uint8_t relayPub[32], const uint8_t owner[32]){
    auto cit = conns_.find(connId);
    if(cit == conns_.end()) return;
    Conn& c = cit->second;
    uint32_t now = nowMs();
    if(crypto_verify32(relayPub, me_.pub) == 0){
        openLocal(connId, circ, std::string(), owner);
        return;
    }
    bool known = peers_.count(keyStr(relayPub)) != 0;
    if(!known){
        if(now - c.dialWindowMs > 60000){ c.dialWindowMs = now; c.dialsInWindow = 0; }
        if(c.dialsInWindow >= kMaxNewPeersPerMinute || peers_.size() >= kMaxPeers){
            uint8_t f[9];
            f[0] = kLinkClose;
            putU64(f + 1, circ);
            sendFrame(connId, Lane::Signal, f, sizeof f);
            return;
        }
        c.dialsInWindow++;
    }
    Peer& pr = peerFor(relay, relayPub);
    uint64_t cr = random64();
    Circ a;
    a.other.circ = cr;
    a.other.peerKey = keyStr(relayPub);
    a.opener = true;
    a.lastMs = now;
    a.tokens = kCircuitBurst;
    a.refillMs = now;
    circs_[{connId, circ}] = a;
    remoteIn_[remoteKey(a.other.peerKey, cr)] = { connId, circ };
    c.circuitCount++;
    std::vector<uint8_t> f(1 + 8 + 32);
    f[0] = kLinkForward;
    putU64(f.data() + 1, cr);
    std::memcpy(f.data() + 9, owner, 32);
    sendToPeer(pr, f);
}

bool RelayServer::tryBridge(uint64_t connId, uint64_t circ, const Addr& relay, const uint8_t relayPub[32], const uint8_t owner[32]){
    uint32_t now = nowMs();
    bool self = crypto_verify32(relayPub, me_.pub) == 0;
    std::string target = keyStr(relayPub);
    std::map<uint64_t, size_t> share;
    size_t total = 0, up = 0;
    for(auto& e : bridgeOpens_){ share[e.second]++; total++; }
    for(auto& kv : conns_) if(kv.second.bridge) up++;
    std::vector<std::pair<uint64_t, double>> cand;
    double sum = 0;
    for(auto& kv : conns_){
        const Conn& b = kv.second;
        if(!b.bridge || b.circuitCount + (self ? 2 : 1) > kMaxCircuitsPerConn) continue;
        if(!self && std::find(b.bridgeRelays.begin(), b.bridgeRelays.end(), target) == b.bridgeRelays.end()) continue;
        auto av = avoid_.find(ipKey(b.ses.peer));
        if(av != avoid_.end() && (int32_t)(av->second - now) > 0) continue;
        if(up >= 4 && total >= 4 && share[kv.first] * 4 >= total) continue;
        double w = std::max(kBridgeFloorKbps, std::min(b.goodKbps, (double)b.capKbps));
        cand.push_back({ kv.first, w });
        sum += w;
    }
    if(cand.empty() || sum <= 0) return false;
    double pick = (double)(random64() % 1000000) / 1000000.0 * sum;
    uint64_t v = cand.back().first;
    for(auto& cw : cand){
        if(pick < cw.second){ v = cw.first; break; }
        pick -= cw.second;
    }
    uint8_t f[1 + 8 + 32 + kEnvelopeLen];
    if(!sealEnvelope(relayPub, owner, f + 41)) return false;
    uint64_t circIn = newCircuit(v);
    Circ a;
    a.other.conn = v;
    a.other.circ = circIn;
    a.opener = true;
    a.lastMs = now;
    a.tokens = kCircuitBurst;
    a.refillMs = now;
    Circ b;
    b.other.conn = connId;
    b.other.circ = circ;
    b.lastMs = now;
    b.tokens = kCircuitBurst;
    b.refillMs = now;
    circs_[{connId, circ}] = a;
    circs_[{v, circIn}] = b;
    conns_[connId].circuitCount++;
    conns_[v].circuitCount++;
    Pending pd;
    pd.vConn = v;
    pd.circIn = circIn;
    pd.deadline = now + kBridgeConfirmMs;
    pd.relay = relay;
    std::memcpy(pd.relayPub, relayPub, 32);
    std::memcpy(pd.owner, owner, 32);
    pendingBridge_[{connId, circ}] = pd;
    pendingByBridge_[{v, circIn}] = { connId, circ };
    if(bridgeOpens_.size() >= kMaxBridgeOpens) bridgeOpens_.pop_front();
    bridgeOpens_.push_back({ now, v });
    f[0] = kLinkBridgeOpen;
    putU64(f + 1, circIn);
    std::memcpy(f + 9, relayPub, 32);
    sendFrame(v, Lane::Signal, f, sizeof f);
    return true;
}

void RelayServer::forgetPending(uint64_t connId, uint64_t circ){
    auto a = pendingBridge_.find({connId, circ});
    if(a != pendingBridge_.end()){
        pendingByBridge_.erase({ a->second.vConn, a->second.circIn });
        crypto_wipe(a->second.owner, 32);
        pendingBridge_.erase(a);
        return;
    }
    auto b = pendingByBridge_.find({connId, circ});
    if(b != pendingByBridge_.end()){
        auto pa = pendingBridge_.find(b->second);
        if(pa != pendingBridge_.end()){
            crypto_wipe(pa->second.owner, 32);
            pendingBridge_.erase(pa);
        }
        pendingByBridge_.erase(b);
    }
}

void RelayServer::bridgeFallback(std::pair<uint64_t, uint64_t> key){
    auto it = pendingBridge_.find(key);
    if(it == pendingBridge_.end()) return;
    Pending pd = it->second;
    crypto_wipe(it->second.owner, 32);
    pendingBridge_.erase(it);
    pendingByBridge_.erase({ pd.vConn, pd.circIn });
    if(circs_.erase(key) && conns_.count(key.first) && conns_[key.first].circuitCount) conns_[key.first].circuitCount--;
    if(circs_.erase({ pd.vConn, pd.circIn }) && conns_.count(pd.vConn) && conns_[pd.vConn].circuitCount) conns_[pd.vConn].circuitCount--;
    auto vc = conns_.find(pd.vConn);
    if(vc != conns_.end()){
        uint8_t f[9];
        f[0] = kLinkClose;
        putU64(f + 1, pd.circIn);
        sendFrame(pd.vConn, Lane::Signal, f, sizeof f);
        std::string ip = ipKey(vc->second.ses.peer);
        if(!ip.empty()) avoid_[ip] = nowMs() + kBridgeAvoidMs;
    }
    if(conns_.count(key.first)) routeOpen(key.first, key.second, pd.relay, pd.relayPub, pd.owner);
    crypto_wipe(pd.owner, 32);
}

void RelayServer::bridgeFrame(uint64_t connId, const uint8_t* p, size_t n){
    auto cit = conns_.find(connId);
    if(cit == conns_.end() || n < 1) return;
    Conn& c = cit->second;
    uint8_t kind = p[0];
    uint32_t now = nowMs();
    if(kind == kLinkPing){
        uint8_t f[1] = { kLinkPing };
        sendFrame(connId, Lane::Signal, f, sizeof f);
        return;
    }
    if(kind == kLinkBridgeHello){
        if(n < 7) return;
        uint8_t ver = p[1];
        uint32_t cap = getU32(p + 2);
        uint8_t nr = p[6];
        if(ver != 1 || nr > kMaxBridgeRelays || n != 7 + (size_t)nr * 32) return;
        if(!c.bridge){
            std::string ip = ipKey(c.ses.peer);
            size_t total = 0;
            bool sameIp = false;
            for(auto& kv : conns_){
                if(!kv.second.bridge) continue;
                total++;
                if(ipKey(kv.second.ses.peer) == ip) sameIp = true;
            }
            bool ok = !ip.empty() && !sameIp && total < kMaxBridges;
            uint8_t f[2] = { kLinkBridgeWelcome, (uint8_t)(ok ? 1 : 0) };
            sendFrame(connId, Lane::Signal, f, sizeof f);
            if(!ok) return;
            c.bridge = true;
            c.bridgeWindowMs = now;
            c.bridgeIdleMs = now;
        } else {
            uint8_t f[2] = { kLinkBridgeWelcome, 1 };
            sendFrame(connId, Lane::Signal, f, sizeof f);
        }
        c.capKbps = cap;
        c.bridgeRelays.clear();
        for(uint8_t i = 0; i < nr; i++) c.bridgeRelays.push_back(keyStr(p + 7 + 32 * i));
        return;
    }
    if(!c.bridge) return;
    if(kind == kLinkBridgeIn){
        if(n != 1 + 8 + kEnvelopeLen) return;
        uint64_t circOut = getU64(p + 1);
        const uint8_t* env = p + 9;
        auto ack = [&](bool ok){
            uint8_t f[10];
            f[0] = kLinkBridgeAck;
            putU64(f + 1, circOut);
            f[9] = ok ? 1 : 0;
            sendFrame(connId, Lane::Signal, f, sizeof f);
        };
        uint8_t h[16];
        crypto_blake2b(h, sizeof h, env, kEnvelopeLen);
        std::string hk((const char*)h, sizeof h);
        uint8_t owner[32];
        if(envSeen_.count(hk) || circs_.count({connId, circOut}) || c.circuitCount >= kMaxCircuitsPerConn || !openEnvelope(env, owner)){
            ack(false);
            return;
        }
        if(envSeen_.size() > 100000) envSeen_.clear();
        envSeen_[hk] = now;
        auto t = byOwner_.find(keyStr(owner));
        bool ok = t != byOwner_.end() && conns_.count(t->second);
        if(ok) openLocal(connId, circOut, std::string(), owner);
        crypto_wipe(owner, 32);
        ack(ok && circs_.count({connId, circOut}) != 0);
        return;
    }
    if(kind == kLinkBridgeAck){
        if(n != 10) return;
        uint64_t circIn = getU64(p + 1);
        auto it = pendingByBridge_.find({connId, circIn});
        if(it == pendingByBridge_.end()) return;
        std::pair<uint64_t, uint64_t> key = it->second;
        if(p[9] == 1){
            auto pa = pendingBridge_.find(key);
            if(pa != pendingBridge_.end()){
                crypto_wipe(pa->second.owner, 32);
                pendingBridge_.erase(pa);
            }
            pendingByBridge_.erase(it);
            return;
        }
        bridgeFallback(key);
    }
}

void RelayServer::tickBridges(uint32_t now){
    std::vector<std::pair<uint64_t, uint64_t>> late;
    for(auto& kv : pendingBridge_) if((int32_t)(now - kv.second.deadline) >= 0) late.push_back(kv.first);
    for(auto& k : late) bridgeFallback(k);
    while(!bridgeOpens_.empty() && (int32_t)(now - bridgeOpens_.front().first) > (int32_t)kBridgeShareWindowMs) bridgeOpens_.pop_front();
    for(auto it = avoid_.begin(); it != avoid_.end();) if((int32_t)(now - it->second) >= 0) it = avoid_.erase(it); else ++it;
    for(auto it = envSeen_.begin(); it != envSeen_.end();) if((int32_t)(now - it->second) > (int32_t)kEnvSeenMs) it = envSeen_.erase(it); else ++it;
    std::vector<uint64_t> idle;
    for(auto& kv : conns_){
        Conn& b = kv.second;
        if(!b.bridge) continue;
        if((int32_t)(now - b.bridgeWindowMs) >= 60000){
            double kbps = (double)b.bridgeBytes * 8.0 / (double)(now - b.bridgeWindowMs);
            b.goodKbps = b.goodKbps * 0.8 + kbps * 0.2;
            b.bridgeBytes = 0;
            b.bridgeWindowMs = now;
        }
        if(b.circuitCount) b.bridgeIdleMs = now;
        else if((int32_t)(now - b.bridgeIdleMs) > (int32_t)kBridgeIdleMs) idle.push_back(kv.first);
    }
    for(uint64_t id : idle) dropConn(id);
}

void RelayServer::frame(uint64_t connId, const uint8_t* p, size_t n){
    auto cit = conns_.find(connId);
    if(cit == conns_.end() || n < 1) return;
    Conn& c = cit->second;
    uint8_t kind = p[0];
    uint32_t now = nowMs();
    if(c.bridge && kind != kLinkData && kind != kLinkClose){
        bridgeFrame(connId, p, n);
        return;
    }
    if(kind == kLinkBridgeHello){
        if(!c.registered && !c.dialed) bridgeFrame(connId, p, n);
        return;
    }
    if(kind == kLinkRegister){
        if(c.dialed) return;
        std::string owner = keyStr(c.ses.peerPub);
        auto old = byOwner_.find(owner);
        if(old != byOwner_.end() && old->second != connId && conns_.count(old->second)){
            uint64_t oldId = old->second;
            replaceLink(oldId, connId);
            auto oc = conns_.find(oldId);
            if(oc != conns_.end()){ sessionWipe(oc->second.ses); conns_.erase(oc); }
        }
        byOwner_[owner] = connId;
        conns_[connId].registered = true;
        uint8_t f[2] = { kLinkRegistered, 1 };
        sendFrame(connId, Lane::Signal, f, sizeof f);
        return;
    }
    if(kind == kLinkPing){
        if(c.dialed) return;
        uint8_t f[1] = { kLinkPing };
        sendFrame(connId, Lane::Signal, f, sizeof f);
        return;
    }
    if(kind == kLinkOpen){
        if(n < 1 + 8 + 19 + 32 + 32) return;
        uint64_t circ = getU64(p + 1);
        if(!c.registered || c.dialed || c.circuitCount >= kMaxCircuitsPerConn){
            uint8_t f[9];
            f[0] = kLinkClose;
            putU64(f + 1, circ);
            sendFrame(connId, Lane::Signal, f, sizeof f);
            return;
        }
        if(circs_.count({connId, circ})) return;
        Addr relay;
        if(!decodeAddr(p + 9, relay)) return;
        uint8_t flags = n >= 1 + 8 + 19 + 32 + 32 + 1 ? p[92] : 0;
        if((flags & kOpenMasked) && tryBridge(connId, circ, relay, p + 28, p + 60)) return;
        routeOpen(connId, circ, relay, p + 28, p + 60);
        return;
    }
    if(kind == kLinkForward){
        if(n < 1 + 8 + 32 || c.dialed || c.registered) return;
        uint64_t cr = getU64(p + 1);
        if(circs_.count({connId, cr})) return;
        if(c.circuitCount >= kMaxCircuitsPerConn){
            uint8_t f[9];
            f[0] = kLinkClose;
            putU64(f + 1, cr);
            sendFrame(connId, Lane::Signal, f, sizeof f);
            return;
        }
        openLocal(connId, cr, std::string(), p + 9);
        return;
    }
    if(kind == kLinkData || kind == kLinkClose){
        if(n < 9) return;
        uint64_t circ = getU64(p + 1);
        if(kind == kLinkClose){ dropCircuit(connId, circ, true); return; }
        const uint8_t* payload = p + 9;
        size_t pn = n - 9;
        Circ* here = nullptr;
        End target;
        if(c.dialed){
            auto r = remoteIn_.find(remoteKey(c.peerKey, circ));
            if(r == remoteIn_.end()) return;
            auto lc = circs_.find(r->second);
            if(lc == circs_.end()) return;
            here = &lc->second;
            target.conn = r->second.first;
            target.circ = r->second.second;
        } else {
            auto lc = circs_.find({connId, circ});
            if(lc == circs_.end()) return;
            here = &lc->second;
            target = lc->second.other;
        }
        here->tokens = std::min(kCircuitBurst, here->tokens + (now - here->refillMs) * kCircuitBytesPerMs);
        here->refillMs = now;
        if(here->tokens < (double)pn) return;
        here->tokens -= (double)pn;
        here->lastMs = now;
        if(c.bridge) c.bridgeBytes += pn;
        if(target.peerKey.empty()){
            auto tc = conns_.find(target.conn);
            if(tc != conns_.end() && tc->second.bridge) tc->second.bridgeBytes += pn;
        }
        if(target.peerKey.empty()){
            auto o = circs_.find({target.conn, target.circ});
            if(o != circs_.end()) o->second.lastMs = now;
        }
        sendFrameTo(target, kLinkData, target.circ, payload, pn, laneOf(payload, pn));
        return;
    }
    if(kind == kLinkPublish){
        if(!c.registered || c.dialed) return;
        PointerRecord r;
        bool ok = openPointer(p + 1, n - 1, wallSeconds(), r) && crypto_verify32(r.owner, c.ses.peerPub) == 0 && (crypto_verify32(r.relayPub, me_.pub) == 0 || (r.relays == 2 && crypto_verify32(r.relayPub2, me_.pub) == 0));
        if(!ok){
            uint8_t f[2] = { kLinkPublished, 0 };
            sendFrame(connId, Lane::Signal, f, sizeof f);
            return;
        }
        Job j;
        j.conn = connId;
        j.publish = true;
        j.record.assign(p + 1, p + n);
        j.pinned = false;
        {
            std::lock_guard<std::mutex> g(jobMu_);
            if(jobs_.size() < 256 && std::count_if(jobs_.begin(), jobs_.end(), [connId](const Job& q){ return q.conn == connId; }) < (long)kMaxJobsPerConn) jobs_.push_back(j);
        }
        jobCv_.notify_one();
        return;
    }
    if(kind == kLinkLookup){
        if(!c.registered || c.dialed || n < 2) return;
        size_t len = p[1];
        if(n < 2 + len + 32) return;
        Job j;
        j.conn = connId;
        j.publish = false;
        j.name.assign((const char*)p + 2, len);
        std::memcpy(j.pin, p + 2 + len, 32);
        uint8_t acc = 0;
        for(uint8_t b : j.pin) acc |= b;
        j.pinned = acc != 0;
        {
            std::lock_guard<std::mutex> g(jobMu_);
            if(jobs_.size() < 256 && std::count_if(jobs_.begin(), jobs_.end(), [connId](const Job& q){ return q.conn == connId; }) < (long)kMaxJobsPerConn) jobs_.push_back(j);
        }
        jobCv_.notify_one();
        return;
    }
}

bool RelayServer::handle(const uint8_t* pkt, size_t n, const Addr& from){
    Header h;
    if(!readHeader(pkt, n, h)) return false;
    uint32_t now = nowMs();
    if(h.type == PacketType::Link){
        if(n >= kKeyRequestLen && pkt[kHeaderLen] == kKeyRequest && allow(from)){
            uint8_t r[kHeaderLen + 1 + 32];
            writeHeader(r, PacketType::Link, Lane::Signal);
            r[kHeaderLen] = kKeyReply;
            std::memcpy(r + kHeaderLen + 1, me_.pub, 32);
            s_.sendTo(r, sizeof r, from);
        }
        return true;
    }
    if(h.type == PacketType::Hello){
        if(!allow(from) || conns_.size() >= kMaxConns) return true;
        Session ses;
        std::string caps;
        uint64_t stamp = 0;
        uint8_t reply[kMaxPacket];
        size_t rl = 0;
        if(!hsAccept(pkt, n, me_, caps_, ses, caps, stamp, reply, sizeof reply, rl)) return true;
        std::string k = keyStr(ses.peerPub);
        if(stamps_.size() > kMaxStamps) stamps_.clear();
        auto st = stamps_.find(k);
        if(st != stamps_.end() && stamp <= st->second){ sessionWipe(ses); return true; }
        stamps_[k] = stamp;
        ses.peer = from;
        Conn c;
        c.ses = ses;
        c.lastRecvMs = now;
        c.lastSendMs = now;
        uint64_t id = ses.id;
        sessionWipe(ses);
        conns_[id] = c;
        sessionWipe(c.ses);
        s_.sendTo(reply, rl, from);
        return true;
    }
    if(h.type == PacketType::Welcome){
        for(auto& kv : peers_){
            Peer& p = kv.second;
            if(!p.dialing) continue;
            Session ses;
            std::string caps;
            if(!p.dial.finish(pkt, n, ses, caps)) continue;
            ses.peer = from;
            Conn c;
            c.ses = ses;
            c.dialed = true;
            c.confirmed = true;
            c.lastRecvMs = now;
            c.lastSendMs = now;
            c.peerKey = kv.first;
            uint64_t id = ses.id;
            sessionWipe(ses);
            conns_[id] = c;
            sessionWipe(c.ses);
            p.conn = id;
            p.dialing = false;
            p.tries = 0;
            for(int i = 0; i < 2; i++){
                uint8_t ka[kDataHeaderLen + kTagLen];
                size_t len = 0;
                if(sessionSeal(conns_[id].ses, Lane::Signal, nullptr, 0, ka, sizeof ka, len)) s_.sendTo(ka, len, conns_[id].ses.peer);
            }
            std::deque<std::vector<uint8_t>> q;
            q.swap(p.queued);
            for(const auto& f : q) sendToPeer(p, f);
            return true;
        }
        return false;
    }
    if(h.type == PacketType::Data){
        if(n < kDataHeaderLen) return false;
        auto it = conns_.find(getU64(pkt + kHeaderLen));
        if(it == conns_.end()) return false;
        uint8_t plain[kMaxPacket];
        size_t pn = 0;
        Lane lane;
        if(!sessionOpen(it->second.ses, pkt, n, from, lane, plain, sizeof plain, pn)) return true;
        it->second.lastRecvMs = now;
        it->second.confirmed = true;
        if(pn) frame(it->first, plain, pn);
        return true;
    }
    return false;
}

void RelayServer::tick(){
    uint32_t now = nowMs();
    std::vector<uint64_t> dead;
    for(auto& kv : conns_){
        if((int32_t)(now - kv.second.lastRecvMs) > (int32_t)kLinkSilentMs) dead.push_back(kv.first);
        else if(kv.second.dialed && (int32_t)(now - kv.second.lastSendMs) > (int32_t)kPeerPingMs){
            uint8_t f[1] = { kLinkPing };
            sendFrame(kv.first, Lane::Signal, f, sizeof f);
        }
    }
    for(uint64_t id : dead) dropConn(id);
    std::vector<std::pair<uint64_t, uint64_t>> idle;
    for(auto& kv : circs_) if((int32_t)(now - kv.second.lastMs) > (int32_t)kCircuitIdleMs) idle.push_back(kv.first);
    for(auto& k : idle) if(circs_.count(k)) dropCircuit(k.first, k.second, true);
    std::vector<std::string> giveUp;
    for(auto& kv : peers_){
        Peer& p = kv.second;
        if(!p.dialing) continue;
        uint32_t wait = 1000u << std::min(p.tries, 3);
        if((int32_t)(now - p.lastDialMs) < (int32_t)wait) continue;
        if(p.tries >= 8) giveUp.push_back(kv.first);
        else dialPeer(p);
    }
    for(const std::string& k : giveUp){
        std::vector<std::string> gone;
        for(auto& kv : remoteIn_) if(kv.first.compare(0, 32, k) == 0) gone.push_back(kv.first);
        for(const std::string& g : gone){
            std::pair<uint64_t, uint64_t> local = remoteIn_[g];
            remoteIn_.erase(g);
            if(circs_.erase(local)){
                if(conns_.count(local.first) && conns_[local.first].circuitCount) conns_[local.first].circuitCount--;
                uint8_t f[9];
                f[0] = kLinkClose;
                putU64(f + 1, local.second);
                sendFrame(local.first, Lane::Signal, f, sizeof f);
            }
        }
        peers_.erase(k);
    }
    std::deque<Done> results;
    {
        std::lock_guard<std::mutex> g(jobMu_);
        results.swap(done_);
    }
    for(const Done& d : results){
        if(!conns_.count(d.conn)) continue;
        if(d.publish){
            uint8_t f[2] = { kLinkPublished, (uint8_t)(d.ok ? 1 : d.taken ? 0 : 2) };
            sendFrame(d.conn, Lane::Signal, f, sizeof f);
        } else {
            std::vector<uint8_t> f(2 + (d.ok ? d.record.size() : 0));
            f[0] = kLinkResult;
            f[1] = d.ok ? 1 : 0;
            if(d.ok && !d.record.empty()) std::memcpy(f.data() + 2, d.record.data(), d.record.size());
            sendFrame(d.conn, Lane::Signal, f.data(), f.size());
        }
    }
    tickBridges(nowMs());
}

void RelayServer::workerLoop(){
    UdpSocket ws;
    std::string err;
    StarDns dns;
    bool started = false;
    uint32_t lastTry = 0;
    for(;;){
        Job j;
        bool have = false;
        {
            std::unique_lock<std::mutex> g(jobMu_);
            jobCv_.wait_for(g, std::chrono::milliseconds(500), [this]{ return stop_ || !jobs_.empty(); });
            if(stop_) break;
            if(!jobs_.empty()){ j = jobs_.front(); jobs_.pop_front(); have = true; }
        }
        if(!started && (have || nowMs() - lastTry > 30000)){
            lastTry = nowMs();
            if(ws.localPort() == 0) ws.open(dhtSeeds_.empty() ? familyV4() : dhtSeeds_[0].family(), 0, err);
            started = dns.start(ws, dhtSeeds_, err);
        }
        if(started) dns.poll();
        if(!have) continue;
        Done d;
        d.conn = j.conn;
        d.publish = j.publish;
        d.ok = false;
        if(started){
            if(j.publish){
                d.ok = dns.publishRecord(j.record.data(), j.record.size(), err);
                d.taken = !d.ok && err.find("already taken") != std::string::npos;
            } else {
                PointerRecord r;
                d.ok = dns.lookup(j.name, j.pinned ? j.pin : nullptr, r, err, &d.record);
            }
        }
        std::lock_guard<std::mutex> g(jobMu_);
        done_.push_back(d);
    }
}

}

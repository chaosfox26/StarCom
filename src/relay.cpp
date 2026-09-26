#include "relay.h"
#include "../third_party/monocypher/monocypher.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace sc {

namespace {

const uint32_t kLinkSilentMs = 75000;
const uint32_t kCircuitIdleMs = 120000;
const uint32_t kPeerPingMs = 25000;
const size_t kMaxConns = 2000;
const size_t kMaxCircuitsPerConn = 64;
const size_t kMaxQueued = 64;
const double kCircuitBytesPerMs = 256.0;
const double kCircuitBurst = 512.0 * 1024.0;
const size_t kMaxStamps = 20000;
const size_t kMaxPeers = 256;
const int kMaxNewPeersPerMinute = 4;
const size_t kMaxRates = 8192;

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
    identityWipe(me_);
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

void RelayServer::frame(uint64_t connId, const uint8_t* p, size_t n){
    auto cit = conns_.find(connId);
    if(cit == conns_.end() || n < 1) return;
    Conn& c = cit->second;
    uint8_t kind = p[0];
    uint32_t now = nowMs();
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
        const uint8_t* relayPub = p + 28;
        const uint8_t* owner = p + 60;
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
        bool ok = openPointer(p + 1, n - 1, wallSeconds(), r) && crypto_verify32(r.owner, c.ses.peerPub) == 0 && crypto_verify32(r.relayPub, me_.pub) == 0;
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
            if(jobs_.size() < 256) jobs_.push_back(j);
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
            if(jobs_.size() < 256) jobs_.push_back(j);
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
        if(now - kv.second.lastRecvMs > kLinkSilentMs) dead.push_back(kv.first);
        else if(kv.second.dialed && now - kv.second.lastSendMs > kPeerPingMs){
            uint8_t f[1] = { kLinkPing };
            sendFrame(kv.first, Lane::Signal, f, sizeof f);
        }
    }
    for(uint64_t id : dead) dropConn(id);
    std::vector<std::pair<uint64_t, uint64_t>> idle;
    for(auto& kv : circs_) if(now - kv.second.lastMs > kCircuitIdleMs) idle.push_back(kv.first);
    for(auto& k : idle) if(circs_.count(k)) dropCircuit(k.first, k.second, true);
    std::vector<std::string> giveUp;
    for(auto& kv : peers_){
        Peer& p = kv.second;
        if(!p.dialing) continue;
        uint32_t wait = 1000u << std::min(p.tries, 3);
        if(now - p.lastDialMs < wait) continue;
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
            uint8_t f[2] = { kLinkPublished, (uint8_t)(d.ok ? 1 : 0) };
            sendFrame(d.conn, Lane::Signal, f, sizeof f);
        } else {
            std::vector<uint8_t> f(2 + (d.ok ? d.record.size() : 0));
            f[0] = kLinkResult;
            f[1] = d.ok ? 1 : 0;
            if(d.ok && !d.record.empty()) std::memcpy(f.data() + 2, d.record.data(), d.record.size());
            sendFrame(d.conn, Lane::Signal, f.data(), f.size());
        }
    }
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

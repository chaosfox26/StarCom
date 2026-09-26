#include "dht.h"
#include "../third_party/monocypher/monocypher.h"
#include "../third_party/monocypher/monocypher-ed25519.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace sc {

namespace {

enum Op : uint8_t { kPing = 1, kPong = 2, kFindNode = 3, kNodes = 4, kFindValue = 5, kValue = 6, kStore = 7, kStored = 8 };

const size_t kBody = kHeaderLen + 1 + 1 + 8 + 32;
const size_t kContactLen = 32 + 19;
const uint8_t kFlagClientOnly = 1;
const uint32_t kStaleMs = 15 * 60 * 1000;
const uint32_t kEvictWaitMs = 2000;
const uint32_t kSecretMs = 10 * 60 * 1000;
const uint32_t kRefreshMs = 60 * 60 * 1000;
const size_t kMaxRecords = 10000;
const size_t kMaxRateEntries = 8192;
const char kMagic[4] = {'S', 'C', 'P', '2'};
const char kMagic3[4] = {'S', 'C', 'P', '3'};

uint32_t nowMs(){
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void put32(uint8_t* p, uint32_t v){ for(int i = 0; i < 4; i++){ p[i] = (uint8_t)v; v >>= 8; } }
uint32_t get32(const uint8_t* p){ uint32_t v = 0; for(int i = 3; i >= 0; i--) v = (v << 8) | p[i]; return v; }

int bucketIndex(const uint8_t a[32], const uint8_t b[32]){
    for(int i = 0; i < 32; i++){
        uint8_t x = a[i] ^ b[i];
        if(x){
            int lz = 0;
            while(!(x & 0x80)){ x <<= 1; lz++; }
            return 255 - (i * 8 + lz);
        }
    }
    return -1;
}

bool closer(const uint8_t target[32], const uint8_t a[32], const uint8_t b[32]){
    for(int i = 0; i < 32; i++){
        uint8_t da = a[i] ^ target[i], db = b[i] ^ target[i];
        if(da != db) return da < db;
    }
    return false;
}

std::string keyStr(const uint8_t k[32]){ return std::string((const char*)k, 32); }

std::string ipOnly(const Addr& a){
    uint8_t e[19];
    if(!encodeAddr(a, e)) return std::string();
    return std::string((const char*)e, 1) + std::string((const char*)e + 3, 16);
}

bool sameAddr(const Addr& a, const Addr& b){
    uint8_t x[19], y[19];
    return encodeAddr(a, x) && encodeAddr(b, y) && std::memcmp(x, y, 19) == 0;
}

}

uint32_t wallSeconds(){
    return (uint32_t)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool normalizeName(const std::string& in, std::string& out){
    std::string s = in;
    if(!s.empty() && s[0] == '@') s.erase(0, 1);
    if(s.size() < 3 || s.size() > 32) return false;
    out.clear();
    for(char c : s){
        if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        out += c;
    }
    return true;
}

void nameKey(const std::string& name, uint8_t key[32]){
    static const char label[] = "starcom-name";
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 32);
    crypto_blake2b_update(&ctx, (const uint8_t*)label, sizeof label - 1);
    crypto_blake2b_update(&ctx, (const uint8_t*)name.data(), name.size());
    crypto_blake2b_final(&ctx, key);
}

size_t signPointer(const Identity& id, const PointerRecord& r, uint8_t* out, size_t cap){
    std::string name;
    if(!normalizeName(r.name, name)) return 0;
    bool two = r.relays == 2;
    size_t n = 4 + 32 + 1 + name.size() + 8 + 4 + (two ? 1 + 2 * (19 + 32) : 19 + 32);
    if(n + 64 > cap || n + 64 > kMaxPointerLen) return 0;
    uint8_t* p = out;
    std::memcpy(p, two ? kMagic3 : kMagic, 4); p += 4;
    std::memcpy(p, id.pub, 32); p += 32;
    *p++ = (uint8_t)name.size();
    std::memcpy(p, name.data(), name.size()); p += name.size();
    putU64(p, r.seq); p += 8;
    put32(p, r.expires); p += 4;
    if(two) *p++ = 2;
    if(!encodeAddr(r.relay, p)) return 0;
    p += 19;
    std::memcpy(p, r.relayPub, 32);
    p += 32;
    if(two){
        if(!encodeAddr(r.relay2, p)) return 0;
        p += 19;
        std::memcpy(p, r.relayPub2, 32);
    }
    crypto_ed25519_sign(out + n, id.secret, out, n);
    return n + 64;
}

bool openPointer(const uint8_t* in, size_t n, uint32_t nowSec, PointerRecord& r){
    if(n < 4 + 32 + 1 + 3 + 8 + 4 + 19 + 32 + 64 || n > kMaxPointerLen) return false;
    bool two = std::memcmp(in, kMagic3, 4) == 0;
    if(!two && std::memcmp(in, kMagic, 4) != 0) return false;
    size_t nameLen = in[36];
    size_t body = 37 + nameLen + 8 + 4 + (two ? 1 + 2 * (19 + 32) : 19 + 32);
    if(nameLen < 3 || nameLen > 32 || body + 64 != n) return false;
    if(crypto_ed25519_check(in + body, in + 4, in, body) != 0) return false;
    std::string raw((const char*)in + 37, nameLen), norm;
    if(!normalizeName(raw, norm) || norm != raw) return false;
    PointerRecord t;
    std::memcpy(t.owner, in + 4, 32);
    t.name = norm;
    t.seq = getU64(in + 37 + nameLen);
    t.expires = get32(in + 37 + nameLen + 8);
    if(t.expires + kPointerSlackSec < nowSec || t.expires > nowSec + kPointerMaxLifeSec + kPointerSlackSec) return false;
    const uint8_t* q = in + 37 + nameLen + 12;
    if(two){
        if(*q != 2) return false;
        q++;
        t.relays = 2;
    }
    if(!decodeAddr(q, t.relay)) return false;
    std::memcpy(t.relayPub, q + 19, 32);
    if(two){
        if(!decodeAddr(q + 51, t.relay2)) return false;
        std::memcpy(t.relayPub2, q + 70, 32);
        if(std::memcmp(t.relayPub, t.relayPub2, 32) == 0) return false;
    }
    r = t;
    return true;
}

DhtNode::DhtNode(UdpSocket& s, bool clientOnly) : s_(s), clientOnly_(clientOnly){
    sysRandom(id_, 32);
    sysRandom(secret_, 32);
    std::memcpy(prevSecret_, secret_, 32);
    secretAtMs_ = nowMs();
    lastRefreshMs_ = nowMs();
}

size_t DhtNode::contacts() const {
    size_t n = 0;
    for(const auto& b : buckets_) n += b.size();
    return n;
}

size_t DhtNode::header(uint8_t* p, uint8_t op, uint64_t txid) const {
    writeHeader(p, PacketType::Dht, Lane::Background);
    p[kHeaderLen] = op;
    p[kHeaderLen + 1] = clientOnly_ ? kFlagClientOnly : 0;
    putU64(p + kHeaderLen + 2, txid);
    std::memcpy(p + kHeaderLen + 10, id_, 32);
    return kBody;
}

void DhtNode::makeToken(const Addr& a, const uint8_t secret[32], uint8_t out[16]) const {
    uint8_t e[19] = {0};
    encodeAddr(a, e);
    uint8_t h[16];
    crypto_blake2b_keyed(h, 16, secret, 32, e, 19);
    std::memcpy(out, h, 16);
}

bool DhtNode::tokenOk(const Addr& a, const uint8_t tok[16]) const {
    uint8_t t[16];
    makeToken(a, secret_, t);
    if(crypto_verify16(t, tok) == 0) return true;
    makeToken(a, prevSecret_, t);
    return crypto_verify16(t, tok) == 0;
}

bool DhtNode::allow(const Addr& a){
    std::string k = ipOnly(a);
    if(k.empty()) return false;
    if(rate_.size() > kMaxRateEntries) rate_.clear();
    Bucket& b = rate_[k];
    uint32_t now = nowMs();
    if(b.last) b.tokens = std::min(40.0, b.tokens + (now - b.last) * 0.02);
    b.last = now;
    if(b.tokens < 1) return false;
    b.tokens -= 1;
    return true;
}

void DhtNode::seen(const uint8_t id[32], const Addr& a){
    int bi = bucketIndex(id_, id);
    if(bi < 0) return;
    std::vector<DhtContact>& b = buckets_[bi];
    uint32_t now = nowMs();
    for(size_t i = 0; i < b.size(); i++){
        if(std::memcmp(b[i].id, id, 32) == 0){
            DhtContact c = b[i];
            c.addr = a;
            c.lastSeenMs = now;
            b.erase(b.begin() + (long)i);
            b.push_back(c);
            return;
        }
    }
    DhtContact c;
    std::memcpy(c.id, id, 32);
    c.addr = a;
    c.lastSeenMs = now;
    if(b.size() < kDhtK){ b.push_back(c); return; }
    for(const Eviction& e : evictions_) if(e.bucket == bi) return;
    if(now - b.front().lastSeenMs < kStaleMs) return;
    uint64_t tx = 0;
    sysRandom((uint8_t*)&tx, 8);
    uint8_t pkt[kBody];
    header(pkt, kPing, tx);
    s_.sendTo(pkt, kBody, b.front().addr);
    waiting_.push_back(tx);
    evictions_.push_back(Eviction{ bi, c, tx, now });
}

std::vector<DhtContact> DhtNode::closestKnown(const uint8_t target[32], size_t n) const {
    std::vector<DhtContact> all;
    for(const auto& b : buckets_) all.insert(all.end(), b.begin(), b.end());
    std::sort(all.begin(), all.end(), [&](const DhtContact& x, const DhtContact& y){ return closer(target, x.id, y.id); });
    if(all.size() > n) all.resize(n);
    return all;
}

void DhtNode::answer(uint8_t op, uint64_t txid, const uint8_t* body, size_t n, const Addr& from, size_t inLen){
    uint8_t pkt[kDhtPadded];
    size_t len = header(pkt, op, txid);
    if(len + n > sizeof pkt) return;
    if(n) std::memcpy(pkt + len, body, n);
    len += n;
    if(op != kPong && op != kStored && len > inLen) return;
    s_.sendTo(pkt, len, from);
}

bool DhtNode::acceptStore(const uint8_t* rec, size_t n){
    PointerRecord r;
    if(!openPointer(rec, n, wallSeconds(), r)) return false;
    uint8_t key[32];
    nameKey(r.name, key);
    std::string k = keyStr(key);
    auto it = store_.find(k);
    if(it != store_.end() && it->second.expires + kPointerSlackSec >= wallSeconds()){
        if(std::memcmp(it->second.owner, r.owner, 32) != 0) return false;
        if(r.seq <= it->second.seq) return false;
    }
    if(it == store_.end() && store_.size() >= kMaxRecords){
        auto victim = store_.begin();
        for(auto j = store_.begin(); j != store_.end(); ++j) if(j->second.expires < victim->second.expires) victim = j;
        store_.erase(victim);
    }
    Stored& s = store_[k];
    s.bytes.assign(rec, rec + n);
    std::memcpy(s.owner, r.owner, 32);
    s.seq = r.seq;
    s.expires = r.expires;
    return true;
}

void DhtNode::handle(const uint8_t* pkt, size_t n, const Addr& from){
    Header h;
    if(!readHeader(pkt, n, h) || h.type != PacketType::Dht || n < kBody) return;
    uint8_t op = pkt[kHeaderLen];
    uint8_t flags = pkt[kHeaderLen + 1];
    uint64_t txid = getU64(pkt + kHeaderLen + 2);
    const uint8_t* sid = pkt + kHeaderLen + 10;
    const uint8_t* b = pkt + kBody;
    size_t bn = n - kBody;
    bool selfId = std::memcmp(sid, id_, 32) == 0;
    if(op == kPong || op == kNodes || op == kValue || op == kStored){
        auto w = std::find(waiting_.begin(), waiting_.end(), txid);
        if(w == waiting_.end()) return;
        waiting_.erase(w);
        if(!selfId) seen(sid, from);
        for(size_t i = 0; i < evictions_.size(); i++){
            if(evictions_[i].txid == txid){ evictions_.erase(evictions_.begin() + (long)i); break; }
        }
        Reply r;
        r.txid = txid;
        r.op = op;
        r.from = from;
        std::memcpy(r.id, sid, 32);
        r.haveToken = false;
        r.stored = false;
        if((op == kNodes || op == kValue) && bn >= 16){
            std::memcpy(r.token, b, 16);
            r.haveToken = true;
            if(op == kNodes && bn >= 17){
                size_t count = b[16];
                if(count > kDhtK || 17 + count * kContactLen > bn) return;
                for(size_t i = 0; i < count; i++){
                    DhtContact c;
                    std::memcpy(c.id, b + 17 + i * kContactLen, 32);
                    if(!decodeAddr(b + 17 + i * kContactLen + 32, c.addr)) continue;
                    r.nodes.push_back(c);
                }
            } else if(op == kValue){
                if(bn - 16 > kMaxPointerLen) return;
                r.value.assign(b + 16, b + bn);
            }
        } else if(op == kStored){
            r.stored = bn >= 1 && b[0] == 1;
        }
        replies_.push_back(r);
        if(replies_.size() > 256) replies_.pop_front();
        return;
    }
    if(op != kPing && op != kFindNode && op != kFindValue && op != kStore) return;
    if(selfId || !allow(from)) return;
    clientOnly_ = false;
    if(!(flags & kFlagClientOnly)) seen(sid, from);
    if(op == kPing){ answer(kPong, txid, nullptr, 0, from, n); return; }
    if(op == kFindNode || op == kFindValue){
        if(n < kDhtPadded || bn < 32) return;
        uint8_t out[16 + 1 + kDhtK * kContactLen];
        std::memset(out, 0, 16);
        if(op == kFindValue) makeToken(from, secret_, out);
        if(op == kFindValue){
            auto it = store_.find(keyStr(b));
            if(it != store_.end() && it->second.expires + kPointerSlackSec >= wallSeconds()){
                uint8_t v[16 + kMaxPointerLen];
                std::memcpy(v, out, 16);
                std::memcpy(v + 16, it->second.bytes.data(), it->second.bytes.size());
                answer(kValue, txid, v, 16 + it->second.bytes.size(), from, n);
                return;
            }
        }
        std::vector<DhtContact> near = closestKnown(b, kDhtK);
        size_t len = 17;
        out[16] = 0;
        for(const DhtContact& c : near){
            if(sameAddr(c.addr, from)) continue;
            std::memcpy(out + len, c.id, 32);
            if(!encodeAddr(c.addr, out + len + 32)) continue;
            len += kContactLen;
            out[16]++;
        }
        answer(kNodes, txid, out, len, from, n);
        return;
    }
    if(op == kStore){
        if(bn < 16 + 1) return;
        uint8_t ok = tokenOk(from, b) && acceptStore(b + 16, bn - 16) ? 1 : 0;
        answer(kStored, txid, &ok, 1, from, n);
    }
}

void DhtNode::tick(){
    uint32_t now = nowMs();
    if(now - secretAtMs_ >= kSecretMs){
        std::memcpy(prevSecret_, secret_, 32);
        sysRandom(secret_, 32);
        secretAtMs_ = now;
    }
    uint32_t wall = wallSeconds();
    for(auto it = store_.begin(); it != store_.end();){
        if(it->second.expires + kPointerSlackSec < wall) it = store_.erase(it); else ++it;
    }
    for(size_t i = 0; i < evictions_.size();){
        Eviction& e = evictions_[i];
        if(now - e.sentMs < kEvictWaitMs){ i++; continue; }
        std::vector<DhtContact>& b = buckets_[e.bucket];
        if(!b.empty()) b.erase(b.begin());
        e.candidate.lastSeenMs = now;
        b.push_back(e.candidate);
        auto w = std::find(waiting_.begin(), waiting_.end(), e.txid);
        if(w != waiting_.end()) waiting_.erase(w);
        evictions_.erase(evictions_.begin() + (long)i);
    }
}

void DhtNode::pump(int maxMs){
    uint8_t in[kMaxPacket];
    Addr from;
    int r = s_.recvFrom(in, sizeof in, from, maxMs < 0 ? 0 : maxMs);
    if(r > 0) handle(in, (size_t)r, from);
    tick();
}

void DhtNode::drain(){
    uint8_t in[kMaxPacket];
    for(int i = 0; i < 64; i++){
        Addr from;
        int r = s_.recvFrom(in, sizeof in, from, 0);
        if(r <= 0) break;
        handle(in, (size_t)r, from);
    }
    tick();
}

bool DhtNode::waitReply(uint64_t txid, int maxMs, Reply& out){
    uint32_t start = nowMs();
    for(;;){
        for(auto it = replies_.begin(); it != replies_.end(); ++it){
            if(it->txid == txid){ out = *it; replies_.erase(it); return true; }
        }
        int left = maxMs - (int)(nowMs() - start);
        if(left <= 0) break;
        pump(left < 50 ? left : 50);
    }
    auto w = std::find(waiting_.begin(), waiting_.end(), txid);
    if(w != waiting_.end()) waiting_.erase(w);
    return false;
}

bool DhtNode::ping(const Addr& to, int maxMs){
    uint64_t tx = 0;
    sysRandom((uint8_t*)&tx, 8);
    uint8_t pkt[kBody];
    header(pkt, kPing, tx);
    waiting_.push_back(tx);
    s_.sendTo(pkt, kBody, to);
    Reply r;
    return waitReply(tx, maxMs, r);
}

bool DhtNode::iterate(const uint8_t target[32], bool wantValue, const std::vector<Addr>& seeds,
                      std::vector<Found>& closest, std::vector<std::vector<uint8_t>>* values, int maxMs){
    std::vector<Found> list;
    for(const DhtContact& c : closestKnown(target, kDhtK * 2)){
        Found f;
        f.c = c;
        f.haveId = true;
        list.push_back(f);
    }
    if(list.empty()){
        for(const Addr& a : seeds){
            Found f;
            f.c.addr = a;
            list.push_back(f);
        }
    }
    if(list.empty()) return false;
    uint32_t start = nowMs();
    auto order = [&](){
        std::stable_sort(list.begin(), list.end(), [&](const Found& x, const Found& y){
            if(x.failed != y.failed) return !x.failed;
            if(x.haveId != y.haveId) return x.haveId;
            return x.haveId && closer(target, x.c.id, y.c.id);
        });
    };
    for(;;){
        uint32_t now = nowMs();
        for(Reply& r : replies_){
            std::vector<DhtContact> learned;
            for(Found& f : list){
                if(!f.queried || f.answered || f.failed || f.txid != r.txid) continue;
                f.answered = true;
                std::memcpy(f.c.id, r.id, 32);
                f.haveId = true;
                if(r.haveToken){ std::memcpy(f.token, r.token, 16); f.haveToken = true; }
                if(r.op == kValue && values) values->push_back(r.value);
                learned = r.nodes;
                r.txid = 0;
                break;
            }
            for(const DhtContact& c : learned){
                if(std::memcmp(c.id, id_, 32) == 0) continue;
                bool dup = false;
                for(const Found& g : list) if((g.haveId && std::memcmp(g.c.id, c.id, 32) == 0) || sameAddr(g.c.addr, c.addr)) dup = true;
                if(dup) continue;
                Found nf;
                nf.c = c;
                nf.haveId = true;
                list.push_back(nf);
            }
        }
        replies_.erase(std::remove_if(replies_.begin(), replies_.end(), [](const Reply& r){ return r.txid == 0; }), replies_.end());
        for(Found& f : list){
            if(f.queried && !f.answered && !f.failed && now - f.sentMs > 1000){
                f.failed = true;
                auto w = std::find(waiting_.begin(), waiting_.end(), f.txid);
                if(w != waiting_.end()) waiting_.erase(w);
            }
        }
        order();
        size_t inflight = 0, considered = 0;
        bool allDone = true;
        for(Found& f : list){
            if(f.failed) continue;
            if(considered++ >= kDhtK) break;
            if(f.queried && !f.answered) inflight++;
            if(!f.answered) allDone = false;
        }
        if(allDone && inflight == 0) break;
        if((int)(now - start) > maxMs) break;
        considered = 0;
        for(Found& f : list){
            if(inflight >= kDhtAlpha) break;
            if(f.failed) continue;
            if(considered++ >= kDhtK) break;
            if(f.queried) continue;
            uint8_t pkt[kDhtPadded];
            std::memset(pkt, 0, sizeof pkt);
            sysRandom((uint8_t*)&f.txid, 8);
            header(pkt, wantValue ? kFindValue : kFindNode, f.txid);
            std::memcpy(pkt + kBody, target, 32);
            waiting_.push_back(f.txid);
            s_.sendTo(pkt, kDhtPadded, f.c.addr);
            f.queried = true;
            f.sentMs = now;
            inflight++;
        }
        pump(50);
    }
    closest.clear();
    for(const Found& f : list){
        if(f.answered) closest.push_back(f);
        if(closest.size() >= kDhtK) break;
    }
    return !closest.empty();
}

bool DhtNode::storeAt(const Found& f, const uint8_t* record, size_t n, int maxMs){
    if(!f.haveToken || n > kMaxPointerLen) return false;
    uint8_t pkt[kBody + 16 + kMaxPointerLen];
    uint64_t tx = 0;
    sysRandom((uint8_t*)&tx, 8);
    size_t len = header(pkt, kStore, tx);
    std::memcpy(pkt + len, f.token, 16);
    std::memcpy(pkt + len + 16, record, n);
    waiting_.push_back(tx);
    s_.sendTo(pkt, len + 16 + n, f.c.addr);
    Reply r;
    return waitReply(tx, maxMs, r) && r.op == kStored && r.stored;
}

bool StarDns::start(UdpSocket& s, const std::vector<Addr>& seeds, std::string& err){
    delete node_;
    node_ = new DhtNode(s, true);
    seeds_ = seeds;
    if(seeds_.empty()){ err = "this copy of StarCom has no seed nodes built in"; return false; }
    std::vector<DhtNode::Found> closest;
    if(!node_->iterate(node_->id(), false, seeds_, closest, nullptr, 8000) || node_->contacts() == 0){
        err = "could not reach any seed node";
        return false;
    }
    return true;
}

bool StarDns::publishRecord(const uint8_t* rec, size_t n, std::string& err){
    if(!node_){ err = "Star DNS is not started"; return false; }
    PointerRecord r;
    if(!openPointer(rec, n, wallSeconds(), r)){ err = "the pointer record is not valid"; return false; }
    uint8_t key[32];
    nameKey(r.name, key);
    std::vector<DhtNode::Found> closest;
    std::vector<std::vector<uint8_t>> existing;
    if(!node_->iterate(key, true, seeds_, closest, &existing, 8000)){ err = "no Star DNS nodes answered"; return false; }
    uint32_t now = wallSeconds();
    for(const auto& v : existing){
        PointerRecord o;
        if(openPointer(v.data(), v.size(), now, o) && o.name == r.name && std::memcmp(o.owner, r.owner, 32) != 0){
            err = "@" + r.name + " is already taken by someone else";
            return false;
        }
    }
    int stored = 0;
    for(const DhtNode::Found& f : closest) if(node_->storeAt(f, rec, n, 1500)) stored++;
    if(!stored){ err = "no Star DNS node accepted the record (is the @name taken by someone else?)"; return false; }
    return true;
}

bool StarDns::publish(const Identity& id, const std::string& name, const Addr& relay, const uint8_t relayPub[32], std::string& err){
    PointerRecord r;
    if(!normalizeName(name, r.name)){ err = "an @name is 3 to 32 characters: letters, digits, _ or -"; return false; }
    std::memcpy(r.owner, id.pub, 32);
    r.seq = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    r.expires = wallSeconds() + kPointerMaxLifeSec;
    r.relay = relay;
    std::memcpy(r.relayPub, relayPub, 32);
    uint8_t rec[kMaxPointerLen];
    size_t n = signPointer(id, r, rec, sizeof rec);
    if(!n){ err = "could not build the pointer record"; return false; }
    return publishRecord(rec, n, err);
}

bool StarDns::lookup(const std::string& name, const uint8_t* pinnedPub, PointerRecord& out, std::string& err, std::vector<uint8_t>* raw){
    if(!node_){ err = "Star DNS is not started"; return false; }
    std::string norm;
    if(!normalizeName(name, norm)){ err = "an @name is 3 to 32 characters: letters, digits, _ or -"; return false; }
    uint8_t key[32];
    nameKey(norm, key);
    std::vector<DhtNode::Found> closest;
    std::vector<std::vector<uint8_t>> values;
    node_->iterate(key, true, seeds_, closest, &values, 8000);
    std::vector<PointerRecord> good;
    std::vector<const std::vector<uint8_t>*> goodRaw;
    uint32_t now = wallSeconds();
    for(const auto& v : values){
        PointerRecord r;
        if(!openPointer(v.data(), v.size(), now, r) || r.name != norm) continue;
        if(pinnedPub && crypto_verify32(r.owner, pinnedPub) != 0) continue;
        good.push_back(r);
        goodRaw.push_back(&v);
    }
    if(good.empty()){ err = pinnedPub ? "no current record for @" + norm + " from that friend" : "nobody has published @" + norm; return false; }
    size_t best = 0;
    int bestVotes = -1;
    for(size_t i = 0; i < good.size(); i++){
        int votes = 0;
        for(const PointerRecord& g : good) if(std::memcmp(g.owner, good[i].owner, 32) == 0) votes++;
        if(votes > bestVotes || (votes == bestVotes && good[i].seq > good[best].seq)){ best = i; bestVotes = votes; }
    }
    for(size_t i = 0; i < good.size(); i++){
        if(std::memcmp(good[i].owner, good[best].owner, 32) == 0 && good[i].seq > good[best].seq) best = i;
    }
    out = good[best];
    if(raw) *raw = *goodRaw[best];
    return true;
}

void StarDns::poll(){
    if(node_) node_->drain();
}

StarDns::~StarDns(){
    delete node_;
}

}

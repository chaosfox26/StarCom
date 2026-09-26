#include "rendezvous.h"
#include "noise.h"
#include "../third_party/monocypher/monocypher.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace sc {

namespace {

using Clock = std::chrono::steady_clock;

uint64_t wallMs(){
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void keyedHash(uint8_t out[32], const uint8_t key[32], const char* label, const uint8_t* a, size_t an, const uint8_t* b, size_t bn){
    crypto_blake2b_ctx ctx;
    crypto_blake2b_keyed_init(&ctx, 32, key, 32);
    crypto_blake2b_update(&ctx, (const uint8_t*)label, std::strlen(label));
    if(an) crypto_blake2b_update(&ctx, a, an);
    if(bn) crypto_blake2b_update(&ctx, b, bn);
    crypto_blake2b_final(&ctx, out);
}

void noteKey(uint8_t out[32], const uint8_t shared[32], const uint8_t ownerEdPub[32]){
    keyedHash(out, shared, "starcom-note", ownerEdPub, 32, nullptr, 0);
}

bool isV4Mapped(const uint8_t* a){
    static const uint8_t pre[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    return std::memcmp(a, pre, 12) == 0;
}

std::string addrKey(const Addr& a){
    uint8_t e[19];
    size_t n = encodeAddr(a, e);
    return std::string((const char*)e, n);
}

struct Bucket {
    double tokens;
    double rate;
    double burst;
    Clock::time_point last;
    Bucket(double r = 20, double b = 40) : tokens(b), rate(r), burst(b), last(Clock::now()) {}
    bool take(double n){
        auto now = Clock::now();
        tokens += std::chrono::duration<double>(now - last).count() * rate;
        if(tokens > burst) tokens = burst;
        last = now;
        if(tokens < n) return false;
        tokens -= n;
        return true;
    }
};

struct Box {
    std::vector<uint8_t> blob;
    Clock::time_point expires;
};

struct Pair {
    Addr ends[2];
    Clock::time_point endSeen[2];
    int count = 0;
    Clock::time_point lastSeen;
    Bucket bytes = Bucket(262144, 524288);
};

const size_t kMaxBoxes = 10000;
const size_t kMaxPairs = 2000;
const size_t kMaxSources = 8192;
const auto kBoxTtl = std::chrono::minutes(10);
const auto kPairIdle = std::chrono::seconds(60);

}

uint64_t currentHour(){
    return wallMs() / 3600000ull;
}

bool friendSecret(const Identity& me, const uint8_t friendEdPub[32], uint8_t shared[32]){
    StaticKey mine;
    uint8_t theirs[32];
    if(!staticFromSeed(me.seed, mine)) return false;
    x25519FromEdPub(friendEdPub, theirs);
    uint8_t raw[32];
    crypto_x25519(raw, mine.sec, theirs);
    crypto_wipe(&mine, sizeof mine);
    uint8_t acc = 0;
    for(uint8_t c : raw) acc |= c;
    if(!acc) return false;
    crypto_blake2b(shared, 32, raw, 32);
    crypto_wipe(raw, sizeof raw);
    return true;
}

void mailboxId(const uint8_t shared[32], uint64_t hour, const uint8_t ownerEdPub[32], uint8_t out[32]){
    uint8_t h[8];
    putU64(h, hour);
    keyedHash(out, shared, "starcom-mbox", h, 8, ownerEdPub, 32);
}

void relayToken(const uint8_t shared[32], uint64_t hour, uint8_t out[32]){
    uint8_t h[8];
    putU64(h, hour);
    keyedHash(out, shared, "starcom-relay", h, 8, nullptr, 0);
}

size_t encodeAddr(const Addr& a, uint8_t out[19]){
    std::memset(out, 0, 19);
    if(a.family() == AF_INET){
        const sockaddr_in* s = (const sockaddr_in*)a.raw;
        out[0] = 4;
        std::memcpy(out + 1, &s->sin_port, 2);
        std::memcpy(out + 3, &s->sin_addr, 4);
        return 19;
    }
    if(a.family() == AF_INET6){
        const sockaddr_in6* s = (const sockaddr_in6*)a.raw;
        const uint8_t* b = (const uint8_t*)&s->sin6_addr;
        std::memcpy(out + 1, &s->sin6_port, 2);
        if(isV4Mapped(b)){
            out[0] = 4;
            std::memcpy(out + 3, b + 12, 4);
        } else {
            out[0] = 6;
            std::memcpy(out + 3, b, 16);
        }
        return 19;
    }
    return 0;
}

bool decodeAddr(const uint8_t in[19], Addr& a){
    a = Addr();
    if(in[0] == 4){
        sockaddr_in s;
        std::memset(&s, 0, sizeof s);
        s.sin_family = AF_INET;
        std::memcpy(&s.sin_port, in + 1, 2);
        std::memcpy(&s.sin_addr, in + 3, 4);
        std::memcpy(a.raw, &s, sizeof s);
        a.len = (int)sizeof s;
        return true;
    }
    if(in[0] == 6){
        sockaddr_in6 s;
        std::memset(&s, 0, sizeof s);
        s.sin6_family = AF_INET6;
        std::memcpy(&s.sin6_port, in + 1, 2);
        std::memcpy(&s.sin6_addr, in + 3, 16);
        std::memcpy(a.raw, &s, sizeof s);
        a.len = (int)sizeof s;
        return true;
    }
    return false;
}

size_t sealNote(const uint8_t shared[32], const uint8_t ownerEdPub[32], const uint8_t mailbox[32], const std::vector<Addr>& cands, uint8_t* out, size_t cap){
    uint8_t body[9 + kMaxCandidates * 19];
    putU64(body, wallMs());
    size_t n = 9;
    uint8_t count = 0;
    for(const Addr& a : cands){
        if(count >= kMaxCandidates) break;
        if(encodeAddr(a, body + n)){ n += 19; count++; }
    }
    body[8] = count;
    if(cap < 24 + 16 + n) return 0;
    uint8_t key[32];
    noteKey(key, shared, ownerEdPub);
    if(!sysRandom(out, 24)){ crypto_wipe(key, sizeof key); return 0; }
    crypto_aead_lock(out + 40, out + 24, key, out, mailbox, 32, body, n);
    crypto_wipe(key, sizeof key);
    return 40 + n;
}

bool openNote(const uint8_t shared[32], const uint8_t ownerEdPub[32], const uint8_t mailbox[32], const uint8_t* in, size_t n, uint64_t& stampMs, std::vector<Addr>& cands){
    if(n < 40 + 9 || n > kMaxNote) return false;
    size_t len = n - 40;
    uint8_t body[kMaxNote];
    uint8_t key[32];
    noteKey(key, shared, ownerEdPub);
    bool ok = crypto_aead_unlock(body, in + 24, key, in, mailbox, 32, in + 40, len) == 0;
    crypto_wipe(key, sizeof key);
    if(!ok) return false;
    uint8_t count = body[8];
    if(count > kMaxCandidates || len != 9 + (size_t)count * 19) return false;
    stampMs = getU64(body);
    cands.clear();
    for(uint8_t i = 0; i < count; i++){
        Addr a;
        if(decodeAddr(body + 9 + i * 19, a)) cands.push_back(a);
    }
    return true;
}

Addr mapForSocket(const Addr& a, int socketFamily){
    if(socketFamily != AF_INET6 || a.family() != AF_INET) return a;
    const sockaddr_in* s = (const sockaddr_in*)a.raw;
    sockaddr_in6 m;
    std::memset(&m, 0, sizeof m);
    m.sin6_family = AF_INET6;
    m.sin6_port = s->sin_port;
    uint8_t* b = (uint8_t*)&m.sin6_addr;
    b[10] = 0xff;
    b[11] = 0xff;
    std::memcpy(b + 12, &s->sin_addr, 4);
    Addr o;
    std::memcpy(o.raw, &m, sizeof m);
    o.len = (int)sizeof m;
    return o;
}

bool noteFresh(uint64_t stampMs, uint64_t lastSeenMs){
    uint64_t now = wallMs();
    if(stampMs <= lastSeenMs) return false;
    if(stampMs > now + 300000ull) return false;
    return now < stampMs || now - stampMs <= (uint64_t)kNoteMaxAgeSec * 1000ull;
}

size_t buildPut(const uint8_t mailbox[32], const uint8_t* blob, size_t n, uint8_t* out, size_t cap){
    if(n > kMaxNote || cap < kHeaderLen + 1 + 32 + n) return 0;
    writeHeader(out, PacketType::Rendezvous, Lane::Signal);
    out[kHeaderLen] = kRvPut;
    std::memcpy(out + kHeaderLen + 1, mailbox, 32);
    std::memcpy(out + kHeaderLen + 33, blob, n);
    return kHeaderLen + 33 + n;
}

size_t buildGet(const uint8_t mailbox[32], uint8_t* out, size_t cap){
    if(cap < kGetPadded) return 0;
    std::memset(out, 0, kGetPadded);
    writeHeader(out, PacketType::Rendezvous, Lane::Signal);
    out[kHeaderLen] = kRvGet;
    std::memcpy(out + kHeaderLen + 1, mailbox, 32);
    return kGetPadded;
}

size_t buildBind(const uint8_t token[32], uint8_t* out, size_t cap){
    if(cap < kBindPadded) return 0;
    std::memset(out, 0, kBindPadded);
    writeHeader(out, PacketType::Relay, Lane::Signal);
    out[kHeaderLen] = kRelayBind;
    std::memcpy(out + kHeaderLen + 1, token, 32);
    return kBindPadded;
}

bool parseRendezvous(const uint8_t* pkt, size_t n, uint8_t& op, Addr& observed, const uint8_t*& blob, size_t& blobLen){
    Header h;
    if(!readHeader(pkt, n, h) || h.type != PacketType::Rendezvous || n < kHeaderLen + 20) return false;
    op = pkt[kHeaderLen];
    if(op != kRvPutOk && op != kRvFound && op != kRvMissing) return false;
    if(!decodeAddr(pkt + kHeaderLen + 1, observed)) return false;
    blob = pkt + kHeaderLen + 20;
    blobLen = op == kRvFound ? n - kHeaderLen - 20 : 0;
    return true;
}

bool parseRelayReply(const uint8_t* pkt, size_t n, uint8_t& count){
    Header h;
    if(!readHeader(pkt, n, h) || h.type != PacketType::Relay || n < kHeaderLen + 2 || pkt[kHeaderLen] != kRelayBound) return false;
    count = pkt[kHeaderLen + 1];
    return true;
}

int runRendezvous(uint16_t port){
    UdpSocket s;
    std::string err;
    if(!s.open(familyV6(), port, err) && !s.open(familyV4(), port, err)){
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::printf("Rendezvous and relay on UDP port %u. Nothing is written to disk and nothing is logged.\n", (unsigned)s.localPort());
    std::fflush(stdout);
    std::unordered_map<std::string, Box> boxes;
    std::unordered_map<std::string, Pair> pairs;
    std::unordered_map<std::string, std::string> byEnd;
    std::unordered_map<std::string, Bucket> sources;
    uint8_t in[kMaxPacket + 64], out[kMaxPacket];
    auto lastSweep = Clock::now();
    for(;;){
        Addr from;
        int r = s.recvFrom(in, sizeof in, from, 1000);
        auto now = Clock::now();
        if(now - lastSweep > std::chrono::seconds(5)){
            lastSweep = now;
            for(auto it = boxes.begin(); it != boxes.end();) it = now > it->second.expires ? boxes.erase(it) : std::next(it);
            for(auto it = pairs.begin(); it != pairs.end();){
                if(now - it->second.lastSeen > kPairIdle){
                    for(int i = 0; i < it->second.count; i++) byEnd.erase(addrKey(it->second.ends[i]));
                    it = pairs.erase(it);
                } else ++it;
            }
            if(sources.size() > kMaxSources) sources.clear();
        }
        if(r <= 0) continue;
        size_t n = (size_t)r;
        Header h;
        if(!readHeader(in, n, h)) continue;
        std::string ek = addrKey(from);
        if(ek.empty()) continue;
        if(h.type == PacketType::Hello || h.type == PacketType::Welcome || h.type == PacketType::Data){
            auto e = byEnd.find(ek);
            if(e == byEnd.end()) continue;
            auto p = pairs.find(e->second);
            if(p == pairs.end() || p->second.count < 2) continue;
            if(!p->second.bytes.take((double)n)) continue;
            p->second.lastSeen = now;
            int me = addrKey(p->second.ends[0]) == ek ? 0 : 1;
            p->second.endSeen[me] = now;
            const Addr& to = p->second.ends[1 - me];
            s.sendTo(in, n, to);
            continue;
        }
        std::string ip = ek.substr(0, 1) + ek.substr(3);
        auto src = sources.find(ip);
        if(src == sources.end()) src = sources.emplace(ip, Bucket()).first;
        if(!src->second.take(1)) continue;
        uint8_t op = n > kHeaderLen ? in[kHeaderLen] : 0;
        if(h.type == PacketType::Rendezvous){
            if(n < kHeaderLen + 33) continue;
            std::string box((const char*)in + kHeaderLen + 1, 32);
            writeHeader(out, PacketType::Rendezvous, Lane::Signal);
            encodeAddr(from, out + kHeaderLen + 1);
            size_t len = kHeaderLen + 20;
            if(op == kRvPut){
                size_t bn = n - kHeaderLen - 33;
                if(bn == 0 || bn > kMaxNote) continue;
                auto it = boxes.find(box);
                if(it == boxes.end() && boxes.size() >= kMaxBoxes) continue;
                Box& b = boxes[box];
                b.blob.assign(in + kHeaderLen + 33, in + n);
                b.expires = now + kBoxTtl;
                out[kHeaderLen] = kRvPutOk;
            } else if(op == kRvGet){
                if(n < kGetPadded) continue;
                auto it = boxes.find(box);
                if(it == boxes.end()){
                    out[kHeaderLen] = kRvMissing;
                } else {
                    out[kHeaderLen] = kRvFound;
                    std::memcpy(out + len, it->second.blob.data(), it->second.blob.size());
                    len += it->second.blob.size();
                }
            } else continue;
            s.sendTo(out, len, from);
        } else if(h.type == PacketType::Relay && op == kRelayBind){
            if(n < kBindPadded) continue;
            std::string tok((const char*)in + kHeaderLen + 1, 32);
            auto it = pairs.find(tok);
            if(it == pairs.end()){
                if(pairs.size() >= kMaxPairs) continue;
                it = pairs.emplace(tok, Pair()).first;
            }
            Pair& p = it->second;
            int slot = -1;
            for(int i = 0; i < p.count; i++) if(addrKey(p.ends[i]) == ek) slot = i;
            if(slot < 0){
                auto old = byEnd.find(ek);
                if(old != byEnd.end() && old->second != tok) continue;
                if(p.count < 2) slot = p.count++;
                else {
                    for(int i = 0; i < 2; i++) if(now - p.endSeen[i] > std::chrono::seconds(30)) slot = i;
                    if(slot < 0) continue;
                    byEnd.erase(addrKey(p.ends[slot]));
                }
                p.ends[slot] = from;
                byEnd[ek] = tok;
            }
            p.endSeen[slot] = now;
            p.lastSeen = now;
            writeHeader(out, PacketType::Relay, Lane::Signal);
            out[kHeaderLen] = kRelayBound;
            out[kHeaderLen + 1] = (uint8_t)p.count;
            s.sendTo(out, kHeaderLen + 2, from);
        }
    }
}

}

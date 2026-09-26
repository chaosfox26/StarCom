#include "hub.h"
#include "dht.h"
#include "engine.h"
#include "paths.h"
#include "platform_net.h"
#include "textsafe.h"
#include "wire.h"
#include "../third_party/monocypher/monocypher.h"
#include "../third_party/monocypher/monocypher-ed25519.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>

namespace sc {

namespace {

using Key = std::array<uint8_t, 32>;

enum Kind : uint8_t {
    kOffer = 1, kManReq = 2, kChunk = 3, kRecReq = 4, kRecord = 5, kStem = 6, kFluff = 7,
    kPresence = 8, kJoin = 9, kJoinNo = 10, kKeys = 11, kVoiceStream = 12, kVoiceBye = 13
};
enum RecType : uint8_t { kAdmit = 1, kKick = 2, kBan = 3, kUnban = 4, kLeave = 5 };
enum JoinNo : uint8_t { kNoExpired = 1, kNoUsed = 2, kNoBanned = 3, kNoFull = 4, kNoUnknown = 5 };

const size_t kHdr = 33;
const size_t kChunkBytes = 1000;
const size_t kMaxChunks = 16;
const size_t kMaxRecords = 512;
const size_t kMsgHead = 32 + 8 + 8 + 2 + 4 + 24;
const size_t kMaxHubName = 48;
const size_t kMaxChannelName = 32;
const size_t kKeysPerFrame = 7;
const size_t kMeshTarget = 4;
const size_t kMeshMax = 8;
const uint32_t kEpochMs = 600000;
const uint32_t kHealMs = 120000;
const uint32_t kPresenceEveryMs = 300000;
const uint32_t kPresenceTtlMs = 660000;
const uint32_t kPresenceMinGapMs = 10000;
const uint32_t kSeenMs = 600000;
const uint32_t kWantTimeoutMs = 30000;
const uint32_t kJoinTimeoutMs = 60000;
const uint32_t kOwnWaitMs = 30000;
const uint32_t kAssemblyMs = 30000;
const uint32_t kEpochGraceMs = 300000;
const uint32_t kGenGraceMs = 30000;
const uint32_t kReplyGapMs = 10000;
const uint32_t kReofferMs = 30000;
const uint64_t kFutureMs = 60000;
const uint64_t kWindowMs = 600000;
const size_t kWantsPerMinute = 8;
const uint32_t kMaxRatchetSteps = 24 * 45;
const uint32_t kPalette[] = { 0xE0218A, 0x3D5AFE, 0x9C27B0, 0x2979FF, 0xD81B60, 0x5C6BC0 };
const char* kFileMagicRecords = "SCR1";
const char* kFileMagicKeys = "SCK2";
const uint32_t kKeyHorizon = 1080;
const uint32_t kRekeyAfter = 720;
const uint32_t kRekeyRetryMs = 60000;
const size_t kKeyEntry = 8 + 2 + 4 + 32 + 4 + 32 + 32 + 64;
const size_t kMaxRecordsPerSigner = 32;
const char* kFileMagicInvites = "SCI1";

Key toKey(const uint8_t* p){
    Key k;
    std::memcpy(k.data(), p, 32);
    return k;
}

uint64_t wallMs(){
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

uint32_t hourOf(uint64_t ms){
    return (uint32_t)(ms / 3600000);
}

Key hashOf(const uint8_t* a, size_t an, const uint8_t* b = nullptr, size_t bn = 0, const uint8_t* c = nullptr, size_t cn = 0){
    Key out;
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 32);
    crypto_blake2b_update(&ctx, a, an);
    if(b) crypto_blake2b_update(&ctx, b, bn);
    if(c) crypto_blake2b_update(&ctx, c, cn);
    crypto_blake2b_final(&ctx, out.data());
    return out;
}

uint64_t id8(const uint8_t sig[64]){
    Key h = hashOf(sig, 64);
    return getU64(h.data());
}

std::string hexOf(const uint8_t* p, size_t n){
    static const char* d = "0123456789abcdef";
    std::string s;
    for(size_t i = 0; i < n; i++){ s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

bool textOk(const std::string& s, size_t max){
    if(s.empty() || s.size() > max) return false;
    for(unsigned char c : s) if(c < 0x20 || c == 0x7F) return false;
    return safeText(s) == s;
}

bool atName(const std::string& s){
    std::string n;
    return normalizeName(s, n) && n == s;
}

Key ratchet(const Key& k){
    static const char label[] = "starcom-chan";
    return hashOf((const uint8_t*)label, sizeof label - 1, k.data(), 32);
}

Key commitOf(const Key& k, uint32_t atEpoch, uint32_t e0){
    Key c = k;
    for(uint32_t i = atEpoch; i < e0 + kKeyHorizon; i++){ Key n = ratchet(c); crypto_wipe(c.data(), 32); c = n; }
    static const char label[] = "starcom-commit";
    Key out = hashOf((const uint8_t*)label, sizeof label - 1, c.data(), 32);
    crypto_wipe(c.data(), 32);
    return out;
}


struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v){ b.push_back(v); }
    void u16(uint16_t v){ b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
    void u32(uint32_t v){ for(int i = 3; i >= 0; i--) b.push_back((uint8_t)(v >> (8 * i))); }
    void u64(uint64_t v){ for(int i = 7; i >= 0; i--) b.push_back((uint8_t)(v >> (8 * i))); }
    void raw(const void* p, size_t n){ const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
    void str(const std::string& s){ u8((uint8_t)s.size()); raw(s.data(), s.size()); }
};

struct Reader {
    const uint8_t* p;
    size_t n;
    size_t i = 0;
    bool ok = true;
    Reader(const uint8_t* d, size_t len) : p(d), n(len) {}
    bool need(size_t k){ if(!ok || n - i < k){ ok = false; return false; } return true; }
    uint8_t u8(){ if(!need(1)) return 0; return p[i++]; }
    uint16_t u16(){ if(!need(2)) return 0; uint16_t v = (uint16_t)((p[i] << 8) | p[i + 1]); i += 2; return v; }
    uint32_t u32(){ if(!need(4)) return 0; uint32_t v = 0; for(int k = 0; k < 4; k++) v = (v << 8) | p[i + k]; i += 4; return v; }
    uint64_t u64(){ if(!need(8)) return 0; uint64_t v = 0; for(int k = 0; k < 8; k++) v = (v << 8) | p[i + k]; i += 8; return v; }
    const uint8_t* take(size_t k){ if(!need(k)) return nullptr; const uint8_t* q = p + i; i += k; return q; }
    Key key(){ const uint8_t* q = take(32); Key k{}; if(q) std::memcpy(k.data(), q, 32); return k; }
    std::string str(size_t max){ uint8_t len = u8(); if(len > max){ ok = false; return std::string(); } const uint8_t* q = take(len); return q ? std::string((const char*)q, len) : std::string(); }
    bool end() const { return ok && i == n; }
};

std::vector<uint8_t> keyStatement(const Key& hub, uint64_t ch, uint16_t gen, uint32_t e0, const Key& commit){
    Writer w;
    w.raw("SCKY", 4);
    w.raw(hub.data(), 32);
    w.u64(ch);
    w.u16(gen);
    w.u32(e0);
    w.raw(commit.data(), 32);
    return w.b;
}

struct Channel {
    uint64_t id = 0;
    ChannelKind kind = ChannelKind::Text;
    std::string name;
};

struct Member {
    Key key{};
    HubRole role = HubRole::Member;
    std::string name;
};

struct Manifest {
    Key hub{};
    Key owner{};
    uint8_t salt[16] = {0};
    uint64_t version = 0;
    uint64_t ts = 0;
    std::string name;
    uint32_t colour = 0;
    std::vector<Channel> channels;
    std::vector<Member> members;
    std::vector<Key> bans;
    std::vector<uint64_t> folded;
    std::vector<uint8_t> raw;
    Key hash{};
};

struct Record {
    uint8_t type = 0;
    Key target{};
    uint64_t ts = 0;
    std::string name;
    Key signer{};
    std::vector<uint8_t> raw;
    uint64_t id = 0;
};

struct IssuedKey {
    Key key{};
    uint32_t e0 = 0;
    Key commit{};
    Key issuer{};
    uint8_t sig[64] = {0};
};

struct KeySet {
    uint16_t gen = 0;
    uint32_t epoch = 0;
    std::vector<IssuedKey> keys;
};

struct ChanKeys {
    KeySet cur;
    KeySet prevEpoch;
    uint32_t prevEpochUntil = 0;
    KeySet prevGen;
    uint32_t prevGenUntil = 0;
};

struct Presence {
    uint64_t ts = 0;
    bool online = false;
    uint64_t vc = 0;
    uint32_t seenMs = 0;
    uint32_t acceptedMs = 0;
    std::vector<uint8_t> raw;
};

struct Bucket {
    double tokens = 5;
    uint32_t last = 0;
    bool take(uint32_t now){
        if(last) tokens = std::min(5.0, tokens + (double)(now - last) / 1000.0);
        last = now;
        if(tokens < 1) return false;
        tokens -= 1;
        return true;
    }
};

struct Assembly {
    Key hash{};
    uint16_t count = 0;
    std::vector<std::vector<uint8_t>> parts;
    size_t have = 0;
    uint32_t startMs = 0;
};

struct Link {
    bool outgoing = false;
    uint32_t sinceMs = 0;
};

struct Own {
    std::vector<uint8_t> msg;
    uint64_t id = 0;
    uint32_t deadline = 0;
};

struct Hub {
    Key id{};
    Manifest man;
    std::map<uint64_t, Record> records;
    std::map<Key, Member> eff;
    std::set<Key> bans;
    bool joined = false;
    std::map<uint64_t, ChanKeys> keys;
    std::map<Key, Presence> pres;
    std::map<Key, Link> links;
    std::map<Key, uint32_t> wanted;
    uint32_t epochStart = 0;
    bool diffuser = false;
    std::vector<Key> succ;
    std::map<Key, size_t> inMap;
    std::map<uint64_t, uint32_t> seen;
    std::map<uint64_t, uint32_t> fluffed;
    std::map<uint64_t, std::pair<uint32_t, std::vector<uint8_t>>> embargo;
    std::map<uint64_t, uint32_t> presSeen;
    std::map<Key, Bucket> buckets;
    std::deque<Own> waiting;
    uint32_t lastHealMs = 0;
    uint32_t lastPresMs = 0;
    bool healPending = false;
    uint32_t lastOfferMs = 0;
    std::vector<Key> dropped;
    uint32_t lastRekeyMs = 0;
    uint32_t badIssuerSince = 0;
    bool removed = false;
    std::vector<std::vector<uint8_t>> pendRec;
    std::vector<std::vector<uint8_t>> pendKeys;
    std::vector<std::vector<uint8_t>> pendPres;
};

struct Invite {
    Key tokenHash{};
    Key hub{};
    uint16_t uses = 0;
    uint64_t expiry = 0;
};

struct PeerInfo {
    bool up = false;
    std::string name;
    std::map<Key, Assembly> assembly;
    std::map<uint8_t, std::pair<Key, uint64_t>> inStreams;
    uint8_t nextStream = 1;
    std::map<std::pair<Key, uint64_t>, uint32_t> lastReply;
};

struct PendingJoin {
    Key inviter{};
    uint8_t token[10] = {0};
    std::string inviterName;
    uint32_t startMs = 0;
    bool sent = false;
};

}

struct HubCore::Impl {
    Identity me;
    Key myKey{};
    std::string myName;
    std::string dir;
    HubSendFn send;
    HubWantFn want;
    std::map<Key, Hub> hubs;
    std::map<Key, PeerInfo> peers;
    std::vector<Invite> invites;
    std::vector<PendingJoin> joins;
    std::vector<HubEvent> events;
    std::mt19937_64 rng;
    std::deque<uint32_t> wantTimes;
    Key voiceHub{};
    uint64_t voiceVc = 0;
    std::map<Key, uint8_t> outStreams;
    std::vector<Key> doomed;
    uint32_t now = 0;

    void reap(){
        for(const Key& id : doomed) hubs.erase(id);
        doomed.clear();
    }

    uint32_t nowMs(){
        now = engineNowMs();
        return now;
    }

    void emit(HubEvent::Kind k, const Key& hub, uint64_t channel = 0, const Key* author = nullptr, const std::string& text = std::string(), uint64_t ts = 0){
        HubEvent e;
        e.kind = k;
        std::memcpy(e.hub, hub.data(), 32);
        e.channel = channel;
        if(author) std::memcpy(e.author, author->data(), 32);
        e.text = text;
        e.ts = ts;
        events.push_back(e);
    }

    void error(const Key& hub, const std::string& text){
        emit(HubEvent::Error, hub, 0, nullptr, text);
    }

    bool sendFrame(const Key& peer, uint8_t kind, const Key& hub, const uint8_t* body, size_t n){
        if(!send || kHdr + n > Engine::kMaxPayload) return false;
        auto it = peers.find(peer);
        if(it == peers.end() || !it->second.up) return false;
        std::vector<uint8_t> f(kHdr + n);
        f[0] = kind;
        std::memcpy(f.data() + 1, hub.data(), 32);
        if(n) std::memcpy(f.data() + kHdr, body, n);
        return send(peer.data(), f.data(), f.size());
    }

    bool mayReply(const Key& peer, const Key& hub, uint8_t kind, uint64_t about){
        auto& pi = peers[peer];
        auto k = std::make_pair(hub, ((uint64_t)kind << 56) ^ (about & 0x00FFFFFFFFFFFFFFull));
        auto it = pi.lastReply.find(k);
        if(it != pi.lastReply.end() && now - it->second < kReplyGapMs) return false;
        pi.lastReply[k] = now;
        return true;
    }

    bool wantPeer(const Key& peer, const std::string& name, const std::string& hint){
        while(!wantTimes.empty() && now - wantTimes.front() >= 60000) wantTimes.pop_front();
        if(wantTimes.size() >= kWantsPerMinute || !want || name.empty()) return false;
        wantTimes.push_back(now);
        want(peer.data(), name, hint);
        return true;
    }

    std::string hubPath(const Hub& h, const char* ext){
        return dir + "/" + hexOf(h.id.data(), 32) + ext;
    }

    bool writeFile(const std::string& path, const std::vector<uint8_t>& data, bool secret){
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if(!f) return false;
            f.write((const char*)data.data(), (std::streamsize)data.size());
            if(!f) return false;
        }
        if(secret) std::filesystem::permissions(tmp, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, ec);
        return replaceFile(tmp, path);
    }

    bool readFile(const std::string& path, std::vector<uint8_t>& out){
        std::ifstream f(path, std::ios::binary);
        if(!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }

    std::vector<uint8_t> encodeManifestBody(const Manifest& m){
        Writer w;
        w.raw("SCHM", 4);
        w.raw(m.hub.data(), 32);
        w.raw(m.owner.data(), 32);
        w.raw(m.salt, 16);
        w.u64(m.version);
        w.u64(m.ts);
        w.str(m.name);
        w.u32(m.colour);
        w.u8((uint8_t)m.channels.size());
        for(const Channel& c : m.channels){ w.u64(c.id); w.u8((uint8_t)c.kind); w.str(c.name); }
        w.u8((uint8_t)m.members.size());
        for(const Member& mb : m.members){ w.raw(mb.key.data(), 32); w.u8((uint8_t)mb.role); w.str(mb.name); }
        w.u8((uint8_t)m.bans.size());
        for(const Key& b : m.bans) w.raw(b.data(), 32);
        w.u8((uint8_t)m.folded.size());
        for(uint64_t id : m.folded) w.u64(id);
        return w.b;
    }

    void signManifest(Manifest& m){
        m.raw = encodeManifestBody(m);
        uint8_t sig[64];
        crypto_ed25519_sign(sig, me.secret, m.raw.data(), m.raw.size());
        m.raw.insert(m.raw.end(), sig, sig + 64);
        m.hash = hashOf(m.raw.data(), m.raw.size());
    }

    static Key hubIdFor(const Key& owner, const uint8_t salt[16]){
        static const char label[] = "starcom-hub";
        return hashOf((const uint8_t*)label, sizeof label - 1, owner.data(), 32, salt, 16);
    }

    bool decodeManifest(const std::vector<uint8_t>& raw, Manifest& m){
        if(raw.size() < 64 + 4 || raw.size() > kChunkBytes * kMaxChunks) return false;
        size_t body = raw.size() - 64;
        Reader r(raw.data(), body);
        const uint8_t* magic = r.take(4);
        if(!magic || std::memcmp(magic, "SCHM", 4) != 0) return false;
        m.hub = r.key();
        m.owner = r.key();
        const uint8_t* salt = r.take(16);
        if(!salt) return false;
        std::memcpy(m.salt, salt, 16);
        m.version = r.u64();
        m.ts = r.u64();
        m.name = r.str(kMaxHubName);
        m.colour = r.u32();
        uint8_t nc = r.u8();
        if(nc > kMaxHubChannels) return false;
        std::set<uint64_t> chIds;
        for(uint8_t i = 0; i < nc && r.ok; i++){
            Channel c;
            c.id = r.u64();
            uint8_t k = r.u8();
            c.name = r.str(kMaxChannelName);
            if(c.id == 0 || (k != (uint8_t)ChannelKind::Text && k != (uint8_t)ChannelKind::Voice) || !textOk(c.name, kMaxChannelName) || !chIds.insert(c.id).second) return false;
            c.kind = (ChannelKind)k;
            m.channels.push_back(c);
        }
        uint8_t nm = r.u8();
        if(nm == 0 || nm > kMaxHubMembers) return false;
        std::set<Key> seenKeys;
        int owners = 0;
        for(uint8_t i = 0; i < nm && r.ok; i++){
            Member mb;
            mb.key = r.key();
            uint8_t role = r.u8();
            mb.name = r.str(32);
            if(role > (uint8_t)HubRole::Owner || !atName(mb.name) || !seenKeys.insert(mb.key).second) return false;
            mb.role = (HubRole)role;
            if(mb.role == HubRole::Owner){
                if(mb.key != m.owner) return false;
                owners++;
            }
            m.members.push_back(mb);
        }
        uint8_t nb = r.u8();
        for(uint8_t i = 0; i < nb && r.ok; i++) m.bans.push_back(r.key());
        uint8_t nf = r.u8();
        for(uint8_t i = 0; i < nf && r.ok; i++) m.folded.push_back(r.u64());
        if(!r.end() || owners != 1 || !textOk(m.name, kMaxHubName) || m.version == 0) return false;
        if(hubIdFor(m.owner, m.salt) != m.hub) return false;
        if(crypto_ed25519_check(raw.data() + body, m.owner.data(), raw.data(), body) != 0) return false;
        m.raw = raw;
        m.hash = hashOf(raw.data(), raw.size());
        return true;
    }

    std::vector<uint8_t> signRecord(const Key& hub, uint8_t type, const Key& target, const std::string& name, uint64_t minTs = 0){
        Writer w;
        w.u8(type);
        w.raw(target.data(), 32);
        w.u64(std::max(wallMs(), minTs));
        w.str(name);
        w.raw(myKey.data(), 32);
        std::vector<uint8_t> msg(hub.begin(), hub.end());
        msg.insert(msg.end(), w.b.begin(), w.b.end());
        uint8_t sig[64];
        crypto_ed25519_sign(sig, me.secret, msg.data(), msg.size());
        w.raw(sig, 64);
        return w.b;
    }

    bool decodeRecord(const Key& hub, const uint8_t* p, size_t n, Record& rec){
        if(n < 64) return false;
        Reader r(p, n - 64);
        rec.type = r.u8();
        rec.target = r.key();
        rec.ts = r.u64();
        rec.name = r.str(32);
        rec.signer = r.key();
        if(!r.end() || rec.type < kAdmit || rec.type > kLeave) return false;
        if(rec.type == kAdmit ? !atName(rec.name) : !rec.name.empty()) return false;
        std::vector<uint8_t> msg(hub.begin(), hub.end());
        msg.insert(msg.end(), p, p + n - 64);
        if(crypto_ed25519_check(p + n - 64, rec.signer.data(), msg.data(), msg.size()) != 0) return false;
        rec.raw.assign(p, p + n);
        rec.id = id8(p + n - 64);
        return true;
    }

    HubRole roleIn(const Hub& h, const Key& k, bool& member){
        auto it = h.eff.find(k);
        member = it != h.eff.end();
        return member ? it->second.role : HubRole::Member;
    }

    void recompute(Hub& h){
        bool was = h.joined;
        h.eff.clear();
        h.bans.clear();
        for(const Member& m : h.man.members) h.eff[m.key] = m;
        for(const Key& b : h.man.bans) h.bans.insert(b);
        std::vector<const Record*> order;
        for(auto& kv : h.records) if(kv.second.ts > h.man.ts) order.push_back(&kv.second);
        std::sort(order.begin(), order.end(), [](const Record* a, const Record* b){ return a->ts != b->ts ? a->ts < b->ts : a->id < b->id; });
        for(const Record* r : order){
            bool sm = false, tm = false;
            HubRole sr = roleIn(h, r->signer, sm);
            HubRole tr = roleIn(h, r->target, tm);
            bool mod = sm && sr >= HubRole::Admin;
            switch(r->type){
            case kAdmit:
                if(mod && !tm && !h.bans.count(r->target) && h.eff.size() < kMaxHubMembers){
                    Member m;
                    m.key = r->target;
                    m.name = r->name;
                    h.eff[r->target] = m;
                }
                break;
            case kKick:
                if(mod && tm && tr < sr) h.eff.erase(r->target);
                break;
            case kBan:
                if(mod && (!tm || tr < sr) && r->target != h.man.owner){
                    h.eff.erase(r->target);
                    h.bans.insert(r->target);
                }
                break;
            case kUnban:
                if(sm && sr == HubRole::Owner) h.bans.erase(r->target);
                break;
            case kLeave:
                if(r->signer == r->target && tm && tr != HubRole::Owner) h.eff.erase(r->target);
                break;
            }
        }
        h.joined = h.eff.count(myKey) != 0;
        for(auto it = h.links.begin(); it != h.links.end();){
            if(!h.eff.count(it->first)){
                h.dropped.push_back(it->first);
                it = h.links.erase(it);
            } else ++it;
        }
        if(!was && h.joined) emit(HubEvent::Joined, h.id, 0, nullptr, "You are in " + h.man.name + ".");
    }

    Key recordsDigest(const Hub& h){
        crypto_blake2b_ctx ctx;
        crypto_blake2b_init(&ctx, 32);
        for(auto& kv : h.records){
            uint8_t b[8];
            putU64(b, kv.first);
            crypto_blake2b_update(&ctx, b, 8);
        }
        Key out;
        crypto_blake2b_final(&ctx, out.data());
        return out;
    }

    uint16_t generation(const Hub& h){
        uint16_t g = 0;
        for(auto& kv : h.keys) g = std::max(g, kv.second.cur.gen);
        return g;
    }

    void saveManifest(Hub& h){
        writeFile(hubPath(h, ".manifest"), h.man.raw, false);
    }

    void saveRecords(Hub& h){
        Writer w;
        w.raw(kFileMagicRecords, 4);
        w.u16((uint16_t)h.records.size());
        for(auto& kv : h.records){ w.u16((uint16_t)kv.second.raw.size()); w.raw(kv.second.raw.data(), kv.second.raw.size()); }
        writeFile(hubPath(h, ".records"), w.b, false);
    }

    void saveKeys(Hub& h){
        Writer w;
        w.raw(kFileMagicKeys, 4);
        w.u8((uint8_t)h.keys.size());
        for(auto& kv : h.keys){
            w.u64(kv.first);
            w.u16(kv.second.cur.gen);
            w.u32(kv.second.cur.epoch);
            w.u8((uint8_t)kv.second.cur.keys.size());
            for(const IssuedKey& k : kv.second.cur.keys){
                w.raw(k.key.data(), 32);
                w.u32(k.e0);
                w.raw(k.commit.data(), 32);
                w.raw(k.issuer.data(), 32);
                w.raw(k.sig, 64);
            }
        }
        writeFile(hubPath(h, ".keys"), w.b, true);
        crypto_wipe(w.b.data(), w.b.size());
    }

    void saveInvites(){
        if(invites.empty()){
            std::error_code ec;
            std::filesystem::remove(dir + "/invites", ec);
            return;
        }
        Writer w;
        w.raw(kFileMagicInvites, 4);
        w.u16((uint16_t)invites.size());
        for(const Invite& iv : invites){ w.raw(iv.tokenHash.data(), 32); w.raw(iv.hub.data(), 32); w.u16(iv.uses); w.u64(iv.expiry); }
        writeFile(dir + "/invites", w.b, true);
    }

    void saveAll(Hub& h){
        saveManifest(h);
        saveRecords(h);
        saveKeys(h);
    }

    void removeFiles(Hub& h){
        std::error_code ec;
        for(const char* ext : { ".manifest", ".records", ".keys" }) std::filesystem::remove(hubPath(h, ext), ec);
    }

    void wipeKeys(Hub& h){
        for(auto& kv : h.keys){
            for(KeySet* s : { &kv.second.cur, &kv.second.prevEpoch, &kv.second.prevGen }) for(IssuedKey& k : s->keys) crypto_wipe(k.key.data(), 32);
        }
        h.keys.clear();
    }

    void loadAll(){
        std::error_code ec;
        if(!std::filesystem::is_directory(dir, ec)) return;
        for(auto& ent : std::filesystem::directory_iterator(dir, ec)){
            std::string path = ent.path().string();
            if(path.size() < 9 || path.compare(path.size() - 9, 9, ".manifest") != 0) continue;
            std::vector<uint8_t> raw;
            Manifest m;
            if(!readFile(path, raw) || !decodeManifest(raw, m)) continue;
            Hub& h = hubs[m.hub];
            h.id = m.hub;
            h.man = m;
            std::vector<uint8_t> rr;
            if(readFile(hubPath(h, ".records"), rr) && rr.size() >= 6 && std::memcmp(rr.data(), kFileMagicRecords, 4) == 0){
                Reader r(rr.data() + 4, rr.size() - 4);
                uint16_t cnt = r.u16();
                for(uint16_t i = 0; i < cnt && r.ok; i++){
                    uint16_t len = r.u16();
                    const uint8_t* q = r.take(len);
                    Record rec;
                    if(q && decodeRecord(h.id, q, len, rec)) h.records[rec.id] = rec;
                }
            }
            std::vector<uint8_t> kr;
            if(readFile(hubPath(h, ".keys"), kr) && kr.size() >= 5 && std::memcmp(kr.data(), kFileMagicKeys, 4) == 0){
                Reader r(kr.data() + 4, kr.size() - 4);
                uint8_t cnt = r.u8();
                for(uint8_t i = 0; i < cnt && r.ok; i++){
                    uint64_t ch = r.u64();
                    KeySet s;
                    s.gen = r.u16();
                    s.epoch = r.u32();
                    uint8_t nk = r.u8();
                    for(uint8_t j = 0; j < nk && j < 2 && r.ok; j++){
                        IssuedKey ik;
                        ik.key = r.key();
                        ik.e0 = r.u32();
                        ik.commit = r.key();
                        ik.issuer = r.key();
                        const uint8_t* sg = r.take(64);
                        if(sg) std::memcpy(ik.sig, sg, 64);
                        s.keys.push_back(ik);
                    }
                    if(r.ok) h.keys[ch].cur = s;
                }
                crypto_wipe(kr.data(), kr.size());
            }
            recompute(h);
        }
        std::vector<uint8_t> ir;
        if(readFile(dir + "/invites", ir) && ir.size() >= 6 && std::memcmp(ir.data(), kFileMagicInvites, 4) == 0){
            Reader r(ir.data() + 4, ir.size() - 4);
            uint16_t cnt = r.u16();
            for(uint16_t i = 0; i < cnt && r.ok; i++){
                Invite iv;
                iv.tokenHash = r.key();
                iv.hub = r.key();
                iv.uses = r.u16();
                iv.expiry = r.u64();
                if(r.ok) invites.push_back(iv);
            }
        }
        events.clear();
    }

    void advanceEpochs(Hub& h){
        uint32_t e = hourOf(wallMs());
        bool changed = false;
        for(auto& kv : h.keys){
            ChanKeys& ck = kv.second;
            if(ck.cur.keys.empty() || ck.cur.epoch >= e) continue;
            uint32_t steps = e - ck.cur.epoch;
            if(steps > kMaxRatchetSteps) steps = kMaxRatchetSteps;
            ck.prevEpoch = ck.cur;
            ck.prevEpochUntil = now + kEpochGraceMs;
            for(IssuedKey& k : ck.cur.keys) for(uint32_t i = 0; i < steps; i++){ Key n = ratchet(k.key); crypto_wipe(k.key.data(), 32); k.key = n; }
            ck.cur.epoch = e;
            changed = true;
        }
        if(changed) saveKeys(h);
    }

    bool sendKey(Hub& h, uint64_t ch, uint16_t& gen, uint32_t& epoch, Key& key){
        auto it = h.keys.find(ch);
        if(it == h.keys.end() || it->second.cur.keys.empty()) return false;
        const KeySet& s = it->second.cur;
        if(s.epoch > s.keys[0].e0 + kKeyHorizon) return false;
        const Key* best = &s.keys[0].key;
        for(const IssuedKey& k : s.keys) if(hashOf(k.key.data(), 32) < hashOf(best->data(), 32)) best = &k.key;
        gen = s.gen;
        epoch = s.epoch;
        key = *best;
        return true;
    }

    bool newKeys(Hub& h){
        if(generation(h) == 0xFFFF){
            error(h.id, "This hub has used up its key generations; its owner needs to make a new hub.");
            return false;
        }
        uint16_t g = (uint16_t)(generation(h) + 1);
        uint32_t e = hourOf(wallMs());
        h.lastRekeyMs = now;
        std::map<uint64_t, ChanKeys> next;
        for(const Channel& c : h.man.channels){
            ChanKeys ck;
            auto old = h.keys.find(c.id);
            if(old != h.keys.end() && !old->second.cur.keys.empty()){
                ck.prevGen = old->second.cur;
                ck.prevGenUntil = now + kGenGraceMs;
            }
            IssuedKey ik;
            sysRandom(ik.key.data(), 32);
            ik.e0 = e;
            ik.commit = commitOf(ik.key, e, e);
            ik.issuer = myKey;
            std::vector<uint8_t> st = keyStatement(h.id, c.id, g, e, ik.commit);
            crypto_ed25519_sign(ik.sig, me.secret, st.data(), st.size());
            ck.cur.gen = g;
            ck.cur.epoch = e;
            ck.cur.keys.push_back(ik);
            next[c.id] = ck;
        }
        wipeKeys(h);
        h.keys = next;
        saveKeys(h);
        return true;
    }

    void rekeyAndShare(Hub& h){
        if(!newKeys(h)) return;
        for(auto& kv : h.eff) if(kv.first != myKey && peers[kv.first].up) sendKeys(h, kv.first);
        offerAll(h);
    }

    void sendKeys(Hub& h, const Key& peer){
        std::vector<std::vector<uint8_t>> entries;
        for(auto& kv : h.keys){
            for(const IssuedKey& k : kv.second.cur.keys){
                Writer w;
                w.u64(kv.first);
                w.u16(kv.second.cur.gen);
                w.u32(kv.second.cur.epoch);
                w.raw(k.key.data(), 32);
                w.u32(k.e0);
                w.raw(k.commit.data(), 32);
                w.raw(k.issuer.data(), 32);
                w.raw(k.sig, 64);
                entries.push_back(w.b);
            }
        }
        for(size_t i = 0; i < entries.size(); i += kKeysPerFrame){
            Writer w;
            size_t cnt = std::min(kKeysPerFrame, entries.size() - i);
            w.u8((uint8_t)cnt);
            for(size_t j = 0; j < cnt; j++) w.raw(entries[i + j].data(), entries[i + j].size());
            sendFrame(peer, kKeys, h.id, w.b.data(), w.b.size());
            crypto_wipe(w.b.data(), w.b.size());
        }
        for(auto& e : entries) crypto_wipe(e.data(), e.size());
    }

    void addKeys(Hub& h, const uint8_t* p, size_t n){
        advanceEpochs(h);
        Reader r(p, n);
        uint8_t cnt = r.u8();
        bool changed = false;
        uint32_t nowEpoch = hourOf(wallMs());
        for(uint8_t i = 0; i < cnt && r.ok; i++){
            uint64_t ch = r.u64();
            uint16_t gen = r.u16();
            uint32_t epoch = r.u32();
            IssuedKey ik;
            ik.key = r.key();
            ik.e0 = r.u32();
            ik.commit = r.key();
            ik.issuer = r.key();
            const uint8_t* sg = r.take(64);
            if(!r.ok || !sg) break;
            std::memcpy(ik.sig, sg, 64);
            bool known = false;
            for(const Channel& c : h.man.channels) if(c.id == ch) known = true;
            bool im = false;
            if(!known || gen == 0 || roleIn(h, ik.issuer, im) < HubRole::Admin || !im) continue;
            if(epoch < ik.e0 || epoch > ik.e0 + kKeyHorizon || nowEpoch > ik.e0 + kKeyHorizon) continue;
            std::vector<uint8_t> st = keyStatement(h.id, ch, gen, ik.e0, ik.commit);
            if(crypto_ed25519_check(ik.sig, ik.issuer.data(), st.data(), st.size()) != 0) continue;
            if(commitOf(ik.key, epoch, ik.e0) != ik.commit) continue;
            if(epoch < nowEpoch){
                uint32_t steps = std::min(nowEpoch - epoch, kMaxRatchetSteps);
                for(uint32_t s2 = 0; s2 < steps; s2++){ Key nk = ratchet(ik.key); crypto_wipe(ik.key.data(), 32); ik.key = nk; }
                epoch = nowEpoch;
            }
            ChanKeys& ck = h.keys[ch];
            if(ck.cur.keys.empty() || gen > ck.cur.gen){
                if(!ck.cur.keys.empty()){
                    ck.prevGen = ck.cur;
                    ck.prevGenUntil = now + kGenGraceMs;
                }
                ck.cur = KeySet();
                ck.cur.gen = gen;
                ck.cur.epoch = epoch;
                ck.cur.keys.push_back(ik);
                changed = true;
            } else if(gen == ck.cur.gen && epoch == ck.cur.epoch && ck.cur.keys.size() < 2){
                bool dup = false;
                for(const IssuedKey& k : ck.cur.keys) if(k.key == ik.key) dup = true;
                if(!dup){
                    ck.cur.keys.push_back(ik);
                    changed = true;
                }
            }
            crypto_wipe(ik.key.data(), 32);
        }
        if(changed){
            saveKeys(h);
            offerAll(h);
        }
    }

    bool openWith(const KeySet& s, uint16_t gen, uint32_t epoch, const uint8_t* ad, size_t adn, const uint8_t* nonce, const uint8_t* ct, size_t ctn, const uint8_t* tag, std::string& out){
        if(s.keys.empty() || s.gen != gen || epoch < s.epoch || epoch - s.epoch > 2) return false;
        for(const IssuedKey& base : s.keys){
            Key k = base.key;
            for(uint32_t i = s.epoch; i < epoch; i++) k = ratchet(k);
            std::string plain(ctn, '\0');
            bool ok = crypto_aead_unlock((uint8_t*)&plain[0], tag, k.data(), nonce, ad, adn, ct, ctn) == 0;
            crypto_wipe(k.data(), 32);
            if(ok){ out.swap(plain); return true; }
        }
        return false;
    }

    std::vector<uint8_t> sealMessage(Hub& h, uint64_t ch, const std::string& text, bool& ok){
        ok = false;
        uint16_t gen;
        uint32_t epoch;
        Key key;
        if(!sendKey(h, ch, gen, epoch, key)) return {};
        Writer w;
        w.raw(myKey.data(), 32);
        w.u64(wallMs());
        w.u64(ch);
        w.u16(gen);
        w.u32(epoch);
        std::vector<uint8_t> ad(h.id.begin(), h.id.end());
        ad.insert(ad.end(), w.b.begin(), w.b.end());
        uint8_t nonce[24];
        sysRandom(nonce, 24);
        w.raw(nonce, 24);
        std::vector<uint8_t> ct(text.size());
        uint8_t tag[16];
        crypto_aead_lock(ct.data(), tag, key.data(), nonce, ad.data(), ad.size(), (const uint8_t*)text.data(), text.size());
        crypto_wipe(key.data(), 32);
        w.raw(ct.data(), ct.size());
        w.raw(tag, 16);
        std::vector<uint8_t> msg(h.id.begin(), h.id.end());
        msg.insert(msg.end(), w.b.begin(), w.b.end());
        uint8_t sig[64];
        crypto_ed25519_sign(sig, me.secret, msg.data(), msg.size());
        w.raw(sig, 64);
        ok = true;
        return w.b;
    }

    bool openMessage(Hub& h, const Key& from, const uint8_t* p, size_t n, uint64_t& id, Key& author, uint64_t& ch, uint64_t& ts, std::string& text){
        if(n < kMsgHead + 16 + 64 || n > kMsgHead + 16 + 64 + kMaxHubText) return false;
        Reader r(p, kMsgHead);
        author = r.key();
        ts = r.u64();
        ch = r.u64();
        uint16_t gen = r.u16();
        uint32_t epoch = r.u32();
        const uint8_t* nonce = r.take(24);
        if(!r.end()) return false;
        uint64_t wall = wallMs();
        if(ts + kWindowMs < wall || ts > wall + kFutureMs) return false;
        if(!h.eff.count(author)) return false;
        bool isText = false;
        for(const Channel& c : h.man.channels) if(c.id == ch && c.kind == ChannelKind::Text) isText = true;
        if(!isText) return false;
        const uint8_t* sig = p + n - 64;
        std::vector<uint8_t> msg(h.id.begin(), h.id.end());
        msg.insert(msg.end(), p, p + n - 64);
        if(crypto_ed25519_check(sig, author.data(), msg.data(), msg.size()) != 0) return false;
        id = id8(sig);
        std::vector<uint8_t> ad(h.id.begin(), h.id.end());
        ad.insert(ad.end(), p, p + kMsgHead - 24);
        const uint8_t* ct = p + kMsgHead;
        size_t ctn = n - kMsgHead - 16 - 64;
        const uint8_t* tag = ct + ctn;
        auto kit = h.keys.find(ch);
        if(kit == h.keys.end()) return false;
        ChanKeys& ck = kit->second;
        if(openWith(ck.cur, gen, epoch, ad.data(), ad.size(), nonce, ct, ctn, tag, text)) return true;
        if(now < ck.prevEpochUntil && openWith(ck.prevEpoch, gen, epoch, ad.data(), ad.size(), nonce, ct, ctn, tag, text)) return true;
        if(now < ck.prevGenUntil && openWith(ck.prevGen, gen, epoch, ad.data(), ad.size(), nonce, ct, ctn, tag, text)) return true;
        if(gen > ck.cur.gen) offer(h, from);
        return false;
    }

    void offer(Hub& h, const Key& peer){
        Writer w;
        w.u64(h.man.version);
        w.raw(h.man.hash.data(), 32);
        Key d = recordsDigest(h);
        w.raw(d.data(), 32);
        w.u16(generation(h));
        sendFrame(peer, kOffer, h.id, w.b.data(), w.b.size());
    }

    void offerAll(Hub& h){
        for(auto& kv : h.links) offer(h, kv.first);
    }

    void pickSuccessors(Hub& h){
        h.succ.clear();
        h.inMap.clear();
        std::vector<Key> cand;
        for(auto& kv : h.links) cand.push_back(kv.first);
        std::shuffle(cand.begin(), cand.end(), rng);
        for(size_t i = 0; i < cand.size() && i < 2; i++) h.succ.push_back(cand[i]);
        if(h.succ.size() == 1) h.succ.push_back(h.succ[0]);
    }

    void newEpoch(Hub& h){
        h.epochStart = now;
        h.diffuser = std::uniform_real_distribution<double>(0, 1)(rng) < 0.2;
        pickSuccessors(h);
    }

    void linkUp(Hub& h, const Key& peer, bool outgoing){
        if(h.links.count(peer) || !h.eff.count(peer)) return;
        Link l;
        l.outgoing = outgoing;
        l.sinceMs = now;
        h.links[peer] = l;
        h.wanted.erase(peer);
        offer(h, peer);
        for(auto& kv : h.pres) if(kv.first != peer && !kv.second.raw.empty() && now - kv.second.seenMs <= kPresenceTtlMs) sendFrame(peer, kPresence, h.id, kv.second.raw.data(), kv.second.raw.size());
        if(h.succ.empty()) newEpoch(h);
        if(h.healPending && h.links.size() > kMeshTarget){
            std::vector<Key> old;
            for(auto& kv : h.links) if(kv.second.outgoing && kv.first != peer) old.push_back(kv.first);
            if(!old.empty()){
                Key drop = old[std::uniform_int_distribution<size_t>(0, old.size() - 1)(rng)];
                linkDown(h, drop);
            }
            h.healPending = false;
        }
        while(!h.waiting.empty()){
            Own o = h.waiting.front();
            h.waiting.pop_front();
            stemOwn(h, o.msg, o.id);
        }
    }

    void linkDown(Hub& h, const Key& peer){
        if(!h.links.erase(peer)) return;
        h.inMap.erase(peer);
        if(std::find(h.succ.begin(), h.succ.end(), peer) != h.succ.end()) pickSuccessors(h);
    }

    void fluff(Hub& h, const std::vector<uint8_t>& msg, uint64_t id, const Key* except){
        h.fluffed[id] = now;
        h.embargo.erase(id);
        for(auto& kv : h.links) if(!except || kv.first != *except) sendFrame(kv.first, kFluff, h.id, msg.data(), msg.size());
    }

    void stemOwn(Hub& h, const std::vector<uint8_t>& msg, uint64_t id){
        if(h.succ.empty()) pickSuccessors(h);
        if(h.succ.empty()){
            Own o;
            o.msg = msg;
            o.id = id;
            o.deadline = now + kOwnWaitMs;
            h.waiting.push_back(o);
            return;
        }
        sendFrame(h.succ[0], kStem, h.id, msg.data(), msg.size());
        h.embargo[id] = std::make_pair(now + 1000 + (uint32_t)std::uniform_int_distribution<int>(0, 4000)(rng), msg);
    }

    void deliver(Hub& h, uint64_t id, const Key& author, uint64_t ch, const std::string& text, uint64_t ts){
        if(h.seen.count(id)) return;
        h.seen[id] = now;
        emit(HubEvent::Message, h.id, ch, &author, text, ts);
    }

    bool acceptMessage(Hub& h, const Key& from, const uint8_t* p, size_t n, uint64_t& id){
        Key author;
        uint64_t ch, ts;
        std::string text;
        if(!openMessage(h, from, p, n, id, author, ch, ts, text)) return false;
        if(h.seen.count(id) || h.fluffed.count(id)){
            return true;
        }
        if(!h.buckets[author].take(now)) return false;
        deliver(h, id, author, ch, text, ts);
        return true;
    }

    void onStem(Hub& h, const Key& from, const uint8_t* p, size_t n){
        uint64_t id = 0;
        bool dup = false;
        {
            if(n >= 64) dup = h.seen.count(id8(p + n - 64)) || h.fluffed.count(id8(p + n - 64));
        }
        if(!acceptMessage(h, from, p, n, id)) return;
        if(dup) return;
        std::vector<uint8_t> msg(p, p + n);
        if(h.diffuser || h.succ.empty()){
            fluff(h, msg, id, nullptr);
            return;
        }
        auto it = h.inMap.find(from);
        size_t slot;
        if(it == h.inMap.end()){
            slot = std::uniform_int_distribution<size_t>(0, h.succ.size() - 1)(rng);
            h.inMap[from] = slot;
        } else slot = it->second;
        Key next = h.succ[slot];
        if(next == from) next = h.succ[1 - (slot & 1)];
        if(next == from){
            fluff(h, msg, id, nullptr);
            return;
        }
        sendFrame(next, kStem, h.id, msg.data(), msg.size());
        h.embargo[id] = std::make_pair(now + 1000 + (uint32_t)std::uniform_int_distribution<int>(0, 4000)(rng), msg);
    }

    void onFluff(Hub& h, const Key& from, const uint8_t* p, size_t n){
        if(n >= 64 && h.fluffed.count(id8(p + n - 64))) return;
        uint64_t id = 0;
        if(!acceptMessage(h, from, p, n, id)) return;
        std::vector<uint8_t> msg(p, p + n);
        fluff(h, msg, id, &from);
    }

    std::vector<uint8_t> signPresence(Hub& h, bool online, uint64_t vc){
        Writer w;
        w.raw(myKey.data(), 32);
        w.u64(wallMs());
        w.u8(online ? 1 : 0);
        w.u64(vc);
        std::vector<uint8_t> msg(h.id.begin(), h.id.end());
        msg.insert(msg.end(), w.b.begin(), w.b.end());
        uint8_t sig[64];
        crypto_ed25519_sign(sig, me.secret, msg.data(), msg.size());
        w.raw(sig, 64);
        return w.b;
    }

    void sendPresence(Hub& h, bool online){
        uint64_t vc = (voiceVc && voiceHub == h.id) ? voiceVc : 0;
        std::vector<uint8_t> p = signPresence(h, online, vc);
        h.presSeen[id8(p.data() + p.size() - 64)] = now;
        Presence& me_ = h.pres[myKey];
        me_.ts = wallMs();
        me_.online = online;
        me_.vc = vc;
        me_.seenMs = now;
        me_.raw = p;
        h.lastPresMs = now;
        for(auto& kv : h.links) sendFrame(kv.first, kPresence, h.id, p.data(), p.size());
    }

    void onPresence(Hub& h, const Key& from, const uint8_t* p, size_t n){
        if(n != 32 + 8 + 1 + 8 + 64) return;
        uint64_t pid = id8(p + n - 64);
        if(h.presSeen.count(pid)) return;
        Reader r(p, n - 64);
        Key member = r.key();
        uint64_t ts = r.u64();
        uint8_t state = r.u8();
        uint64_t vc = r.u64();
        if(!r.end() || state > 1 || !h.eff.count(member) || member == myKey) return;
        uint64_t wall = wallMs();
        if(ts > wall + kFutureMs || ts + kWindowMs < wall) return;
        std::vector<uint8_t> msg(h.id.begin(), h.id.end());
        msg.insert(msg.end(), p, p + n - 64);
        if(crypto_ed25519_check(p + n - 64, member.data(), msg.data(), msg.size()) != 0) return;
        Presence& pr = h.pres[member];
        if(ts <= pr.ts) return;
        if(pr.acceptedMs && now - pr.acceptedMs < kPresenceMinGapMs && state == (pr.online ? 1 : 0) && vc == pr.vc) return;
        h.presSeen[pid] = now;
        bool changedOnline = pr.online != (state == 1);
        bool changedVc = pr.vc != vc;
        uint64_t oldVc = pr.vc;
        pr.ts = ts;
        pr.online = state == 1;
        pr.vc = pr.online ? vc : 0;
        pr.seenMs = now;
        pr.acceptedMs = now;
        pr.raw.assign(p, p + n);
        for(auto& kv : h.links) if(kv.first != from) sendFrame(kv.first, kPresence, h.id, p, n);
        if(changedOnline) emit(HubEvent::Presence, h.id, 0, &member);
        if(changedVc){
            if(oldVc) emit(HubEvent::VoiceMembers, h.id, oldVc);
            if(pr.vc) emit(HubEvent::VoiceMembers, h.id, pr.vc);
        }
    }

    bool storeRecord(Hub& h, const Record& rec){
        if(h.records.count(rec.id)) return false;
        if(rec.ts <= h.man.ts) return false;
        if(rec.ts > wallMs() + kFutureMs) return false;
        bool sm = false;
        HubRole sr = roleIn(h, rec.signer, sm);
        if(!sm){
            for(const Member& m : h.man.members) if(m.key == rec.signer){ sm = true; sr = m.role; }
        }
        if(!sm) return false;
        bool allowed = false;
        switch(rec.type){
        case kAdmit: case kKick: case kBan: allowed = sr >= HubRole::Admin; break;
        case kUnban: allowed = sr == HubRole::Owner; break;
        case kLeave: allowed = rec.signer == rec.target; break;
        }
        if(!allowed) return false;
        size_t mine = 0;
        const Record* oldestMine = nullptr;
        for(auto& kv : h.records){
            if(kv.second.signer != rec.signer) continue;
            mine++;
            if(!oldestMine || kv.second.ts < oldestMine->ts) oldestMine = &kv.second;
        }
        if(mine >= kMaxRecordsPerSigner && oldestMine) h.records.erase(oldestMine->id);
        if(h.records.size() >= kMaxRecords){
            auto oldest = h.records.begin();
            for(auto it = h.records.begin(); it != h.records.end(); ++it) if(it->second.ts < oldest->second.ts) oldest = it;
            h.records.erase(oldest);
        }
        h.records[rec.id] = rec;
        return true;
    }

    void afterMembershipChange(Hub& h){
        bool was = h.joined;
        recompute(h);
        h.dropped.clear();
        saveRecords(h);
        emit(HubEvent::ManifestChanged, h.id);
        if(was && !h.joined) removedFromHub(h, "You are no longer a member of " + h.man.name + ".");
    }

    void removedFromHub(Hub& h, const std::string& why){
        if(h.removed) return;
        Key id = h.id;
        if(voiceVc && voiceHub == id) voiceLeaveNow();
        h.removed = true;
        h.joined = false;
        h.links.clear();
        h.wanted.clear();
        h.embargo.clear();
        h.waiting.clear();
        removeFiles(h);
        wipeKeys(h);
        for(auto it = invites.begin(); it != invites.end();) if(it->hub == id) it = invites.erase(it); else ++it;
        saveInvites();
        doomed.push_back(id);
        emit(HubEvent::Left, id, 0, nullptr, why);
    }

    void checkJoinDone(Hub& h, const Key& from){
        if(!h.joined || h.removed) return;
        for(auto it = joins.begin(); it != joins.end(); ++it){
            if(it->inviter != from) continue;
            joins.erase(it);
            if(peers[from].up) linkUp(h, from, true);
            return;
        }
    }

    void onRecord(Hub& h, const Key& from, const uint8_t* p, size_t n){
        Record rec;
        if(!decodeRecord(h.id, p, n, rec)) return;
        if(!storeRecord(h, rec)) return;
        for(auto& kv : h.links) if(kv.first != from) sendFrame(kv.first, kRecord, h.id, p, n);
        afterMembershipChange(h);
        checkJoinDone(h, from);
    }

    void floodRecord(Hub& h, const std::vector<uint8_t>& raw){
        Record rec;
        if(!decodeRecord(h.id, raw.data(), raw.size(), rec)) return;
        if(!storeRecord(h, rec)) return;
        for(auto& kv : h.links) sendFrame(kv.first, kRecord, h.id, raw.data(), raw.size());
    }

    bool recordHeld(const Hub& h, const Record& r){
        switch(r.type){
        case kAdmit: return h.eff.count(r.target) || h.bans.count(r.target);
        case kKick: return !h.eff.count(r.target);
        case kBan: return !h.eff.count(r.target) && h.bans.count(r.target);
        case kUnban: return !h.bans.count(r.target);
        case kLeave: return !h.eff.count(r.target);
        }
        return true;
    }

    void acceptManifest(Hub& h, Manifest& m){
        bool was = h.joined;
        std::vector<Record> mine;
        for(auto& kv : h.records){
            if(kv.second.signer != myKey || kv.second.ts > m.ts) continue;
            if(std::find(m.folded.begin(), m.folded.end(), kv.first) != m.folded.end()) continue;
            mine.push_back(kv.second);
        }
        h.man = m;
        for(auto it = h.records.begin(); it != h.records.end();) if(it->second.ts <= m.ts) it = h.records.erase(it); else ++it;
        std::set<uint64_t> chans;
        for(const Channel& c : m.channels) chans.insert(c.id);
        for(auto it = h.keys.begin(); it != h.keys.end();) if(!chans.count(it->first)) it = h.keys.erase(it); else ++it;
        recompute(h);
        if(h.joined){
            bool reissued = false;
            for(const Record& r : mine){
                if(recordHeld(h, r) || r.type == kLeave) continue;
                bool sm = false;
                if(roleIn(h, myKey, sm) < HubRole::Admin && r.type != kLeave) continue;
                std::vector<uint8_t> raw = signRecord(h.id, r.type, r.target, r.name, m.ts + 1);
                Record nr;
                if(decodeRecord(h.id, raw.data(), raw.size(), nr) && storeRecord(h, nr)){
                    for(auto& kv : h.links) sendFrame(kv.first, kRecord, h.id, raw.data(), raw.size());
                    reissued = true;
                }
            }
            if(reissued) recompute(h);
        }
        saveAll(h);
        emit(HubEvent::ManifestChanged, h.id);
        offerAll(h);
        std::vector<Key> gone;
        gone.swap(h.dropped);
        for(const Key& k : gone) sendManifest(h, k);
        if(was && !h.joined) removedFromHub(h, "You are no longer a member of " + m.name + ".");
    }

    void onChunk(Hub& h, const Key& from, const uint8_t* p, size_t n){
        Reader r(p, n);
        Key hash = r.key();
        uint16_t idx = r.u16();
        uint16_t cnt = r.u16();
        if(!r.ok || cnt == 0 || cnt > kMaxChunks || idx >= cnt || n - r.i > kChunkBytes) return;
        Assembly& a = peers[from].assembly[h.id];
        if(a.hash != hash || a.count != cnt){
            a = Assembly();
            a.hash = hash;
            a.count = cnt;
            a.parts.assign(cnt, std::vector<uint8_t>());
            a.startMs = now;
        }
        if(a.parts[idx].empty()){
            a.parts[idx].assign(p + r.i, p + n);
            a.have++;
        }
        if(a.have < a.count) return;
        std::vector<uint8_t> raw;
        for(auto& part : a.parts) raw.insert(raw.end(), part.begin(), part.end());
        peers[from].assembly.erase(h.id);
        if(hashOf(raw.data(), raw.size()) != hash) return;
        Manifest m;
        if(!decodeManifest(raw, m) || m.hub != h.id) return;
        if(h.man.version && (m.owner != h.man.owner || m.version <= h.man.version)) return;
        bool first = h.man.version == 0;
        acceptManifest(h, m);
        if(first){
            std::vector<std::vector<uint8_t>> rec, keys, pres;
            rec.swap(h.pendRec);
            keys.swap(h.pendKeys);
            pres.swap(h.pendPres);
            for(auto& rr : rec) onRecord(h, from, rr.data(), rr.size());
            for(auto& k : keys) addKeys(h, k.data(), k.size());
            for(auto& pp : pres) onPresence(h, from, pp.data(), pp.size());
            if(!h.joined) sendFrame(from, kRecReq, h.id, nullptr, 0);
        }
        checkJoinDone(h, from);
    }

    void sendManifest(Hub& h, const Key& peer){
        const std::vector<uint8_t>& raw = h.man.raw;
        uint16_t cnt = (uint16_t)((raw.size() + kChunkBytes - 1) / kChunkBytes);
        for(uint16_t i = 0; i < cnt; i++){
            Writer w;
            w.raw(h.man.hash.data(), 32);
            w.u16(i);
            w.u16(cnt);
            size_t off = (size_t)i * kChunkBytes;
            w.raw(raw.data() + off, std::min(kChunkBytes, raw.size() - off));
            sendFrame(peer, kChunk, h.id, w.b.data(), w.b.size());
        }
    }

    void onOffer(Hub& h, const Key& from, const uint8_t* p, size_t n){
        Reader r(p, n);
        uint64_t version = r.u64();
        Key hash = r.key();
        Key digest = r.key();
        uint16_t gen = r.u16();
        if(!r.end()) return;
        bool member = h.eff.count(from) != 0;
        if(h.joined && member && !h.links.count(from) && h.links.size() < kMeshMax) linkUp(h, from, false);
        if(version > h.man.version) sendFrame(from, kManReq, h.id, hash.data(), 32);
        Key mine = recordsDigest(h);
        if(digest != mine && mayReply(from, h.id, kRecReq, getU64(digest.data()) ^ getU64(mine.data()))) sendFrame(from, kRecReq, h.id, nullptr, 0);
        if(h.joined && member && gen < generation(h) && mayReply(from, h.id, kKeys, ((uint64_t)gen << 16) | generation(h))) sendKeys(h, from);
    }

    void onJoin(const Key& from, const uint8_t* p, size_t n){
        Reader r(p, n);
        const uint8_t* token = r.take(10);
        std::string name = r.str(32);
        Key zero{};
        auto no = [&](uint8_t reason){ sendFrame(from, kJoinNo, zero, &reason, 1); };
        if(!token || !r.end()) return;
        const std::string& verified = peers[from].name;
        if(verified.empty() || verified != name){ no(kNoUnknown); return; }
        Key th = hashOf(token, 10);
        auto iv = std::find_if(invites.begin(), invites.end(), [&](const Invite& x){ return x.tokenHash == th; });
        if(iv == invites.end()){ no(kNoUnknown); return; }
        if(iv->expiry < wallMs() / 1000){ no(kNoExpired); return; }
        if(iv->uses == 0){ no(kNoUsed); return; }
        auto hit = hubs.find(iv->hub);
        if(hit == hubs.end() || !hit->second.joined){ no(kNoUnknown); return; }
        Hub& h = hit->second;
        bool sm = false;
        HubRole myRole = roleIn(h, myKey, sm);
        if(!sm || myRole < HubRole::Admin){ no(kNoUnknown); return; }
        if(h.bans.count(from)){ no(kNoBanned); return; }
        if(!h.eff.count(from)){
            if(h.eff.size() >= kMaxHubMembers){ no(kNoFull); return; }
            iv->uses--;
            saveInvites();
            if(myRole == HubRole::Owner){
                Member m;
                m.key = from;
                m.name = name;
                ownerManifest(h, [&](Manifest& mm){ mm.members.push_back(m); });
            } else {
                floodRecord(h, signRecord(h.id, kAdmit, from, name));
                afterMembershipChange(h);
            }
        }
        linkUp(h, from, false);
        offer(h, from);
        for(auto& kv : h.records) sendFrame(from, kRecord, h.id, kv.second.raw.data(), kv.second.raw.size());
        sendKeys(h, from);
    }

    void onJoinNo(const Key& from, const uint8_t* p, size_t n){
        if(n != 1) return;
        for(auto it = joins.begin(); it != joins.end(); ++it){
            if(it->inviter != from) continue;
            static const char* why[] = { "", "That invite has expired.", "That invite has been used up.", "You are banned from that hub.", "That hub is full (100 members).", "That invite is not valid (or your @name could not be checked)." };
            Key zero{};
            error(zero, p[0] >= 1 && p[0] <= 5 ? why[p[0]] : why[5]);
            joins.erase(it);
            return;
        }
    }

    void ownerManifest(Hub& h, const std::function<void(Manifest&)>& change){
        Manifest m = h.man;
        m.version = h.man.version + 1;
        m.ts = std::max(wallMs(), h.man.ts + 1);
        m.members.clear();
        for(auto& kv : h.eff) m.members.push_back(kv.second);
        m.bans.assign(h.bans.begin(), h.bans.end());
        m.folded.clear();
        std::vector<const Record*> recs;
        for(auto& kv : h.records) if(kv.second.ts <= m.ts) recs.push_back(&kv.second);
        std::sort(recs.begin(), recs.end(), [](const Record* a, const Record* b){ return a->ts > b->ts; });
        for(size_t i = 0; i < recs.size() && i < 255; i++) m.folded.push_back(recs[i]->id);
        change(m);
        signManifest(m);
        acceptManifest(h, m);
    }

    void voiceLeaveNow(){
        if(!voiceVc) return;
        Key hub = voiceHub;
        uint64_t vc = voiceVc;
        voiceVc = 0;
        for(auto& kv : outStreams){
            uint8_t sid = kv.second;
            sendFrame(kv.first, kVoiceBye, hub, &sid, 1);
        }
        outStreams.clear();
        auto it = hubs.find(hub);
        if(it != hubs.end()){
            sendPresence(it->second, true);
            emit(HubEvent::VoiceMembers, hub, vc);
        }
    }

    std::vector<Key> voicePeers(){
        std::vector<Key> out;
        auto it = hubs.find(voiceHub);
        if(!voiceVc || it == hubs.end()) return out;
        for(auto& kv : it->second.pres){
            if(kv.first == myKey || !kv.second.online || kv.second.vc != voiceVc || now - kv.second.seenMs > kPresenceTtlMs) continue;
            if(it->second.eff.count(kv.first)) out.push_back(kv.first);
        }
        return out;
    }

    void maintainVoice(){
        if(!voiceVc) return;
        std::vector<Key> vp = voicePeers();
        auto hit = hubs.find(voiceHub);
        for(const Key& k : vp){
            if(outStreams.count(k)) continue;
            PeerInfo& pi = peers[k];
            if(pi.up){
                uint8_t sid = pi.nextStream++;
                if(pi.nextStream == 0) pi.nextStream = 1;
                outStreams[k] = sid;
                Writer w;
                w.u64(voiceVc);
                w.u8(sid);
                sendFrame(k, kVoiceStream, voiceHub, w.b.data(), w.b.size());
            } else if(hit != hubs.end() && !hit->second.wanted.count(k)){
                hit->second.wanted[k] = now;
                wantPeer(k, hit->second.eff[k].name, std::string());
            }
        }
        for(auto it = outStreams.begin(); it != outStreams.end();){
            if(std::find(vp.begin(), vp.end(), it->first) == vp.end()){
                uint8_t sid = it->second;
                sendFrame(it->first, kVoiceBye, voiceHub, &sid, 1);
                it = outStreams.erase(it);
            } else ++it;
        }
    }

    bool presenceFresh(const Hub& h, const Key& k){
        auto it = h.pres.find(k);
        return it != h.pres.end() && it->second.online && now - it->second.seenMs <= kPresenceTtlMs;
    }

    void maintain(Hub& h){
        advanceEpochs(h);
        for(auto& kv : h.keys){
            if(kv.second.prevEpochUntil && now >= kv.second.prevEpochUntil){ for(IssuedKey& k : kv.second.prevEpoch.keys) crypto_wipe(k.key.data(), 32); kv.second.prevEpoch = KeySet(); kv.second.prevEpochUntil = 0; }
            if(kv.second.prevGenUntil && now >= kv.second.prevGenUntil){ for(IssuedKey& k : kv.second.prevGen.keys) crypto_wipe(k.key.data(), 32); kv.second.prevGen = KeySet(); kv.second.prevGenUntil = 0; }
        }
        for(auto it = h.embargo.begin(); it != h.embargo.end();){
            if((int32_t)(now - it->second.first) >= 0){
                uint64_t id = it->first;
                std::vector<uint8_t> msg = it->second.second;
                it = h.embargo.erase(it);
                if(!h.fluffed.count(id)) fluff(h, msg, id, nullptr);
            } else ++it;
        }
        for(auto* m : { &h.seen, &h.fluffed, &h.presSeen }) for(auto it = m->begin(); it != m->end();) if(now - it->second > kSeenMs) it = m->erase(it); else ++it;
        while(!h.waiting.empty() && (int32_t)(now - h.waiting.front().deadline) >= 0){
            h.waiting.pop_front();
            error(h.id, "A message to " + h.man.name + " was not sent: nobody else from this hub is online.");
        }
        for(auto it = h.wanted.begin(); it != h.wanted.end();) if(now - it->second > kWantTimeoutMs) it = h.wanted.erase(it); else ++it;
        if(!h.joined) return;
        if(!h.lastPresMs || now - h.lastPresMs >= kPresenceEveryMs) sendPresence(h, true);
        bool mod = false;
        HubRole myRole = roleIn(h, myKey, mod);
        if(mod && myRole >= HubRole::Admin && !h.keys.empty() && (!h.lastRekeyMs || now - h.lastRekeyMs >= kRekeyRetryMs)){
            uint32_t nowEpoch = hourOf(wallMs());
            bool stale = false, badIssuer = false;
            for(auto& kv : h.keys){
                if(kv.second.cur.keys.empty()) continue;
                bool issuerOk = false;
                for(const IssuedKey& k : kv.second.cur.keys){
                    bool im = false;
                    if(roleIn(h, k.issuer, im) >= HubRole::Admin && im) issuerOk = true;
                    if(nowEpoch > k.e0 + kRekeyAfter + myKey[0] % 24) stale = true;
                }
                if(!issuerOk) badIssuer = true;
            }
            if(badIssuer && !h.badIssuerSince) h.badIssuerSince = now;
            if(!badIssuer) h.badIssuerSince = 0;
            uint32_t wait = myRole == HubRole::Owner ? 0 : 30000 + (uint32_t)(myKey[1] % 30) * 1000;
            if(stale || (badIssuer && now - h.badIssuerSince >= wait)){
                h.badIssuerSince = 0;
                rekeyAndShare(h);
            }
        }
        if(now - h.lastOfferMs >= kReofferMs){
            h.lastOfferMs = now;
            offerAll(h);
        }
        while(h.links.size() > kMeshMax){
            std::vector<Key> spare;
            for(auto& kv : h.links) if(std::find(h.succ.begin(), h.succ.end(), kv.first) == h.succ.end() && now - kv.second.sinceMs > 10000) spare.push_back(kv.first);
            if(spare.empty()) break;
            linkDown(h, spare[std::uniform_int_distribution<size_t>(0, spare.size() - 1)(rng)]);
        }
        std::vector<Key> online, all;
        for(auto& kv : h.eff){
            if(kv.first == myKey || h.links.count(kv.first) || h.wanted.count(kv.first)) continue;
            all.push_back(kv.first);
            if(presenceFresh(h, kv.first)) online.push_back(kv.first);
        }
        std::vector<Key>& cand = online.empty() ? all : online;
        bool heal = h.links.size() >= kMeshTarget && !online.empty() && now - h.lastHealMs >= kHealMs;
        if(!cand.empty() && (h.links.size() < kMeshTarget || heal)){
            Key k = cand[std::uniform_int_distribution<size_t>(0, cand.size() - 1)(rng)];
            if(heal){ h.lastHealMs = now; h.healPending = true; }
            if(peers[k].up) linkUp(h, k, true);
            else if(wantPeer(k, h.eff[k].name, std::string())) h.wanted[k] = now;
        }
        if(!h.epochStart || now - h.epochStart >= kEpochMs) newEpoch(h);
    }

    Hub* hubOf(const uint8_t id[32]){
        auto it = hubs.find(toKey(id));
        return it == hubs.end() ? nullptr : &it->second;
    }
};

namespace {

struct Reaper {
    HubCore::Impl* p;
    ~Reaper(){ p->reap(); }
};

}

HubCore::HubCore(const Identity& me, const std::string& myName, const std::string& dataDir) : p_(new Impl) {
    p_->me = me;
    std::memcpy(p_->myKey.data(), me.pub, 32);
    p_->myName = myName;
    p_->dir = dataDir.empty() ? std::string("hubs") : dataDir + "/hubs";
    uint8_t seed[8];
    sysRandom(seed, 8);
    p_->rng.seed(getU64(seed));
    p_->nowMs();
    p_->loadAll();
}

HubCore::~HubCore(){
    for(auto& kv : p_->hubs) p_->wipeKeys(kv.second);
    identityWipe(p_->me);
    delete p_;
}

void HubCore::setSender(HubSendFn send){ p_->send = std::move(send); }
void HubCore::setWanted(HubWantFn want){ p_->want = std::move(want); }

void HubCore::peerUp(const uint8_t peer[32], const std::string& verifiedName){
    p_->nowMs();
    Reaper reaper{p_};
    Key k = toKey(peer);
    PeerInfo& pi = p_->peers[k];
    pi.up = true;
    pi.name = verifiedName;
    for(auto& kv : p_->hubs){
        Hub& h = kv.second;
        if(!h.joined || !h.eff.count(k)) continue;
        if(h.wanted.count(k) || h.links.size() < kMeshTarget) p_->linkUp(h, k, h.wanted.count(k) != 0);
    }
    for(PendingJoin& j : p_->joins){
        if(j.inviter != k || j.sent) continue;
        Writer w;
        w.raw(j.token, 10);
        w.str(p_->myName);
        Key zero{};
        p_->sendFrame(k, kJoin, zero, w.b.data(), w.b.size());
        j.sent = true;
    }
    p_->maintainVoice();
}

void HubCore::peerDown(const uint8_t peer[32]){
    p_->nowMs();
    Reaper reaper{p_};
    Key k = toKey(peer);
    auto it = p_->peers.find(k);
    if(it == p_->peers.end()) return;
    it->second.up = false;
    it->second.assembly.clear();
    bool hadStreams = !it->second.inStreams.empty();
    it->second.inStreams.clear();
    p_->outStreams.erase(k);
    for(auto& kv : p_->hubs) p_->linkDown(kv.second, k);
    for(PendingJoin& j : p_->joins) if(j.inviter == k) j.sent = false;
    if(hadStreams && p_->voiceVc) p_->emit(HubEvent::VoiceMembers, p_->voiceHub, p_->voiceVc);
}

void HubCore::onFrame(const uint8_t peer[32], const uint8_t* p, size_t n){
    p_->nowMs();
    Reaper reaper{p_};
    if(n < kHdr) return;
    Key from = toKey(peer);
    auto pit = p_->peers.find(from);
    if(pit == p_->peers.end() || !pit->second.up) return;
    uint8_t kind = p[0];
    Key hubId = toKey(p + 1);
    const uint8_t* body = p + kHdr;
    size_t bn = n - kHdr;
    if(kind == kJoin){ p_->onJoin(from, body, bn); return; }
    if(kind == kJoinNo){ p_->onJoinNo(from, body, bn); return; }
    auto hit = p_->hubs.find(hubId);
    if(hit == p_->hubs.end()){
        if(kind != kOffer && kind != kChunk && kind != kRecord && kind != kKeys) return;
        bool pending = false;
        for(const PendingJoin& j : p_->joins) if(j.inviter == from && j.sent) pending = true;
        if(!pending) return;
        Hub& h = p_->hubs[hubId];
        h.id = hubId;
        hit = p_->hubs.find(hubId);
    }
    Hub& h = hit->second;
    bool fromMember = h.eff.count(from) != 0;
    if(!h.joined){
        bool inviter = false;
        for(const PendingJoin& j : p_->joins) if(j.inviter == from) inviter = true;
        if(!inviter && !fromMember) return;
        if(kind == kOffer) p_->onOffer(h, from, body, bn);
        else if(kind == kChunk) p_->onChunk(h, from, body, bn);
        else if(kind == kRecord){
            if(h.man.version) p_->onRecord(h, from, body, bn);
            else if(h.pendRec.size() < kMaxRecords) h.pendRec.emplace_back(body, body + bn);
        } else if(kind == kKeys){
            if(h.man.version) p_->addKeys(h, body, bn);
            else if(h.pendKeys.size() < 8) h.pendKeys.emplace_back(body, body + bn);
        } else if(kind == kPresence){
            if(h.man.version) p_->onPresence(h, from, body, bn);
            else if(h.pendPres.size() < kMaxHubMembers) h.pendPres.emplace_back(body, body + bn);
        }
        p_->reap();
        return;
    }
    if(!fromMember) return;
    switch(kind){
    case kOffer: p_->onOffer(h, from, body, bn); break;
    case kManReq:
        if(bn == 32 && toKey(body) == h.man.hash && p_->mayReply(from, h.id, kManReq, h.man.version)) p_->sendManifest(h, from);
        break;
    case kChunk: p_->onChunk(h, from, body, bn); break;
    case kRecReq:
        if(bn == 0 && p_->mayReply(from, h.id, kRecReq, 0)) for(auto& kv : h.records) p_->sendFrame(from, kRecord, h.id, kv.second.raw.data(), kv.second.raw.size());
        break;
    case kRecord: p_->onRecord(h, from, body, bn); break;
    case kStem: p_->onStem(h, from, body, bn); break;
    case kFluff: p_->onFluff(h, from, body, bn); break;
    case kPresence: p_->onPresence(h, from, body, bn); break;
    case kKeys: p_->addKeys(h, body, bn); break;
    case kVoiceStream:
        if(bn == 9){
            Reader r(body, bn);
            uint64_t vc = r.u64();
            uint8_t sid = r.u8();
            bool isVoice = false;
            for(const Channel& c : h.man.channels) if(c.id == vc && c.kind == ChannelKind::Voice) isVoice = true;
            if(isVoice && sid){
                pit->second.inStreams[sid] = std::make_pair(h.id, vc);
                p_->emit(HubEvent::VoiceMembers, h.id, vc);
            }
        }
        break;
    case kVoiceBye:
        if(bn == 1){
            auto sit = pit->second.inStreams.find(body[0]);
            if(sit != pit->second.inStreams.end()){
                uint64_t vc = sit->second.second;
                pit->second.inStreams.erase(sit);
                p_->emit(HubEvent::VoiceMembers, h.id, vc);
            }
        }
        break;
    default: break;
    }
}

void HubCore::tick(uint32_t){
    p_->nowMs();
    Reaper reaper{p_};
    for(auto it = p_->joins.begin(); it != p_->joins.end();){
        if(p_->now - it->startMs > kJoinTimeoutMs){
            Key zero{};
            p_->error(zero, "Could not join: @" + it->inviterName + " did not answer. They need to be online when you join.");
            it = p_->joins.erase(it);
        } else ++it;
    }
    for(auto& pk : p_->peers){
        for(auto it = pk.second.assembly.begin(); it != pk.second.assembly.end();) if(p_->now - it->second.startMs > kAssemblyMs) it = pk.second.assembly.erase(it); else ++it;
    }
    std::vector<Key> ids;
    for(auto& kv : p_->hubs) ids.push_back(kv.first);
    for(const Key& id : ids){
        auto it = p_->hubs.find(id);
        if(it != p_->hubs.end()) p_->maintain(it->second);
    }
    uint64_t wallS = wallMs() / 1000;
    size_t before = p_->invites.size();
    p_->invites.erase(std::remove_if(p_->invites.begin(), p_->invites.end(), [&](const Invite& iv){ return iv.expiry < wallS || iv.uses == 0 || !p_->hubs.count(iv.hub); }), p_->invites.end());
    if(p_->invites.size() != before) p_->saveInvites();
    p_->maintainVoice();
}

bool HubCore::knows(const uint8_t key[32]) const {
    Key k = toKey(key);
    for(auto& kv : p_->hubs) if(kv.second.joined && kv.second.eff.count(k)) return true;
    return false;
}

bool HubCore::uses(const uint8_t peer[32]) const {
    Key k = toKey(peer);
    for(auto& kv : p_->hubs) if(kv.second.links.count(k) || kv.second.wanted.count(k)) return true;
    for(const PendingJoin& j : p_->joins) if(j.inviter == k) return true;
    if(p_->outStreams.count(k)) return true;
    auto it = p_->peers.find(k);
    return it != p_->peers.end() && !it->second.inStreams.empty();
}

bool HubCore::expectsJoin(const uint8_t*, const std::string& invHint) const {
    uint64_t wallS = wallMs() / 1000;
    for(const Invite& iv : p_->invites){
        if(iv.expiry < wallS || iv.uses == 0) continue;
        if(hexOf(iv.tokenHash.data(), 8) == invHint) return true;
    }
    return false;
}

bool HubCore::create(const std::string& name, uint8_t hubOut[32], std::string& err){
    p_->nowMs();
    Reaper reaper{p_};
    if(!textOk(name, kMaxHubName)){ err = "A hub name needs 1 to 48 characters."; return false; }
    if(p_->myName.empty()){ err = "Choose your @name first."; return false; }
    Manifest m;
    sysRandom(m.salt, 16);
    m.owner = p_->myKey;
    m.hub = Impl::hubIdFor(m.owner, m.salt);
    m.version = 1;
    m.ts = wallMs();
    m.name = name;
    m.colour = kPalette[m.hub[0] % (sizeof kPalette / sizeof kPalette[0])];
    Channel text, voice;
    sysRandom((uint8_t*)&text.id, 8);
    sysRandom((uint8_t*)&voice.id, 8);
    text.id |= 1;
    voice.id |= 1;
    text.name = "general";
    voice.name = "General";
    voice.kind = ChannelKind::Voice;
    m.channels = { text, voice };
    Member me;
    me.key = p_->myKey;
    me.role = HubRole::Owner;
    me.name = p_->myName;
    m.members = { me };
    p_->signManifest(m);
    Hub& h = p_->hubs[m.hub];
    h.id = m.hub;
    h.man = m;
    p_->recompute(h);
    p_->newKeys(h);
    p_->saveAll(h);
    std::memcpy(hubOut, m.hub.data(), 32);
    return true;
}

std::string HubCore::makeInvite(const uint8_t hub[32], int maxUses, uint32_t lifeSec, std::string& err){
    p_->nowMs();
    Reaper reaper{p_};
    Hub* h = p_->hubOf(hub);
    if(!h || !h->joined){ err = "You are not in that hub."; return std::string(); }
    bool member = false;
    if(p_->roleIn(*h, p_->myKey, member) < HubRole::Admin){ err = "Only the hub owner and admins can invite people."; return std::string(); }
    if(p_->myName.empty()){ err = "Choose your @name first."; return std::string(); }
    if(maxUses < 1) maxUses = 1;
    if(maxUses > 1000) maxUses = 1000;
    if(lifeSec < 60) lifeSec = 60;
    if(lifeSec > 30u * 86400u) lifeSec = 30u * 86400u;
    uint8_t token[10];
    sysRandom(token, 10);
    Invite iv;
    iv.tokenHash = hashOf(token, 10);
    iv.hub = h->id;
    iv.uses = (uint16_t)maxUses;
    iv.expiry = wallMs() / 1000 + lifeSec;
    p_->invites.push_back(iv);
    p_->saveInvites();
    uint8_t body[42];
    std::memcpy(body, p_->myKey.data(), 32);
    std::memcpy(body + 32, token, 10);
    std::string code = "SCH1-" + base32Encode(body, sizeof body) + "@" + p_->myName;
    crypto_wipe(token, 10);
    crypto_wipe(body, sizeof body);
    return code;
}

bool HubCore::join(const std::string& invite, std::string& err){
    p_->nowMs();
    Reaper reaper{p_};
    std::string s;
    for(char c : invite) if(c != ' ' && c != '\t' && c != '\r' && c != '\n') s += c;
    size_t at = s.rfind('@');
    if(s.size() < 6 || (s.compare(0, 5, "SCH1-") != 0 && s.compare(0, 5, "sch1-") != 0) || at == std::string::npos || at < 5){ err = "That is not a StarCom hub invite."; return false; }
    std::vector<uint8_t> raw;
    std::string name;
    if(!base32Decode(s.substr(5, at - 5), raw) || raw.size() != 42 || !normalizeName(s.substr(at + 1), name)){ err = "That invite is damaged. Ask for a new one."; return false; }
    Key inviter = toKey(raw.data());
    if(inviter == p_->myKey){ err = "That is your own invite."; return false; }
    if(p_->myName.empty()){ err = "Choose your @name first."; return false; }
    for(const PendingJoin& j : p_->joins) if(j.inviter == inviter){ err = "Already joining through @" + name + "."; return false; }
    PendingJoin j;
    j.inviter = inviter;
    std::memcpy(j.token, raw.data() + 32, 10);
    j.inviterName = name;
    j.startMs = p_->now;
    Key th = hashOf(j.token, 10);
    std::string hint = "inv:" + hexOf(th.data(), 8);
    p_->joins.push_back(j);
    crypto_wipe(raw.data(), raw.size());
    PeerInfo& pi = p_->peers[inviter];
    if(pi.up){
        Writer w;
        w.raw(p_->joins.back().token, 10);
        w.str(p_->myName);
        Key zero{};
        p_->sendFrame(inviter, kJoin, zero, w.b.data(), w.b.size());
        p_->joins.back().sent = true;
    } else if(p_->want){
        p_->want(inviter.data(), name, hint);
    }
    return true;
}

bool HubCore::leave(const uint8_t hub[32], std::string& err){
    p_->nowMs();
    Reaper reaper{p_};
    Hub* h = p_->hubOf(hub);
    if(!h){ err = "You are not in that hub."; return false; }
    if(h->man.owner == p_->myKey){ err = "The owner cannot leave their own hub yet."; return false; }
    if(h->joined){
        std::vector<uint8_t> rec = p_->signRecord(h->id, kLeave, p_->myKey, std::string());
        for(auto& kv : h->links) p_->sendFrame(kv.first, kRecord, h->id, rec.data(), rec.size());
        p_->sendPresence(*h, false);
    }
    p_->removedFromHub(*h, "You left " + h->man.name + ".");
    return true;
}

bool HubCore::post(const uint8_t hub[32], uint64_t channel, const std::string& text, std::string& err){
    p_->nowMs();
    Reaper reaper{p_};
    Hub* h = p_->hubOf(hub);
    if(!h || !h->joined){ err = "You are not in that hub."; return false; }
    if(text.empty()) return true;
    if(text.size() > kMaxHubText){ err = "That message is too long (" + std::to_string(text.size()) + " bytes, limit " + std::to_string(kMaxHubText) + ")."; return false; }
    bool isText = false;
    for(const Channel& c : h->man.channels) if(c.id == channel && c.kind == ChannelKind::Text) isText = true;
    if(!isText){ err = "That is not a text channel."; return false; }
    p_->advanceEpochs(*h);
    if(!h->buckets[p_->myKey].take(p_->now)){ err = "Slow down a little (one message a second)."; return false; }
    bool ok = false;
    std::vector<uint8_t> msg = p_->sealMessage(*h, channel, text, ok);
    if(!ok){ err = "This hub's keys have not arrived yet. Try again in a moment."; return false; }
    uint64_t id = id8(msg.data() + msg.size() - 64);
    p_->deliver(*h, id, p_->myKey, channel, text, getU64(msg.data() + 32));
    p_->stemOwn(*h, msg, id);
    return true;
}

bool HubCore::manage(const uint8_t hub[32], ManageOp op, const ManageArgs& a, std::string& err){
    p_->nowMs();
    Reaper reaper{p_};
    Hub* hp = p_->hubOf(hub);
    if(!hp || !hp->joined){ err = "You are not in that hub."; return false; }
    Hub& h = *hp;
    bool member = false;
    HubRole mine = p_->roleIn(h, p_->myKey, member);
    Key target = toKey(a.member);
    bool tm = false;
    HubRole tr = p_->roleIn(h, target, tm);
    if(op == ManageOp::Kick || op == ManageOp::Ban){
        if(mine < HubRole::Admin){ err = "Only the hub owner and admins can do that."; return false; }
        if(target == p_->myKey){ err = "You cannot remove yourself that way; use Leave."; return false; }
        if(tm && tr >= mine){ err = "You cannot remove someone with the same or a higher role."; return false; }
        if(op == ManageOp::Kick && !tm){ err = "They are not in this hub."; return false; }
        if(mine == HubRole::Owner){
            p_->ownerManifest(h, [&](Manifest& m){
                m.members.erase(std::remove_if(m.members.begin(), m.members.end(), [&](const Member& x){ return x.key == target; }), m.members.end());
                if(op == ManageOp::Ban && std::find(m.bans.begin(), m.bans.end(), target) == m.bans.end() && m.bans.size() < 255) m.bans.push_back(target);
            });
        } else {
            p_->floodRecord(h, p_->signRecord(h.id, op == ManageOp::Kick ? kKick : kBan, target, std::string()));
            p_->afterMembershipChange(h);
        }
        auto hit = p_->hubs.find(toKey(hub));
        if(hit == p_->hubs.end() || hit->second.removed) return true;
        p_->rekeyAndShare(hit->second);
        return true;
    }
    if(mine != HubRole::Owner){ err = "Only the hub owner can do that."; return false; }
    switch(op){
    case ManageOp::Rename:
        if(!textOk(a.name, kMaxHubName)){ err = "A hub name needs 1 to 48 characters."; return false; }
        p_->ownerManifest(h, [&](Manifest& m){ m.name = a.name; });
        return true;
    case ManageOp::AddChannel: {
        if(!textOk(a.name, kMaxChannelName)){ err = "A channel name needs 1 to 32 characters."; return false; }
        if(h.man.channels.size() >= kMaxHubChannels){ err = "A hub can have at most 24 channels."; return false; }
        Channel c;
        sysRandom((uint8_t*)&c.id, 8);
        c.id |= 1;
        c.name = a.name;
        c.kind = a.kind == ChannelKind::Voice ? ChannelKind::Voice : ChannelKind::Text;
        p_->ownerManifest(h, [&](Manifest& m){ m.channels.push_back(c); });
        p_->rekeyAndShare(h);
        return true;
    }
    case ManageOp::RenameChannel: {
        if(!textOk(a.name, kMaxChannelName)){ err = "A channel name needs 1 to 32 characters."; return false; }
        bool found = false;
        for(const Channel& c : h.man.channels) if(c.id == a.channel) found = true;
        if(!found){ err = "That channel does not exist."; return false; }
        p_->ownerManifest(h, [&](Manifest& m){ for(Channel& c : m.channels) if(c.id == a.channel) c.name = a.name; });
        return true;
    }
    case ManageOp::RemoveChannel: {
        bool found = false;
        for(const Channel& c : h.man.channels) if(c.id == a.channel) found = true;
        if(!found){ err = "That channel does not exist."; return false; }
        p_->ownerManifest(h, [&](Manifest& m){ m.channels.erase(std::remove_if(m.channels.begin(), m.channels.end(), [&](const Channel& c){ return c.id == a.channel; }), m.channels.end()); });
        return true;
    }
    case ManageOp::SetRole:
        if(!tm || target == p_->myKey){ err = "Pick a member other than yourself."; return false; }
        if(a.role == HubRole::Owner){ err = "A hub has exactly one owner."; return false; }
        p_->ownerManifest(h, [&](Manifest& m){ for(Member& x : m.members) if(x.key == target) x.role = a.role; });
        return true;
    case ManageOp::Unban:
        p_->ownerManifest(h, [&](Manifest& m){ m.bans.erase(std::remove(m.bans.begin(), m.bans.end(), target), m.bans.end()); });
        return true;
    default:
        err = "Unknown action.";
        return false;
    }
}

bool HubCore::voiceJoin(const uint8_t hub[32], uint64_t voiceChannel, std::string& err){
    p_->nowMs();
    Reaper reaper{p_};
    Hub* h = p_->hubOf(hub);
    if(!h || !h->joined){ err = "You are not in that hub."; return false; }
    bool isVoice = false;
    for(const Channel& c : h->man.channels) if(c.id == voiceChannel && c.kind == ChannelKind::Voice) isVoice = true;
    if(!isVoice){ err = "That is not a voice channel."; return false; }
    size_t in = 0;
    for(auto& kv : h->pres) if(kv.first != p_->myKey && p_->presenceFresh(*h, kv.first) && kv.second.vc == voiceChannel) in++;
    if(in >= kMaxVoiceChannelPeople){ err = "This voice channel is full (8 people)."; return false; }
    if(p_->voiceVc) p_->voiceLeaveNow();
    p_->voiceHub = h->id;
    p_->voiceVc = voiceChannel;
    p_->sendPresence(*h, true);
    p_->maintainVoice();
    p_->emit(HubEvent::VoiceMembers, h->id, voiceChannel);
    return true;
}

void HubCore::voiceLeave(){
    p_->nowMs();
    Reaper reaper{p_};
    p_->voiceLeaveNow();
}

bool HubCore::voiceStreamOut(const uint8_t peer[32], uint8_t& streamId) const {
    auto it = p_->outStreams.find(toKey(peer));
    if(it == p_->outStreams.end()) return false;
    streamId = it->second;
    return true;
}

bool HubCore::voiceStreamIn(const uint8_t peer[32], uint8_t streamId, uint8_t hubOut[32], uint64_t& voiceChannel) const {
    auto pit = p_->peers.find(toKey(peer));
    if(pit == p_->peers.end()) return false;
    auto it = pit->second.inStreams.find(streamId);
    if(it == pit->second.inStreams.end()) return false;
    std::memcpy(hubOut, it->second.first.data(), 32);
    voiceChannel = it->second.second;
    return true;
}

std::vector<HubEvent> HubCore::drain(){
    std::vector<HubEvent> out;
    out.swap(p_->events);
    return out;
}

std::vector<HubView> HubCore::hubs() const {
    std::vector<HubView> out;
    for(auto& kv : p_->hubs){
        const Hub& h = kv.second;
        if(!h.joined) continue;
        HubView v;
        std::memcpy(v.id, h.id.data(), 32);
        v.name = h.man.name;
        v.colour = h.man.colour;
        for(const Channel& c : h.man.channels){
            HubChannelView cv;
            cv.id = c.id;
            cv.name = c.name;
            cv.kind = c.kind;
            v.channels.push_back(cv);
        }
        for(auto& mk : h.eff){
            HubMemberView mv;
            std::memcpy(mv.key, mk.first.data(), 32);
            mv.name = mk.second.name;
            mv.role = mk.second.role;
            if(mk.first == p_->myKey){
                mv.online = true;
                mv.voiceChannel = (p_->voiceVc && p_->voiceHub == h.id) ? p_->voiceVc : 0;
                v.myRole = mk.second.role;
            } else {
                auto pit = h.pres.find(mk.first);
                mv.online = pit != h.pres.end() && pit->second.online && p_->now - pit->second.seenMs <= kPresenceTtlMs;
                mv.voiceChannel = mv.online ? pit->second.vc : 0;
            }
            v.members.push_back(mv);
        }
        if(v.myRole == HubRole::Owner){
            for(const Key& b : h.bans){
                HubMemberView bv;
                std::memcpy(bv.key, b.data(), 32);
                v.banned.push_back(bv);
            }
        }
        out.push_back(v);
    }
    return out;
}

}

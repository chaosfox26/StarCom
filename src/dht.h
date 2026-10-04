#pragma once
#include "identity.h"
#include "platform_net.h"
#include "wire.h"
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace sc {

constexpr size_t kDhtIdLen = 32;
constexpr size_t kDhtK = 8;
constexpr size_t kDhtAlpha = 3;
constexpr size_t kDhtPadded = 480;
constexpr size_t kMaxPointerLen = 300;
constexpr uint32_t kPointerMaxLifeSec = 3600;
constexpr uint32_t kPointerSlackSec = 300;

struct PointerRecord {
    uint8_t owner[32] = {0};
    std::string name;
    uint64_t seq = 0;
    uint32_t expires = 0;
    Addr relay;
    uint8_t relayPub[32] = {0};
    int relays = 1;
    Addr relay2;
    uint8_t relayPub2[32] = {0};
};

bool normalizeName(const std::string& in, std::string& out);
void nameKey(const std::string& name, uint8_t key[32]);
size_t signPointer(const Identity& id, const PointerRecord& r, uint8_t* out, size_t cap);
bool openPointer(const uint8_t* in, size_t n, uint32_t nowSec, PointerRecord& r);
uint32_t wallSeconds();

struct DhtContact {
    uint8_t id[32] = {0};
    Addr addr;
    uint32_t lastSeenMs = 0;
};

class DhtNode {
public:
    DhtNode(UdpSocket& s, bool clientOnly);
    void handle(const uint8_t* pkt, size_t n, const Addr& from);
    void tick();
    void drain();
    const uint8_t* id() const { return id_; }
    size_t contacts() const;
    size_t records() const { return store_.size(); }

    struct Found {
        DhtContact c;
        bool haveId = false;
        bool queried = false;
        bool answered = false;
        bool failed = false;
        uint32_t sentMs = 0;
        uint64_t txid = 0;
        uint8_t token[16] = {0};
        bool haveToken = false;
    };
    bool iterate(const uint8_t target[32], bool wantValue, const std::vector<Addr>& seeds,
                 std::vector<Found>& closest, std::vector<std::vector<uint8_t>>* values, int maxMs);
    bool storeAt(const Found& f, const uint8_t* record, size_t n, int maxMs);
    bool ping(const Addr& to, int maxMs);

private:
    struct Stored {
        std::vector<uint8_t> bytes;
        uint8_t owner[32];
        uint64_t seq;
        uint32_t expires;
    };
    struct Reply {
        uint64_t txid;
        uint8_t op;
        Addr from;
        uint8_t id[32];
        std::vector<DhtContact> nodes;
        uint8_t token[16];
        bool haveToken;
        std::vector<uint8_t> value;
        bool stored;
    };
    struct Bucket {
        double tokens = 40;
        uint32_t last = 0;
    };
    struct Eviction {
        int bucket;
        DhtContact candidate;
        uint64_t txid;
        uint32_t sentMs;
    };
    UdpSocket& s_;
    bool clientOnly_;
    uint8_t id_[32] = {0};
    std::vector<DhtContact> buckets_[256];
    std::unordered_map<std::string, Stored> store_;
    std::unordered_map<std::string, Bucket> rate_;
    uint8_t secret_[32] = {0};
    uint8_t prevSecret_[32] = {0};
    uint32_t secretAtMs_ = 0;
    uint32_t lastRefreshMs_ = 0;
    std::deque<Reply> replies_;
    std::vector<uint64_t> waiting_;
    std::vector<Eviction> evictions_;

    size_t header(uint8_t* p, uint8_t op, uint64_t txid) const;
    void seen(const uint8_t id[32], const Addr& a);
    std::vector<DhtContact> closestKnown(const uint8_t target[32], size_t n) const;
    void makeToken(const Addr& a, const uint8_t secret[32], uint8_t out[16]) const;
    bool tokenOk(const Addr& a, const uint8_t tok[16]) const;
    bool allow(const Addr& a);
    void answer(uint8_t op, uint64_t txid, const uint8_t* body, size_t n, const Addr& from, size_t inLen);
    bool acceptStore(const uint8_t* rec, size_t n);
    bool waitReply(uint64_t txid, int maxMs, Reply& out);
    void pump(int maxMs);
};

class StarDns {
public:
    bool start(UdpSocket& s, const std::vector<Addr>& seeds, std::string& err);
    bool publishRecord(const uint8_t* record, size_t n, std::string& err);
    bool publish(const Identity& id, const std::string& name, const Addr& relay, const uint8_t relayPub[32], std::string& err);
    bool lookup(const std::string& name, const uint8_t* pinnedPub, PointerRecord& out, std::string& err, std::vector<uint8_t>* raw = nullptr);
    void poll();
    ~StarDns();

private:
    DhtNode* node_ = nullptr;
    std::vector<Addr> seeds_;
};

}

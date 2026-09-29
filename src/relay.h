#pragma once
#include "dht.h"
#include "handshake.h"
#include "identity.h"
#include "platform_net.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sc {

enum LinkKind : uint8_t {
    kLinkRegister = 1, kLinkRegistered = 2, kLinkOpen = 3, kLinkData = 4, kLinkIncoming = 5, kLinkClose = 6,
    kLinkPublish = 7, kLinkLookup = 8, kLinkResult = 9, kLinkPing = 10, kLinkForward = 11, kLinkPublished = 12,
    kLinkBridgeHello = 13, kLinkBridgeOpen = 14, kLinkBridgeIn = 15, kLinkBridgeAck = 16, kLinkBridgeWelcome = 17
};
enum LinkPlainOp : uint8_t { kKeyRequest = 1, kKeyReply = 2 };
constexpr size_t kKeyRequestLen = 64;
constexpr size_t kEnvelopeLen = 96;
constexpr uint8_t kOpenMasked = 1;
constexpr size_t kMaxBridgeRelays = 8;

class RelayServer {
public:
    RelayServer(UdpSocket& s, int socketFamily, const Identity& me, const std::vector<Addr>& dhtSeeds);
    ~RelayServer();
    RelayServer(const RelayServer&) = delete;
    RelayServer& operator=(const RelayServer&) = delete;
    bool handle(const uint8_t* pkt, size_t n, const Addr& from);
    void tick();
    size_t connections() const { return conns_.size(); }
    size_t circuits() const { return circs_.size(); }
    size_t bridges() const;
    size_t pendingBridges() const { return pendingBridge_.size(); }

private:
    struct Conn {
        Session ses;
        bool dialed = false;
        bool confirmed = false;
        bool registered = false;
        uint32_t lastRecvMs = 0;
        uint32_t lastSendMs = 0;
        size_t circuitCount = 0;
        std::string peerKey;
        uint32_t dialWindowMs = 0;
        int dialsInWindow = 0;
        bool bridge = false;
        uint32_t capKbps = 0;
        std::vector<std::string> bridgeRelays;
        uint64_t bridgeBytes = 0;
        uint32_t bridgeWindowMs = 0;
        double goodKbps = 0;
        uint32_t bridgeIdleMs = 0;
    };
    struct End {
        uint64_t conn = 0;
        uint64_t circ = 0;
        std::string peerKey;
    };
    struct Circ {
        End other;
        bool opener = false;
        uint32_t lastMs = 0;
        double tokens = 0;
        uint32_t refillMs = 0;
    };
    struct Peer {
        Addr addr;
        uint8_t pub[32] = {0};
        HsDialer dial;
        uint64_t conn = 0;
        bool dialing = false;
        int tries = 0;
        uint32_t lastDialMs = 0;
        std::deque<std::vector<uint8_t>> queued;
    };
    struct Job {
        uint64_t conn;
        bool publish;
        std::vector<uint8_t> record;
        std::string name;
        uint8_t pin[32];
        bool pinned;
    };
    struct Done {
        uint64_t conn;
        bool publish;
        bool ok;
        bool taken = false;
        std::vector<uint8_t> record;
    };
    struct Rate {
        double tokens = 40;
        uint32_t last = 0;
    };
    struct Pending {
        uint64_t vConn = 0;
        uint64_t circIn = 0;
        uint32_t deadline = 0;
        Addr relay;
        uint8_t relayPub[32] = {0};
        uint8_t owner[32] = {0};
    };

    UdpSocket& s_;
    int family_;
    Identity me_;
    std::string caps_;
    std::unordered_map<uint64_t, Conn> conns_;
    std::unordered_map<std::string, uint64_t> byOwner_;
    std::unordered_map<std::string, uint64_t> stamps_;
    std::map<std::pair<uint64_t, uint64_t>, Circ> circs_;
    std::map<std::string, std::pair<uint64_t, uint64_t>> remoteIn_;
    std::unordered_map<std::string, Peer> peers_;
    std::unordered_map<std::string, Rate> rate_;
    std::map<std::pair<uint64_t, uint64_t>, Pending> pendingBridge_;
    std::map<std::pair<uint64_t, uint64_t>, std::pair<uint64_t, uint64_t>> pendingByBridge_;
    std::unordered_map<std::string, uint32_t> avoid_;
    std::deque<std::pair<uint32_t, uint64_t>> bridgeOpens_;
    std::unordered_map<std::string, uint32_t> envSeen_;
    uint8_t xSec_[32] = {0};

    std::vector<Addr> dhtSeeds_;
    std::mutex jobMu_;
    std::condition_variable jobCv_;
    std::deque<Job> jobs_;
    std::deque<Done> done_;
    std::atomic<bool> stop_{false};
    std::thread worker_;

    bool allow(const Addr& a);
    bool sendFrame(uint64_t connId, Lane lane, const uint8_t* body, size_t n);
    void sendFrameTo(const End& e, uint8_t kind, uint64_t circ, const uint8_t* payload, size_t n, Lane lane);
    void frame(uint64_t connId, const uint8_t* p, size_t n);
    void openLocal(uint64_t fromConn, uint64_t fromCirc, const std::string& fromPeer, const uint8_t owner[32]);
    void dropCircuit(uint64_t connId, uint64_t circ, bool notifyOther);
    void dropConn(uint64_t connId);
    void replaceLink(uint64_t oldId, uint64_t newId);
    Peer& peerFor(const Addr& a, const uint8_t pub[32]);
    void dialPeer(Peer& p);
    void sendToPeer(Peer& p, const std::vector<uint8_t>& frame);
    void workerLoop();
    uint64_t newCircuit(uint64_t connId);
    void routeOpen(uint64_t connId, uint64_t circ, const Addr& relay, const uint8_t relayPub[32], const uint8_t owner[32]);
    bool tryBridge(uint64_t connId, uint64_t circ, const Addr& relay, const uint8_t relayPub[32], const uint8_t owner[32]);
    void bridgeFallback(std::pair<uint64_t, uint64_t> key);
    void forgetPending(uint64_t connId, uint64_t circ);
    bool sealEnvelope(const uint8_t yEdPub[32], const uint8_t owner[32], uint8_t out[kEnvelopeLen]);
    bool openEnvelope(const uint8_t env[kEnvelopeLen], uint8_t owner[32]);
    void bridgeFrame(uint64_t connId, const uint8_t* p, size_t n);
    void tickBridges(uint32_t now);
};


}

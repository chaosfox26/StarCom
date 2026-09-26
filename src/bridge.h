#pragma once
#include "handshake.h"
#include "identity.h"
#include "platform_net.h"
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sc {

struct BridgeStats {
    int relays = 0;
    int pairs = 0;
    int kbps = 0;
    uint64_t bytes = 0;
    uint64_t dropped = 0;
};

class BridgeHost {
public:
    BridgeHost();
    ~BridgeHost();
    BridgeHost(const BridgeHost&) = delete;
    BridgeHost& operator=(const BridgeHost&) = delete;
    void start(int capKbps);
    void stop();
    bool running() const { return running_; }
    BridgeStats stats();

private:
    struct Link;
    using End = std::pair<int, uint64_t>;
    Identity id_;
    int capKbps_ = 2000;
    std::atomic<bool> running_{false};
    std::atomic<bool> quit_{false};
    std::vector<std::unique_ptr<Link>> links_;
    std::thread ctl_;
    std::mutex mu_;
    std::map<End, End> partner_;
    std::map<End, bool> live_;
    double tokens_ = 0;
    uint32_t lastRefill_ = 0;
    uint64_t bytes_ = 0;
    uint64_t dropped_ = 0;
    uint64_t windowBytes_ = 0;
    uint32_t windowStart_ = 0;
    int kbps_ = 0;

    void ctlLoop();
    void linkLoop(int idx);
    bool linkUp(Link& l);
    void sendHelloAll();
    bool sendFrame(Link& l, const uint8_t* p, size_t n, Lane lane);
    void dispatch(int idx, const uint8_t* p, size_t n);
    void dropLink(int idx);
    void closeEnd(const End& e);
    bool takeTokens(size_t n);
};

}

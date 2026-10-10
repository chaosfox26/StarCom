#pragma once
#include "handshake.h"
#include "identity.h"
#include "platform_net.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace sc {

class RelayAnnouncer {
public:
    RelayAnnouncer();
    ~RelayAnnouncer();
    RelayAnnouncer(const RelayAnnouncer&) = delete;
    RelayAnnouncer& operator=(const RelayAnnouncer&) = delete;
    void start(const Identity& relayId, uint16_t port);
    void stop();
    int listedBy() const;
    int entryPoints() const { return (int)links_.size(); }

private:
    struct Link {
        UdpSocket sock;
        Addr relay;
        std::atomic<bool> listed{false};
        std::thread t;
    };
    Identity id_;
    uint16_t port_ = 0;
    std::atomic<bool> quit_{false};
    std::vector<std::unique_ptr<Link>> links_;
    void linkLoop(Link& l);
    bool connect(Link& l, Session& ses);
    bool sendFrame(Link& l, Session& ses, const uint8_t* p, size_t n);
};

}

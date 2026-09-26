#pragma once
#include "handshake.h"
#include <atomic>
#include <functional>
#include <string>

namespace sc {

bool connectFriend(UdpSocket& s, const Identity& me, const uint8_t friendPub[32], const Addr& server,
                   const std::string& myCaps, bool hideIp, Session& out, std::string& peerCaps, std::string& err,
                   const std::function<void(const std::string&)>& status, const std::atomic<bool>* cancel = nullptr);
class OfflineNotice {
public:
    OfflineNotice() : sent_(false), armed_(false) {}
    ~OfflineNotice();
    OfflineNotice(const OfflineNotice&) = delete;
    OfflineNotice& operator=(const OfflineNotice&) = delete;
    bool arm(UdpSocket& s, const Identity& me, const uint8_t friendPub[32], const Addr& server);
    void send();
private:
    UdpSocket* s_ = nullptr;
    Addr server_;
    uint8_t shared_[32] = {0};
    uint8_t myPub_[32] = {0};
    std::atomic<bool> sent_;
    bool armed_;
};

}

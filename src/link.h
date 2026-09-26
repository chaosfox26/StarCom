#pragma once
#include "dht.h"
#include "handshake.h"
#include "identity.h"
#include "platform_net.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sc {

class RelayLink {
public:
    RelayLink();
    ~RelayLink();
    RelayLink(const RelayLink&) = delete;
    RelayLink& operator=(const RelayLink&) = delete;
    void start(const Identity& me, const std::string& myName);
    void stop();
    bool ready() const { return ready_; }
    bool failed() const { return failed_; }
    bool nameTaken() const { return nameTaken_; }
    std::string note() const;
    bool waitReady(int maxMs, const std::atomic<bool>* cancel);
    Addr relay() const { return relay_; }
    bool lookup(const std::string& name, const uint8_t* pin, PointerRecord& out, std::string& err);
    bool open(const PointerRecord& friendRec, UdpSocket& ch, std::string& err);
    void accept(uint64_t circuit, UdpSocket& ch);
    void close(UdpSocket& ch);
    void wake(UdpSocket& ch);
    void setIncoming(std::function<void(uint64_t)> fn);
    void setOnChange(std::function<void()> fn);

private:
    struct Circuit {
        UdpSocket* ch;
        std::shared_ptr<PacketQueue> q;
    };
    UdpSocket sock_;
    Addr relay_;
    uint8_t relayPub_[32] = {0};
    Identity id_;
    std::string name_;
    Session link_;
    std::mutex sessMu_;
    std::atomic<bool> linked_{false};
    std::shared_ptr<PacketQueue> hsQ_;
    std::shared_ptr<PacketQueue> keyQ_;
    UdpSocket hsCh_;
    std::mutex circMu_;
    std::map<uint64_t, Circuit> circuits_;
    std::mutex waitMu_;
    std::condition_variable waitCv_;
    bool registered_ = false;
    bool haveResult_ = false;
    bool resultOk_ = false;
    std::vector<uint8_t> result_;
    std::mutex lookupMu_;
    std::function<void(uint64_t)> incoming_;
    std::function<void()> onChange_;
    std::mutex incomingMu_;
    mutable std::mutex noteMu_;
    std::string note_;
    std::thread rx_;
    std::thread ctl_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> nameTaken_{false};
    std::atomic<uint32_t> lastRecvMs_{0};
    void setNote(const std::string& n);
    void rxLoop();
    void ctlLoop();
    bool linkUp(std::string& err);
    void dropCircuits();
    void dispatch(const uint8_t* p, size_t n);
    bool sendFrame(Lane lane, const uint8_t* p, size_t n);
    bool sendControl(uint8_t kind, const uint8_t* body, size_t n);
    int sendData(uint64_t circuit, const uint8_t* p, size_t n);
    bool publish(std::string& err);
};

bool connectMasked(RelayLink& link, UdpSocket& ch, const Identity& me, const std::string& friendName, const uint8_t friendPub[32],
                   const std::string& myCaps, Session& out, std::string& peerCaps, std::string& err,
                   const std::function<void(const std::string&)>& status, const std::atomic<bool>* cancel = nullptr);
bool answerMasked(RelayLink& link, uint64_t circuit, UdpSocket& ch, const Identity& me, const std::string& myCaps,
                  Session& out, std::string& peerCaps, std::string& err);

}

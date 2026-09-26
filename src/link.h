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
    Addr relay() const { return conns_[0].relay; }
    bool lookup(const std::string& name, const uint8_t* pin, PointerRecord& out, std::string& err);
    bool open(const PointerRecord& friendRec, UdpSocket& ch, std::string& err, bool masked = false);
    void accept(uint64_t circuit, UdpSocket& ch);
    void close(UdpSocket& ch);
    void wake(UdpSocket& ch);
    void setIncoming(std::function<void(uint64_t)> fn);
    void setOnChange(std::function<void()> fn);

private:
    static constexpr int kConns = 2;
    struct Circuit {
        UdpSocket* ch;
        std::shared_ptr<PacketQueue> q;
        int ci;
    };
    struct Conn {
        UdpSocket sock;
        Addr relay;
        uint8_t relayPub[32] = {0};
        Session ses;
        std::mutex sessMu;
        std::atomic<bool> used{false};
        std::atomic<bool> linked{false};
        std::atomic<bool> ready{false};
        std::shared_ptr<PacketQueue> hsQ;
        std::shared_ptr<PacketQueue> keyQ;
        UdpSocket hsCh;
        std::thread rx;
        std::atomic<uint32_t> lastRecvMs{0};
        bool registered = false;
    };
    Conn conns_[kConns];
    Identity id_;
    std::string name_;
    std::mutex circMu_;
    std::map<uint64_t, Circuit> circuits_;
    std::map<uint64_t, int> incomingConn_;
    std::mutex waitMu_;
    std::condition_variable waitCv_;
    bool haveResult_ = false;
    bool resultOk_ = false;
    std::vector<uint8_t> result_;
    std::mutex lookupMu_;
    std::function<void(uint64_t)> incoming_;
    std::function<void()> onChange_;
    std::mutex incomingMu_;
    mutable std::mutex noteMu_;
    std::string note_;
    std::thread ctl_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> nameTaken_{false};
    void setNote(const std::string& n);
    void rxLoop(int ci);
    void ctlLoop();
    bool linkUp(int ci, std::string& err);
    void linkDown(int ci);
    void dropCircuits(int ci);
    void dispatch(int ci, const uint8_t* p, size_t n);
    bool sendFrame(int ci, Lane lane, const uint8_t* p, size_t n);
    bool sendControl(int ci, uint8_t kind, const uint8_t* body, size_t n);
    int sendData(int ci, uint64_t circuit, const uint8_t* p, size_t n);
    bool publish(int ci, std::string& err);
    int primary() const;
};

bool connectMasked(RelayLink& link, UdpSocket& ch, const Identity& me, const std::string& friendName, const uint8_t friendPub[32],
                   const std::string& myCaps, Session& out, std::string& peerCaps, std::string& err,
                   const std::function<void(const std::string&)>& status, const std::atomic<bool>* cancel = nullptr, bool masked = false);
bool answerMasked(RelayLink& link, uint64_t circuit, UdpSocket& ch, const Identity& me, const std::string& myCaps,
                  Session& out, std::string& peerCaps, std::string& err);

}

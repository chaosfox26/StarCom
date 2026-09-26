#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <string>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace sc {

bool netInit();
void netShutdown();
bool sysRandom(uint8_t* out, size_t n);
int familyV4();
int familyV6();

struct Addr {
    uint8_t raw[28] = {0};
    int len = 0;
    int family() const;
    std::string str() const;
    bool operator==(const Addr& o) const;
};

bool resolve(const std::string& host, uint16_t port, std::vector<Addr>& out, std::string& err);
bool localAddressFor(const Addr& remote, uint16_t port, Addr& out);
void addrSetPort(Addr& a, uint16_t port);

struct PacketQueue {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::pair<std::vector<uint8_t>, Addr>> q;
    bool closed = false;
    void push(const uint8_t* p, size_t n, const Addr& from);
    void shut();
};

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    bool open(int family, uint16_t port, std::string& err);
    int sendTo(const uint8_t* p, size_t n, const Addr& to);
    int recvFrom(uint8_t* p, size_t cap, Addr& from, int timeoutMs);
    uint16_t localPort() const;
    void close();
    void unread(const uint8_t* p, size_t n, const Addr& from);
    void attach(UdpSocket& shared, std::shared_ptr<PacketQueue> inbox, std::function<int(const uint8_t*, size_t, const Addr&)> send = nullptr);
    bool isChannel() const { return parent_ != nullptr; }
private:
    struct Held {
        std::vector<uint8_t> data;
        Addr from;
    };
    intptr_t s_;
    std::deque<Held> held_;
    UdpSocket* parent_ = nullptr;
    std::shared_ptr<PacketQueue> inbox_;
    std::function<int(const uint8_t*, size_t, const Addr&)> send_;
};

}

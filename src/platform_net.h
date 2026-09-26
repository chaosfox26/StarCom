#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
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
private:
    intptr_t s_;
};

}

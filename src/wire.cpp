#include "wire.h"
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace sc {

const uint8_t kPrologue[] = {'S', 't', 'a', 'r', 'C', 'o', 'm', '/', '1'};
const size_t kPrologueLen = sizeof kPrologue;

void writeHeader(uint8_t* p, PacketType t, Lane l){
    p[0] = 'S';
    p[1] = 'C';
    p[2] = kWireVersion;
    p[3] = (uint8_t)t;
    p[4] = (uint8_t)l;
    p[5] = 0;
    p[6] = 0;
    p[7] = 0;
}

bool readHeader(const uint8_t* p, size_t n, Header& h){
    if(n < kHeaderLen || p[0] != 'S' || p[1] != 'C' || p[2] != kWireVersion) return false;
    if(p[3] < (uint8_t)PacketType::Hello || p[3] > (uint8_t)PacketType::Link) return false;
    if(p[4] > (uint8_t)Lane::Background || p[5] || p[6] || p[7]) return false;
    h.type = (PacketType)p[3];
    h.lane = (Lane)p[4];
    return true;
}

void putU64(uint8_t* p, uint64_t v){
    for(int i = 0; i < 8; i++){ p[i] = (uint8_t)v; v >>= 8; }
}

uint64_t getU64(const uint8_t* p){
    uint64_t v = 0;
    for(int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

namespace {

bool isV4Mapped(const uint8_t* a){
    static const uint8_t pre[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    return std::memcmp(a, pre, 12) == 0;
}

}

size_t encodeAddr(const Addr& a, uint8_t out[19]){
    std::memset(out, 0, 19);
    if(a.family() == AF_INET){
        const sockaddr_in* s = (const sockaddr_in*)a.raw;
        out[0] = 4;
        std::memcpy(out + 1, &s->sin_port, 2);
        std::memcpy(out + 3, &s->sin_addr, 4);
        return 19;
    }
    if(a.family() == AF_INET6){
        const sockaddr_in6* s = (const sockaddr_in6*)a.raw;
        const uint8_t* b = (const uint8_t*)&s->sin6_addr;
        std::memcpy(out + 1, &s->sin6_port, 2);
        if(isV4Mapped(b)){
            out[0] = 4;
            std::memcpy(out + 3, b + 12, 4);
        } else {
            out[0] = 6;
            std::memcpy(out + 3, b, 16);
        }
        return 19;
    }
    return 0;
}

bool decodeAddr(const uint8_t in[19], Addr& a){
    a = Addr();
    if(in[0] == 4){
        sockaddr_in s;
        std::memset(&s, 0, sizeof s);
        s.sin_family = AF_INET;
        std::memcpy(&s.sin_port, in + 1, 2);
        std::memcpy(&s.sin_addr, in + 3, 4);
        std::memcpy(a.raw, &s, sizeof s);
        a.len = (int)sizeof s;
        return true;
    }
    if(in[0] == 6){
        sockaddr_in6 s;
        std::memset(&s, 0, sizeof s);
        s.sin6_family = AF_INET6;
        std::memcpy(&s.sin6_port, in + 1, 2);
        std::memcpy(&s.sin6_addr, in + 3, 16);
        std::memcpy(a.raw, &s, sizeof s);
        a.len = (int)sizeof s;
        return true;
    }
    return false;
}

Addr mapForSocket(const Addr& a, int socketFamily){
    if(socketFamily != AF_INET6 || a.family() != AF_INET) return a;
    const sockaddr_in* s = (const sockaddr_in*)a.raw;
    sockaddr_in6 m;
    std::memset(&m, 0, sizeof m);
    m.sin6_family = AF_INET6;
    m.sin6_port = s->sin_port;
    uint8_t* b = (uint8_t*)&m.sin6_addr;
    b[10] = 0xff;
    b[11] = 0xff;
    std::memcpy(b + 12, &s->sin_addr, 4);
    Addr o;
    std::memcpy(o.raw, &m, sizeof m);
    o.len = (int)sizeof m;
    return o;
}

}

#include "platform_net.h"
#include <chrono>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <mswsock.h>
typedef SOCKET sock_t;
static const sock_t BAD_SOCK = INVALID_SOCKET;
static void closeSock(sock_t s){ closesocket(s); }
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <cerrno>
#include <poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
static const sock_t BAD_SOCK = -1;
static void closeSock(sock_t s){ ::close(s); }
#endif

namespace sc {

bool netInit(){
#ifdef _WIN32
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
#else
    return true;
#endif
}

void netShutdown(){
#ifdef _WIN32
    WSACleanup();
#endif
}

bool sysRandom(uint8_t* out, size_t n){
#ifdef _WIN32
    return BCryptGenRandom(nullptr, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    size_t got = 0;
    while(got < n){
        ssize_t r = getrandom(out + got, n - got, 0);
        if(r < 0){ if(errno == EINTR) continue; return false; }
        got += (size_t)r;
    }
    return true;
#endif
}

int familyV4(){ return AF_INET; }
int familyV6(){ return AF_INET6; }

int Addr::family() const {
    if(len <= 0) return 0;
    return ((const sockaddr*)raw)->sa_family;
}

std::string Addr::str() const {
    char host[INET6_ADDRSTRLEN] = {0};
    if(family() == AF_INET){
        const sockaddr_in* a = (const sockaddr_in*)raw;
        inet_ntop(AF_INET, &a->sin_addr, host, sizeof host);
        return std::string(host) + ":" + std::to_string(ntohs(a->sin_port));
    }
    if(family() == AF_INET6){
        const sockaddr_in6* a = (const sockaddr_in6*)raw;
        inet_ntop(AF_INET6, &a->sin6_addr, host, sizeof host);
        return "[" + std::string(host) + "]:" + std::to_string(ntohs(a->sin6_port));
    }
    return "?";
}

bool Addr::operator==(const Addr& o) const {
    return len == o.len && std::memcmp(raw, o.raw, (size_t)len) == 0;
}

bool resolve(const std::string& host, uint16_t port, std::vector<Addr>& out, std::string& err){
    addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    std::string ps = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), ps.c_str(), &hints, &res);
    if(rc != 0){ err = "lookup failed for " + host; return false; }
    for(addrinfo* p = res; p; p = p->ai_next){
        if((p->ai_family != AF_INET && p->ai_family != AF_INET6) || p->ai_addrlen > sizeof(Addr::raw)) continue;
        Addr a;
        std::memcpy(a.raw, p->ai_addr, p->ai_addrlen);
        a.len = (int)p->ai_addrlen;
        bool dup = false;
        for(const Addr& b : out) if(b == a) dup = true;
        if(!dup) out.push_back(a);
    }
    freeaddrinfo(res);
    if(out.empty()){ err = "no A/AAAA records for " + host; return false; }
    return true;
}

bool localAddressFor(const Addr& remote, uint16_t port, Addr& out){
    sock_t s = ::socket(remote.family(), SOCK_DGRAM, IPPROTO_UDP);
    if(s == BAD_SOCK) return false;
    bool ok = ::connect(s, (const sockaddr*)remote.raw, remote.len) == 0;
    sockaddr_storage ss;
    socklen_t sl = sizeof ss;
    if(ok) ok = getsockname(s, (sockaddr*)&ss, &sl) == 0;
    closeSock(s);
    if(!ok) return false;
    size_t cl = (size_t)sl < sizeof out.raw ? (size_t)sl : sizeof out.raw;
    std::memcpy(out.raw, &ss, cl);
    out.len = (int)cl;
    addrSetPort(out, port);
    return true;
}

void addrSetPort(Addr& a, uint16_t port){
    if(a.family() == AF_INET) ((sockaddr_in*)a.raw)->sin_port = htons(port);
    else if(a.family() == AF_INET6) ((sockaddr_in6*)a.raw)->sin6_port = htons(port);
}

UdpSocket::UdpSocket() : s_((intptr_t)BAD_SOCK) {}
UdpSocket::~UdpSocket(){ close(); }

bool UdpSocket::open(int family, uint16_t port, std::string& err){
    close();
    sock_t s = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if(s == BAD_SOCK){ err = "cannot create a UDP socket"; return false; }
#ifdef _WIN32
    {
        BOOL off = FALSE;
        DWORD ret = 0;
        WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, nullptr, 0, &ret, nullptr, nullptr);
    }
#endif
    if(family == AF_INET6){
        int off = 0;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof off);
        sockaddr_in6 a;
        std::memset(&a, 0, sizeof a);
        a.sin6_family = AF_INET6;
        a.sin6_port = htons(port);
        a.sin6_addr = in6addr_any;
        if(::bind(s, (sockaddr*)&a, sizeof a) != 0){ closeSock(s); err = "cannot bind UDP port " + std::to_string(port); return false; }
    } else {
        sockaddr_in a;
        std::memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if(::bind(s, (sockaddr*)&a, sizeof a) != 0){ closeSock(s); err = "cannot bind UDP port " + std::to_string(port); return false; }
    }
    s_ = (intptr_t)s;
    return true;
}

void PacketQueue::push(const uint8_t* p, size_t n, const Addr& from){
    {
        std::lock_guard<std::mutex> g(mu);
        if(closed) return;
        if(q.size() >= 256) q.pop_front();
        q.emplace_back(std::vector<uint8_t>(p, p + n), from);
    }
    cv.notify_one();
}

void PacketQueue::shut(){
    {
        std::lock_guard<std::mutex> g(mu);
        closed = true;
    }
    cv.notify_all();
}

void UdpSocket::attach(UdpSocket& shared, std::shared_ptr<PacketQueue> inbox, std::function<int(const uint8_t*, size_t, const Addr&)> send){
    close();
    parent_ = &shared;
    inbox_ = std::move(inbox);
    send_ = std::move(send);
}

int UdpSocket::sendTo(const uint8_t* p, size_t n, const Addr& to){
    if(send_) return send_(p, n, to);
    UdpSocket* par = parent_;
    if(par) return par->sendTo(p, n, to);
    if((sock_t)s_ == BAD_SOCK) return -1;
    return (int)::sendto((sock_t)s_, (const char*)p, (int)n, 0, (const sockaddr*)to.raw, to.len);
}

int UdpSocket::recvFrom(uint8_t* p, size_t cap, Addr& from, int timeoutMs){
    if(!held_.empty()){
        Held h = held_.front();
        held_.pop_front();
        size_t n = h.data.size() < cap ? h.data.size() : cap;
        if(n) std::memcpy(p, h.data.data(), n);
        from = h.from;
        return (int)n;
    }
    std::shared_ptr<PacketQueue> box = inbox_;
    if(box){
        std::unique_lock<std::mutex> g(box->mu);
        box->cv.wait_for(g, std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs), [&box]{ return box->closed || !box->q.empty(); });
        if(box->q.empty()) return box->closed ? -1 : 0;
        auto pk = std::move(box->q.front());
        box->q.pop_front();
        size_t n = pk.first.size() < cap ? pk.first.size() : cap;
        if(n) std::memcpy(p, pk.first.data(), n);
        from = pk.second;
        return (int)n;
    }
    if((sock_t)s_ == BAD_SOCK) return -1;
#ifdef _WIN32
    WSAPOLLFD pf;
    pf.fd = (sock_t)s_;
    pf.events = POLLRDNORM;
    pf.revents = 0;
    int pr = WSAPoll(&pf, 1, timeoutMs);
#else
    pollfd pf;
    pf.fd = (sock_t)s_;
    pf.events = POLLIN;
    pf.revents = 0;
    int pr = ::poll(&pf, 1, timeoutMs);
#endif
    if(pr <= 0) return 0;
    sockaddr_storage ss;
    socklen_t sl = sizeof ss;
    int r = (int)::recvfrom((sock_t)s_, (char*)p, (int)cap, 0, (sockaddr*)&ss, &sl);
    if(r < 0) return -1;
    size_t cl = (size_t)sl < sizeof from.raw ? (size_t)sl : sizeof from.raw;
    std::memcpy(from.raw, &ss, cl);
    from.len = (int)cl;
    return r;
}

uint16_t UdpSocket::localPort() const {
    const UdpSocket* par = parent_;
    if(par) return par->localPort();
    if((sock_t)s_ == BAD_SOCK) return 0;
    sockaddr_storage ss;
    socklen_t sl = sizeof ss;
    if(getsockname((sock_t)s_, (sockaddr*)&ss, &sl) != 0) return 0;
    if(ss.ss_family == AF_INET) return ntohs(((sockaddr_in*)&ss)->sin_port);
    if(ss.ss_family == AF_INET6) return ntohs(((sockaddr_in6*)&ss)->sin6_port);
    return 0;
}

void UdpSocket::unread(const uint8_t* p, size_t n, const Addr& from){
    if(held_.size() >= 32 || n == 0) return;
    Held h;
    h.data.assign(p, p + n);
    h.from = from;
    held_.push_back(h);
}

void UdpSocket::close(){
    held_.clear();
    parent_ = nullptr;
    inbox_.reset();
    send_ = nullptr;
    if((sock_t)s_ != BAD_SOCK){ closeSock((sock_t)s_); s_ = (intptr_t)BAD_SOCK; }
}

}

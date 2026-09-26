#pragma once
#include "identity.h"
#include "noise.h"
#include "platform_net.h"
#include "wire.h"
#include <cstdint>
#include <string>

namespace sc {

struct Session {
    uint64_t id = 0;
    uint64_t peerId = 0;
    uint8_t peerPub[32] = {0};
    Addr peer;
    NoiseChannel channel;
};

bool hsInitiate(UdpSocket& s, const Addr& to, const Identity& me, const uint8_t peerPub[32], const std::string& myCaps, Session& out, std::string& peerCaps, std::string& err, int timeoutMs);
bool hsRespond(UdpSocket& s, const Identity& me, const std::string& myCaps, Session& out, std::string& peerCaps, std::string& err, int timeoutMs);
bool sessionSeal(Session& s, Lane lane, const uint8_t* plain, size_t n, uint8_t* out, size_t cap, size_t& outLen);
bool sessionOpen(Session& s, const uint8_t* pkt, size_t n, const Addr& from, Lane& lane, uint8_t* out, size_t cap, size_t& outLen);
void sessionWipe(Session& s);

}

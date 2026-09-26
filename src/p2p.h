#pragma once
#include "handshake.h"
#include <functional>
#include <string>

namespace sc {

bool connectFriend(UdpSocket& s, const Identity& me, const uint8_t friendPub[32], const Addr& server,
                   const std::string& myCaps, bool hideIp, Session& out, std::string& peerCaps, std::string& err,
                   const std::function<void(const std::string&)>& status);

}

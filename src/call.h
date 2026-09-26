#pragma once
#include "handshake.h"
#include <string>

namespace sc {

int runCall(UdpSocket& s, Session& ses, const std::string& peerTag);

}

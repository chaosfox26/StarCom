#pragma once
#include "handshake.h"
#include <functional>
#include <string>

namespace sc {

constexpr size_t kMaxChatText = 1200;

struct ChatIO {
    std::function<bool(std::string&)> nextOutgoing;
    std::function<void(const std::string&)> onIncoming;
    std::function<void(const std::string&)> onStatus;
};

bool runChat(UdpSocket& s, Session& ses, ChatIO& io, std::string& err);

}

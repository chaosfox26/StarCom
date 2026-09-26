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

class Engine;

class ChatModule {
public:
    ChatModule(Engine& e, ChatIO& io);
    ~ChatModule();
    ChatModule(const ChatModule&) = delete;
    ChatModule& operator=(const ChatModule&) = delete;
    void tick();
    bool ended() const;
    bool failed() const;
    const std::string& error() const;
    struct Impl;
private:
    Impl* p_;
};

bool runChat(UdpSocket& s, Session& ses, ChatIO& io, std::string& err);
bool runChatWith(Engine& e, ChatIO& io, std::string& err);

}

#pragma once
#include "../identity.h"
#include "../platform_net.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sc {

struct ChatLine {
    bool mine = false;
    bool system = false;
    std::string text;
    std::string time;
    float cachedWidth = -1;
    float cachedHeight = 0;
};

enum class LinkState { Idle, Connecting, Direct, Relay, Ended };

struct Friend {
    std::string nick;
    std::string code;
    uint8_t pub[32] = {0};
    std::vector<ChatLine> lines;
    LinkState state = LinkState::Idle;
    std::string status;
    int unread = 0;
};

class NetHub {
public:
    NetHub();
    ~NetHub();
    NetHub(const NetHub&) = delete;
    NetHub& operator=(const NetHub&) = delete;
    bool start(std::string& err);
    const std::string& myCode() const { return myCode_; }
    std::vector<Friend>& friends() { return friends_; }
    bool addFriend(const std::string& code, const std::string& nick, std::string& err);
    void open(size_t idx);
    void send(size_t idx, const std::string& text);
    void close(size_t idx);
    bool pump();
    void shutdown();
    std::function<void()> wake;
    std::string serverNote() const { return serverNote_; }

private:
    struct Event { size_t idx; int kind; std::string text; };
    struct Worker;
    Identity id_;
    bool haveId_ = false;
    std::string myCode_;
    std::string server_;
    bool hideIp_ = false;
    std::string serverNote_;
    std::vector<Friend> friends_;
    std::vector<std::shared_ptr<Worker>> workers_;
    std::mutex mu_;
    std::deque<Event> events_;
    void post(size_t idx, int kind, const std::string& text);
    void saveFriends();
    void run(size_t idx, std::shared_ptr<Worker> w);
};

}

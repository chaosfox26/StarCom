#pragma once
#include "../handshake.h"
#include "../identity.h"
#include "../link.h"
#include "../platform_net.h"
#include "../voicecall.h"
#include <condition_variable>
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

enum class LinkState { Idle, Connecting, Connected, Ended };

struct CallView {
    CallState state = CallState::Idle;
    std::string note;
    int seconds = 0;
    int ping = 0;
    bool muted = false;
    bool deafened = false;
};

enum class CallAction { Call, Accept, Hangup, ToggleMute, ToggleDeafen };

struct RequestView {
    std::string hint;
    std::string code;
    bool checking = false;
};

struct Friend {
    std::string nick;
    std::string code;
    std::string name;
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
    const std::string& myName() const { return myName_; }
    const std::string& dataFolder() const { return dataFolder_; }
    bool setMyName(const std::string& in, std::string& err);
    std::string networkNote() const { return link_.note(); }
    bool networkReady() const { return link_.ready(); }
    bool nameTaken() const { return link_.nameTaken(); }
    std::vector<Friend>& friends() { return friends_; }
    void find(const std::string& nameIn);
    bool finding() const { return finding_; }
    const std::string& foundName() const { return foundName_; }
    const std::string& foundCode() const { return foundCode_; }
    const std::string& foundError() const { return foundErr_; }
    void clearFound();
    bool addFound(const std::string& nick, std::string& err);
    void open(size_t idx);
    void send(size_t idx, const std::string& text);
    void close(size_t idx);
    CallView callView(size_t idx);
    std::vector<RequestView> requests() const;
    void acceptRequest(size_t i);
    void ignoreRequest(size_t i);
    const std::string& requestNote() const { return requestNote_; }
    void callAction(size_t idx, CallAction a);
    bool pump();
    void shutdown();
    std::function<void()> wake;

private:
    struct Worker;
    struct Adopt;
    struct Request {
        uint8_t key[32];
        std::string hint;
        std::string code;
        std::unique_ptr<UdpSocket> ch;
        uint32_t expiresMs = 0;
        bool checking = false;
    };
    struct Event {
        size_t idx;
        int kind;
        std::string text;
        uint64_t gen;
        std::shared_ptr<Adopt> adopt;
    };
    Identity id_;
    bool haveId_ = false;
    std::string myCode_;
    std::string myName_;
    std::string dataFolder_;
    std::string myCaps_;
    RelayLink link_;
    std::vector<Friend> friends_;
    std::vector<std::shared_ptr<Worker>> workers_;
    std::vector<std::shared_ptr<Worker>> retired_;
    uint64_t nextGen_ = 1;
    std::mutex mu_;
    std::deque<Event> events_;
    std::mutex pubsMu_;
    std::vector<std::vector<uint8_t>> pubs_;
    std::mutex inMu_;
    std::condition_variable inCv_;
    std::deque<uint64_t> inQ_;
    std::thread acceptor_;
    std::thread finder_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> finding_{false};
    std::string foundName_, foundCode_, foundErr_;
    std::vector<Request> requests_;
    std::string requestNote_;
    std::thread checker_;
    std::atomic<bool> checkerBusy_{false};
    uint8_t foundPub_[32] = {0};
    void post(size_t idx, int kind, const std::string& text, uint64_t gen, std::shared_ptr<Adopt> adopt = nullptr);
    void saveFriends();
    void syncPubs();
    void acceptLoop();
    void startWorker(size_t idx, std::shared_ptr<Adopt> adopt);
    void run(size_t idx, std::shared_ptr<Worker> w);
    void chatOn(size_t idx, std::shared_ptr<Worker> w, Session& ses);
    void reap();
};

}

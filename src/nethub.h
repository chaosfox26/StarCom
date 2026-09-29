#pragma once
#include "audio.h"
#include "bridge.h"
#include "handshake.h"
#include "hub.h"
#include "identity.h"
#include "link.h"
#include "platform_net.h"
#include "voice.h"
#include "voicecall.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sc {

struct ChatLine {
    bool mine = false;
    bool system = false;
    std::string who;
    uint32_t whoColour = 0;
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
    bool inHub = false;
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
    bool met = false;
};

struct HubChannelLines {
    uint64_t channel = 0;
    std::vector<ChatLine> lines;
    uint64_t trimmed = 0;
    int unread = 0;
};

struct HubState {
    HubView view;
    std::vector<HubChannelLines> chans;
    int unread = 0;
};

struct HubVoiceView {
    bool active = false;
    uint8_t hub[32] = {0};
    uint64_t channel = 0;
    std::string channelName;
    std::vector<std::string> names;
    bool muted = false;
    bool deafened = false;
    int ping = 0;
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
    void callAction(size_t idx, CallAction a);
    std::vector<RequestView> requests() const;
    void acceptRequest(size_t i);
    void ignoreRequest(size_t i);
    const std::string& requestNote() const { return requestNote_; }

    std::vector<HubState>& hubs() { return hubs_; }
    const std::string& hubNote() const { return hubNote_; }
    bool hubNoteError() const { return hubNoteErr_; }
    const std::string& lastInvite() const { return invite_; }
    void clearHubNote() { hubNote_.clear(); invite_.clear(); }
    void createHub(const std::string& name);
    void joinHub(const std::string& invite);
    void makeInvite(const uint8_t hub[32]);
    void postHub(const uint8_t hub[32], uint64_t channel, const std::string& text);
    void manageHub(const uint8_t hub[32], ManageOp op, const ManageArgs& args);
    void leaveHub(const uint8_t hub[32]);
    void voiceJoin(const uint8_t hub[32], uint64_t channel);
    void voiceLeave();
    HubVoiceView hubVoice();
    void hubVoiceMute();
    void hubVoiceDeafen();

    bool volunteer() const { return volunteer_; }
    bool setVolunteer(bool on, std::string& err);
    BridgeStats bridgeStats() { return bridge_.stats(); }

    bool pump();
    void shutdown();
    std::function<void()> wake;

private:
    struct Peer;
    struct Adopt;
    struct Request {
        uint8_t key[32];
        std::string hint;
        std::string code;
        std::unique_ptr<UdpSocket> ch;
        uint32_t expiresMs = 0;
        bool checking = false;
        bool onSession = false;
    };
    struct Event {
        std::string key;
        int kind;
        std::string text;
        uint64_t gen;
        std::shared_ptr<Adopt> adopt;
    };
    struct VoicePeer {
        std::string key;
        uint8_t outStream = 0;
        int inStream = -1;
        std::shared_ptr<VoicePlayout> play;
    };

    Identity id_;
    bool haveId_ = false;
    std::string myCode_;
    std::string myName_;
    std::string dataFolder_;
    std::string myCaps_;
    mutable std::mutex capsMu_;
    RelayLink link_;
    std::vector<Friend> friends_;
    std::mutex peersMu_;
    std::map<std::string, std::shared_ptr<Peer>> peers_;
    std::vector<std::shared_ptr<Peer>> retired_;
    uint64_t nextGen_ = 1;
    std::mutex mu_;
    std::deque<Event> events_;
    std::mutex pubsMu_;
    std::vector<std::string> friendKeys_;
    std::vector<std::string> metKeys_;
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

    std::thread hubThread_;
    std::mutex hubMu_;
    std::condition_variable hubCv_;
    std::deque<std::function<void(HubCore&)>> hubJobs_;
    std::mutex snapMu_;
    std::vector<HubView> hubSnap_;
    std::deque<HubEvent> hubEvents_;
    std::vector<HubState> hubs_;
    std::string hubNote_;
    bool hubNoteErr_ = true;
    std::string invite_;
    std::atomic<bool> nameChanged_{false};
    std::map<std::string, std::pair<int, std::string>> hubUp_;

    BridgeHost bridge_;
    bool volunteer_ = false;

    std::mutex voiceMu_;
    bool voiceOn_ = false;
    uint8_t voiceHub_[32] = {0};
    uint64_t voiceChan_ = 0;
    std::vector<VoicePeer> voicePeers_;
    AudioIO audio_;
    VoiceEncoder enc_;
    std::atomic<bool> vMuted_{false};
    std::atomic<bool> vDeafened_{false};
    std::string voiceChanName_;
    std::vector<std::string> voiceNames_;

    void post(const std::string& key, int kind, const std::string& text, uint64_t gen, std::shared_ptr<Adopt> adopt = nullptr);
    void saveFriends();
    void syncFriendKeys();
    bool isFriendKey(const std::string& key);
    bool isMetKey(const std::string& key);
    size_t friendIdx(const std::string& key) const;
    void acceptLoop();
    std::shared_ptr<Peer> startPeer(const std::string& key, const std::string& name, const std::string& capsHint, std::shared_ptr<Adopt> adopt);
    void ensurePeer(const std::string& key, const std::string& name, const std::string& capsHint, bool forHub);
    std::shared_ptr<Peer> findPeer(const std::string& key);
    void run(std::shared_ptr<Peer> p);
    void reap();
    void hubLoop();
    void hubJob(std::function<void(HubCore&)> job);
    bool hubSync(std::function<bool(HubCore&)> fn);
    bool sendHub(const uint8_t peer[32], const uint8_t* p, size_t n);
    void hubVoiceIn(const std::string& key, const uint8_t* p, size_t n);
    void refreshVoice(HubCore& h);
    void startAudio();
    void stopAudio();
    void voiceOff(HubCore& c);
    void tuneVoice();
    std::string capsNow() const;
    std::string nameNow() const;
    void applyHubSnapshot();
};

}

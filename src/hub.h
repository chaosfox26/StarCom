#pragma once
#include "identity.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sc {

constexpr uint8_t kChanHub = 4;
constexpr uint8_t kChanHubVoice = 5;
constexpr size_t kMaxHubMembers = 100;
constexpr size_t kMaxVoiceChannelPeople = 8;
constexpr size_t kMaxHubText = 1100;
constexpr size_t kMaxHubChannels = 24;
constexpr size_t kMaxHubBans = 255;

enum class ChannelKind : uint8_t { Text = 1, Voice = 2 };
enum class HubRole : uint8_t { Member = 0, Admin = 1, Owner = 2 };
enum class ManageOp : uint8_t { Rename = 1, AddChannel, RenameChannel, RemoveChannel, SetRole, Kick, Ban, Unban };

struct HubChannelView {
    uint64_t id = 0;
    std::string name;
    ChannelKind kind = ChannelKind::Text;
};

struct HubMemberView {
    uint8_t key[32] = {0};
    std::string name;
    HubRole role = HubRole::Member;
    bool online = false;
    uint64_t voiceChannel = 0;
};

struct HubView {
    uint8_t id[32] = {0};
    std::string name;
    uint32_t colour = 0;
    std::vector<HubChannelView> channels;
    std::vector<HubMemberView> members;
    std::vector<HubMemberView> banned;
    HubRole myRole = HubRole::Member;
};

struct HubEvent {
    enum Kind { Message, Presence, ManifestChanged, VoiceMembers, Joined, Left, Error } kind = Message;
    uint8_t hub[32] = {0};
    uint64_t channel = 0;
    uint8_t author[32] = {0};
    std::string text;
    uint64_t ts = 0;
};

struct ManageArgs {
    std::string name;
    ChannelKind kind = ChannelKind::Text;
    uint64_t channel = 0;
    uint8_t member[32] = {0};
    HubRole role = HubRole::Member;
};

using HubSendFn = std::function<bool(const uint8_t peer[32], const uint8_t* p, size_t n)>;
using HubWantFn = std::function<void(const uint8_t peer[32], const std::string& name, const std::string& capsHint)>;

class HubCore {
public:
    HubCore(const Identity& me, const std::string& myName, const std::string& dataDir);
    ~HubCore();
    HubCore(const HubCore&) = delete;
    HubCore& operator=(const HubCore&) = delete;

    void setSender(HubSendFn send);
    void setWanted(HubWantFn want);

    void peerUp(const uint8_t peer[32], const std::string& verifiedName);
    void peerDown(const uint8_t peer[32]);
    void onFrame(const uint8_t peer[32], const uint8_t* p, size_t n);
    void tick(uint32_t nowMs);

    bool knows(const uint8_t key[32]) const;
    bool uses(const uint8_t peer[32]) const;
    bool expectsJoin(const uint8_t key[32], const std::string& invHint) const;

    bool create(const std::string& name, uint8_t hubOut[32], std::string& err);
    std::string makeInvite(const uint8_t hub[32], int maxUses, uint32_t lifeSec, std::string& err);
    bool join(const std::string& invite, std::string& err);
    bool leave(const uint8_t hub[32], std::string& err);
    bool post(const uint8_t hub[32], uint64_t channel, const std::string& text, std::string& err);
    bool manage(const uint8_t hub[32], ManageOp op, const ManageArgs& args, std::string& err);

    bool voiceJoin(const uint8_t hub[32], uint64_t voiceChannel, std::string& err);
    void voiceLeave();
    bool voiceStreamOut(const uint8_t peer[32], uint8_t& streamId) const;
    bool voiceStreamIn(const uint8_t peer[32], uint8_t streamId, uint8_t hubOut[32], uint64_t& voiceChannel) const;

    std::vector<HubEvent> drain();
    std::vector<HubView> hubs() const;

    struct Impl;
private:
    Impl* p_;
};

}

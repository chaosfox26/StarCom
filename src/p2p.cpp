#include "p2p.h"
#include "rendezvous.h"
#include "../third_party/monocypher/monocypher.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace sc {

namespace {

using Clock = std::chrono::steady_clock;

long long msSince(Clock::time_point t){
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

bool sameAddr(const Addr& a, const Addr& b){
    return a == b;
}

void addUnique(std::vector<Addr>& v, const Addr& a){
    if(a.len <= 0) return;
    for(const Addr& b : v) if(sameAddr(a, b)) return;
    v.push_back(a);
}

bool drainServer(UdpSocket& s, const Addr& server, uint8_t wantOp, Addr& observed, std::vector<uint8_t>& blob, int timeoutMs){
    auto until = Clock::now() + std::chrono::milliseconds(timeoutMs);
    uint8_t in[kMaxPacket];
    while(Clock::now() < until){
        Addr from;
        int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
        int r = s.recvFrom(in, sizeof in, from, left > 0 ? left : 0);
        if(r <= 0) continue;
        if(!sameAddr(from, server)) continue;
        uint8_t op = 0;
        Addr obs;
        const uint8_t* b = nullptr;
        size_t bl = 0;
        if(!parseRendezvous(in, (size_t)r, op, obs, b, bl)) continue;
        if(op == wantOp || (wantOp == kRvFound && op == kRvMissing)){
            observed = obs;
            blob.assign(b, b + bl);
            return op == wantOp;
        }
    }
    return false;
}

int waitBind(UdpSocket& s, const Addr& server, int timeoutMs){
    auto until = Clock::now() + std::chrono::milliseconds(timeoutMs);
    uint8_t in[kMaxPacket];
    while(Clock::now() < until){
        Addr from;
        int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
        int r = s.recvFrom(in, sizeof in, from, left > 0 ? left : 0);
        if(r <= 0 || !sameAddr(from, server)) continue;
        uint8_t count = 0;
        if(parseRelayReply(in, (size_t)r, count)) return count;
    }
    return 0;
}

bool handshakeWith(UdpSocket& s, const Identity& me, const uint8_t friendPub[32], bool initiator,
                   const std::vector<Addr>& targets, bool punchTargets, const std::string& myCaps, Session& out,
                   std::string& peerCaps, std::string& err, int totalMs){
    auto start = Clock::now();
    if(initiator){
        while(msSince(start) < totalMs){
            for(const Addr& t : targets){
                if(msSince(start) >= totalMs) break;
                Session ses;
                std::string caps, e;
                if(hsInitiate(s, t, me, friendPub, myCaps, ses, caps, e, 2500)){
                    out = ses;
                    sessionWipe(ses);
                    peerCaps = caps;
                    return true;
                }
                err = e;
            }
        }
        return false;
    }
    std::atomic<bool> stop(false);
    std::thread punch([&]{
        uint8_t pkt[kHeaderLen + 1];
        writeHeader(pkt, PacketType::Rendezvous, Lane::Signal);
        pkt[kHeaderLen] = 0;
        while(!stop && punchTargets){
            for(const Addr& t : targets) s.sendTo(pkt, sizeof pkt, t);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });
    bool ok = false;
    while(!ok && msSince(start) < totalMs){
        Session ses;
        std::string caps, e;
        int left = totalMs - (int)msSince(start);
        if(!hsRespond(s, me, myCaps, ses, caps, e, left > 0 ? left : 0, friendPub)){ err = e; break; }
        if(crypto_verify32(ses.peerPub, friendPub) != 0){ sessionWipe(ses); continue; }
        out = ses;
        sessionWipe(ses);
        peerCaps = caps;
        ok = true;
    }
    stop = true;
    punch.join();
    return ok;
}

const uint64_t kLiveMs = 45000;
const int kRefreshMs = 10000;

uint64_t wallMs(){
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

class Presence {
public:
    Presence(UdpSocket& s, const Identity& me, const uint8_t friendPub[32], const Addr& server)
        : s_(s), me_(me), server_(server) {
        std::memcpy(friend_, friendPub, 32);
        ok_ = friendSecret(me, friendPub, shared_);
    }
    ~Presence(){ crypto_wipe(shared_, sizeof shared_); }
    bool ok() const { return ok_; }
    const uint8_t* shared() const { return shared_; }
    void setCandidates(const std::vector<Addr>& c){ cands_ = c; }

    void put(bool offline){
        uint8_t mine[32], note[kMaxNote], pkt[kMaxPacket];
        mailboxId(shared_, currentHour(), me_.pub, mine);
        std::vector<Addr> none;
        size_t nl = sealNote(shared_, me_.pub, mine, offline ? none : cands_, note, sizeof note);
        if(!nl) return;
        size_t pn = buildPut(mine, note, nl, pkt, sizeof pkt);
        for(int i = 0; i < (offline ? 2 : 1); i++) s_.sendTo(pkt, pn, server_);
        lastPut_ = Clock::now();
        havePut_ = true;
    }

    void refresh(){
        if(!havePut_ || msSince(lastPut_) >= kRefreshMs) put(false);
    }

    int poll(std::vector<Addr>& out){
        uint8_t pkt[kMaxPacket];
        std::vector<uint8_t> blob;
        uint64_t h = currentHour();
        uint64_t inHour = (wallMs() / 1000) % 3600;
        uint64_t lo = inHour < 120 ? h - 1 : h, hi = inHour >= 3480 ? h + 1 : h;
        for(uint64_t hh = lo; hh <= hi; hh++){
            uint8_t theirs[32];
            mailboxId(shared_, hh, friend_, theirs);
            size_t gn = buildGet(theirs, pkt, sizeof pkt);
            s_.sendTo(pkt, gn, server_);
            Addr obs;
            if(!drainServer(s_, server_, kRvFound, obs, blob, 300)) continue;
            uint64_t stamp = 0;
            std::vector<Addr> cands;
            if(!openNote(shared_, friend_, theirs, blob.data(), blob.size(), stamp, cands) || !noteFresh(stamp, newest_)) continue;
            newest_ = stamp;
            friendOffline_ = cands.empty();
            friendCands_.clear();
            for(const Addr& c : cands){
                Addr m = mapForSocket(c, server_.family());
                if(m.len > 0 && m.family() == server_.family()) addUnique(friendCands_, m);
            }
        }
        if(newest_ == 0) return 0;
        if(friendOffline_) return -1;
        if(!friendLive()) return 0;
        out = friendCands_;
        return friendCands_.empty() ? 0 : 1;
    }

    bool friendLive() const {
        if(newest_ == 0 || friendOffline_) return false;
        uint64_t now = wallMs();
        return now <= newest_ || now - newest_ <= kLiveMs;
    }

    uint64_t friendAgeSec() const {
        uint64_t now = wallMs();
        return newest_ == 0 || now <= newest_ ? 0 : (now - newest_) / 1000;
    }

private:
    UdpSocket& s_;
    const Identity& me_;
    Addr server_;
    uint8_t friend_[32] = {0};
    uint8_t shared_[32] = {0};
    bool ok_ = false;
    std::vector<Addr> cands_;
    std::vector<Addr> friendCands_;
    uint64_t newest_ = 0;
    bool friendOffline_ = false;
    bool havePut_ = false;
    Clock::time_point lastPut_ = Clock::now();
};

}

void announceOffline(UdpSocket& s, const Identity& me, const uint8_t friendPub[32], const Addr& server){
    Presence pr(s, me, friendPub, server);
    if(pr.ok()) pr.put(true);
}

bool connectFriend(UdpSocket& s, const Identity& me, const uint8_t friendPub[32], const Addr& server,
                   const std::string& myCaps, bool hideIp, Session& out, std::string& peerCaps, std::string& err,
                   const std::function<void(const std::string&)>& status){
    Presence pr(s, me, friendPub, server);
    if(!pr.ok()){ err = "could not derive the shared secret with that friend"; return false; }
    const bool initiator = std::memcmp(me.pub, friendPub, 32) < 0;
    uint8_t pkt[kMaxPacket];
    std::vector<uint8_t> blob;
    Addr observed;
    uint8_t probe[32];
    mailboxId(pr.shared(), currentHour(), me.pub, probe);
    size_t n = buildGet(probe, pkt, sizeof pkt);
    bool reached = false;
    for(int i = 0; i < 5 && !reached; i++){
        s.sendTo(pkt, n, server);
        drainServer(s, server, kRvFound, observed, blob, 1000);
        reached = observed.len > 0;
    }
    if(!reached){ err = "the meeting-point server did not answer (check rendezvous= in starcom.conf)"; return false; }
    status("your public address as seen by the meeting point: " + observed.str());

    std::vector<Addr> myCands;
    if(!hideIp){
        addUnique(myCands, observed);
        Addr lan;
        if(localAddressFor(server, s.localPort(), lan)) addUnique(myCands, lan);
    } else {
        addUnique(myCands, server);
        status("Hide my IP is on: your addresses are not put in your note, and only the relay is used");
    }
    pr.setCandidates(myCands);

    auto start = Clock::now();
    std::vector<Addr> theirCands;
    bool saidOffline = false;
    status("waiting for your friend to come online (up to 2 minutes)...");
    while(msSince(start) < 120000){
        auto turn = Clock::now();
        pr.refresh();
        int st = pr.poll(theirCands);
        if(st == 1) break;
        if(st == -1 && !saidOffline){ status("your friend is not in the chat right now; still waiting..."); saidOffline = true; }
        if(st != -1) saidOffline = false;
        long long spent = msSince(turn);
        if(spent < 1000) std::this_thread::sleep_for(std::chrono::milliseconds(1000 - spent));
    }
    if(theirCands.empty() || !pr.friendLive()){ err = "your friend did not come online within 2 minutes"; return false; }

    std::vector<Addr> targets;
    for(const Addr& c : theirCands) if(!(c == observed)) addUnique(targets, c);
    bool friendHides = true;
    for(const Addr& c : theirCands) if(!(c == server)) friendHides = false;
    if(!hideIp && !friendHides){
        status(std::string("friend found; trying a direct connection (") + (initiator ? "calling" : "answering") + ")...");
        pr.put(false);
        if(handshakeWith(s, me, friendPub, initiator, targets, true, myCaps, out, peerCaps, err, 10000)){
            status("direct connection established: your friend can see your public IP address");
            return true;
        }
    }

    status(hideIp || friendHides ? "using the relay (one of you has Hide my IP on)..." : "direct connection failed; switching to the relay (it forwards only encrypted packets)...");
    uint8_t token[32];
    relayToken(pr.shared(), currentHour(), token);
    size_t bn = buildBind(token, pkt, sizeof pkt);
    crypto_wipe(token, sizeof token);
    int bound = 0;
    auto relayStart = Clock::now();
    while(bound < 2 && msSince(relayStart) < 120000){
        s.sendTo(pkt, bn, server);
        int b = waitBind(s, server, 1000);
        if(b > bound) bound = b;
        if(bound >= 2) break;
        pr.refresh();
        std::vector<Addr> ignore;
        pr.poll(ignore);
        if(!pr.friendLive()){
            err = "your friend left before reaching the relay (their last sign of life was " + std::to_string(pr.friendAgeSec()) + " s ago)";
            return false;
        }
    }
    if(bound == 0){ err = "the relay did not answer"; return false; }
    if(bound < 2){ err = "your friend never reached the relay"; return false; }
    std::vector<Addr> relayOnly{ server };
    if(handshakeWith(s, me, friendPub, initiator, relayOnly, false, myCaps, out, peerCaps, err, 30000)){
        status("connected through the relay: your friend cannot see your IP address; the relay sees both addresses, never the content");
        return true;
    }
    if(err.empty()) err = "could not connect, even through the relay";
    return false;
}

}

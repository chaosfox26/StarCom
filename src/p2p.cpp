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

}

bool connectFriend(UdpSocket& s, const Identity& me, const uint8_t friendPub[32], const Addr& server,
                   const std::string& myCaps, bool hideIp, Session& out, std::string& peerCaps, std::string& err,
                   const std::function<void(const std::string&)>& status){
    uint8_t shared[32];
    if(!friendSecret(me, friendPub, shared)){ err = "could not derive the shared secret with that friend"; return false; }
    const bool initiator = std::memcmp(me.pub, friendPub, 32) < 0;
    uint8_t pkt[kMaxPacket];
    std::vector<uint8_t> blob;
    Addr observed;
    uint64_t hour = currentHour();
    uint8_t mine[32];
    mailboxId(shared, hour, me.pub, mine);

    size_t n = buildGet(mine, pkt, sizeof pkt);
    bool reached = false;
    for(int i = 0; i < 5 && !reached; i++){
        s.sendTo(pkt, n, server);
        drainServer(s, server, kRvFound, observed, blob, 1000);
        reached = observed.len > 0;
    }
    if(!reached){ crypto_wipe(shared, sizeof shared); err = "the meeting-point server did not answer (check rendezvous= in starcom.conf)"; return false; }
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

    std::vector<Addr> theirCands;
    auto start = Clock::now();
    auto lastPut = Clock::now() - std::chrono::seconds(600);
    uint64_t newest = 0;
    status("waiting for your friend to come online (up to 2 minutes)...");
    while(msSince(start) < 120000 && theirCands.empty()){
        if(msSince(lastPut) >= 120000){
            uint8_t note[kMaxNote];
            uint64_t hNow = currentHour();
            if(hNow != hour){ hour = hNow; mailboxId(shared, hour, me.pub, mine); }
            size_t nl = sealNote(shared, me.pub, mine, myCands, note, sizeof note);
            if(nl){
                size_t pn = buildPut(mine, note, nl, pkt, sizeof pkt);
                s.sendTo(pkt, pn, server);
                Addr ignore;
                drainServer(s, server, kRvPutOk, ignore, blob, 500);
            }
            lastPut = Clock::now();
        }
        uint64_t h = currentHour();
        for(uint64_t hh = h - 1; hh <= h + 1 && theirCands.empty(); hh++){
            uint8_t theirs[32];
            mailboxId(shared, hh, friendPub, theirs);
            size_t gn = buildGet(theirs, pkt, sizeof pkt);
            s.sendTo(pkt, gn, server);
            Addr obs;
            if(!drainServer(s, server, kRvFound, obs, blob, 400)) continue;
            uint64_t stamp = 0;
            std::vector<Addr> cands;
            if(openNote(shared, friendPub, theirs, blob.data(), blob.size(), stamp, cands) && noteFresh(stamp, newest) && !cands.empty()){
                newest = stamp;
                for(const Addr& c : cands){
                    Addr m = mapForSocket(c, server.family());
                    if(m.len > 0 && m.family() == server.family()) addUnique(theirCands, m);
                }
            }
        }
    }
    if(theirCands.empty()){ crypto_wipe(shared, sizeof shared); err = "your friend did not come online within 2 minutes"; return false; }

    std::vector<Addr> targets;
    for(const Addr& c : theirCands) if(!(c == observed)) addUnique(targets, c);
    bool friendHides = true;
    for(const Addr& c : theirCands) if(!(c == server)) friendHides = false;
    if(!hideIp && !friendHides){
        status(std::string("friend found; trying a direct connection (") + (initiator ? "calling" : "answering") + ")...");
    }
    if(!hideIp && !friendHides && handshakeWith(s, me, friendPub, initiator, targets, true, myCaps, out, peerCaps, err, 10000)){
        crypto_wipe(shared, sizeof shared);
        status("direct connection established: your friend can see your public IP address");
        return true;
    }

    status(hideIp || friendHides ? "using the relay (one of you has Hide my IP on)..." : "direct connection failed; switching to the relay (it forwards only encrypted packets)...");
    uint8_t token[32];
    relayToken(shared, currentHour(), token);
    crypto_wipe(shared, sizeof shared);
    size_t bn = buildBind(token, pkt, sizeof pkt);
    int bound = 0;
    for(int i = 0; i < 30 && bound < 2; i++){
        s.sendTo(pkt, bn, server);
        int b = waitBind(s, server, 1000);
        if(b > bound) bound = b;
    }
    crypto_wipe(token, sizeof token);
    if(bound == 0){ err = "the relay did not answer"; return false; }
    if(bound < 2){
        uint64_t nowMs = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        uint64_t age = nowMs > newest ? (nowMs - newest) / 1000 : 0;
        err = "your friend never reached the relay; their last note was " + std::to_string(age) + " s old, so they may have gone offline";
        return false; }
    std::vector<Addr> relayOnly{ server };
    if(handshakeWith(s, me, friendPub, initiator, relayOnly, false, myCaps, out, peerCaps, err, 30000)){
        status("connected through the relay: your friend cannot see your IP address; the relay sees both addresses, never the content");
        return true;
    }
    if(err.empty()) err = "could not connect, even through the relay";
    return false;
}

}

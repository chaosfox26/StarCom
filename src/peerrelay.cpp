#include "peerrelay.h"
#include "relay.h"
#include "seed.h"
#include "wire.h"
#include <chrono>
#include <cstring>

namespace sc {

namespace {

const uint32_t kAnnounceEveryMs = 240000;
const uint32_t kRetryEveryMs = 30000;
const uint32_t kPingEveryMs = 25000;
const uint32_t kSilentMs = 75000;
const size_t kMaxEntryPoints = 8;

uint32_t nowMs(){
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

RelayAnnouncer::RelayAnnouncer(){}

RelayAnnouncer::~RelayAnnouncer(){
    stop();
}

void RelayAnnouncer::start(const Identity& relayId, uint16_t port){
    stop();
    id_ = relayId;
    port_ = port;
    quit_ = false;
    std::vector<Addr> seeds;
    std::string err;
    if(!seedNodes(seeds, err) || seeds.empty()) return;
    std::vector<Addr> use;
    for(const Addr& a : seeds) if(a.family() == familyV4()) use.push_back(a);
    if(use.empty()) use = seeds;
    if(use.size() > kMaxEntryPoints) use.resize(kMaxEntryPoints);
    for(const Addr& a : use){
        std::unique_ptr<Link> l(new Link());
        l->relay = a;
        links_.push_back(std::move(l));
    }
    for(auto& l : links_){
        Link* lp = l.get();
        lp->t = std::thread([this, lp]{ linkLoop(*lp); });
    }
}

void RelayAnnouncer::stop(){
    quit_ = true;
    for(auto& l : links_) if(l->t.joinable()) l->t.join();
    links_.clear();
    identityWipe(id_);
}

int RelayAnnouncer::listedBy() const {
    int n = 0;
    for(const auto& l : links_) if(l->listed) n++;
    return n;
}

bool RelayAnnouncer::sendFrame(Link& l, Session& ses, const uint8_t* p, size_t n){
    uint8_t pkt[kMaxPacket];
    size_t len = 0;
    if(!sessionSeal(ses, Lane::Signal, p, n, pkt, sizeof pkt, len)) return false;
    return l.sock.sendTo(pkt, len, l.relay) == (int)len;
}

bool RelayAnnouncer::connect(Link& l, Session& ses){
    std::string err;
    if(l.sock.localPort() == 0 && !l.sock.open(l.relay.family(), 0, err)) return false;
    uint8_t pub[32];
    bool havePub = false;
    uint8_t req[kKeyRequestLen] = {0};
    writeHeader(req, PacketType::Link, Lane::Signal);
    req[kHeaderLen] = kKeyRequest;
    for(int t = 0; t < 3 && !havePub && !quit_; t++){
        l.sock.sendTo(req, sizeof req, l.relay);
        uint32_t until = nowMs() + 1000;
        while(!havePub && !quit_ && (int32_t)(nowMs() - until) < 0){
            uint8_t in[kMaxPacket];
            Addr from;
            int r = l.sock.recvFrom(in, sizeof in, from, 200);
            Header h;
            if(r < (int)(kHeaderLen + 1 + 32) || !(from == l.relay) || !readHeader(in, (size_t)r, h) || h.type != PacketType::Link || in[kHeaderLen] != kKeyReply) continue;
            std::memcpy(pub, in + kHeaderLen + 1, 32);
            havePub = true;
        }
    }
    if(!havePub) return false;
    std::string caps, e;
    return hsInitiate(l.sock, l.relay, id_, pub, "{}", ses, caps, e, 8000);
}

void RelayAnnouncer::linkLoop(Link& l){
    uint32_t backoff = 2000;
    while(!quit_){
        Session ses;
        if(!connect(l, ses)){
            sessionWipe(ses);
            for(uint32_t waited = 0; waited < backoff && !quit_; waited += 100) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            backoff = backoff * 2 > 60000 ? 60000 : backoff * 2;
            continue;
        }
        backoff = 2000;
        uint32_t lastAnnounce = nowMs() - kAnnounceEveryMs - kRetryEveryMs;
        uint32_t lastPing = nowMs();
        uint32_t lastRecv = nowMs();
        while(!quit_){
            uint32_t now = nowMs();
            if((int32_t)(now - lastAnnounce) >= (int32_t)(l.listed ? kAnnounceEveryMs : kRetryEveryMs)){
                uint8_t f[3] = { kLinkRelayAnnounce, (uint8_t)(port_ >> 8), (uint8_t)(port_ & 0xFF) };
                sendFrame(l, ses, f, sizeof f);
                lastAnnounce = now;
            }
            if((int32_t)(now - lastPing) >= (int32_t)kPingEveryMs){
                uint8_t f[1] = { kLinkPing };
                sendFrame(l, ses, f, sizeof f);
                lastPing = now;
            }
            if((int32_t)(now - lastRecv) > (int32_t)kSilentMs) break;
            uint8_t in[kMaxPacket], plain[kMaxPacket];
            Addr from;
            int r = l.sock.recvFrom(in, sizeof in, from, 200);
            if(r <= 0 || !(from == l.relay)) continue;
            Lane lane;
            size_t pn = 0;
            if(!sessionOpen(ses, in, (size_t)r, from, lane, plain, sizeof plain, pn) || pn < 1) continue;
            lastRecv = nowMs();
            if(plain[0] == kLinkRelayAck && pn >= 2) l.listed = plain[1] == 1;
        }
        l.listed = false;
        sessionWipe(ses);
    }
}

}

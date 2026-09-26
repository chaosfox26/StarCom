#include "link.h"
#include "wire.h"
#include "seed.h"
#include "../third_party/monocypher/monocypher.h"
#include <chrono>
#include <cstring>

namespace sc {

namespace {

using Clock = std::chrono::steady_clock;

const uint8_t kRegister = 1, kRegistered = 2, kOpen = 3, kData = 4, kIncoming = 5, kClose = 6,
              kPublish = 7, kLookup = 8, kResult = 9, kPing = 10, kPublished = 12;
const uint8_t kKeyReq = 1, kKeyRep = 2;
const size_t kKeyReqLen = 64;
const size_t kMaxInner = 1351;

uint32_t nowMs(){
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

uint64_t wallMsNow(){
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

long long msSince(Clock::time_point t){
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

bool cancelled(const std::atomic<bool>* c){ return c && c->load(); }

void pause(int ms, const std::atomic<bool>* stop){
    auto until = Clock::now() + std::chrono::milliseconds(ms);
    while(Clock::now() < until && !cancelled(stop)) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

}

RelayLink::RelayLink() {}

RelayLink::~RelayLink(){ stop(); }

void RelayLink::setNote(const std::string& n){
    {
        std::lock_guard<std::mutex> g(noteMu_);
        note_ = n;
    }
    std::function<void()> fn;
    {
        std::lock_guard<std::mutex> g(incomingMu_);
        fn = onChange_;
    }
    if(fn) fn();
}

void RelayLink::setOnChange(std::function<void()> fn){
    std::lock_guard<std::mutex> g(incomingMu_);
    onChange_ = std::move(fn);
}

std::string RelayLink::note() const {
    std::lock_guard<std::mutex> g(noteMu_);
    return note_;
}

void RelayLink::setIncoming(std::function<void(uint64_t)> fn){
    std::lock_guard<std::mutex> g(incomingMu_);
    incoming_ = std::move(fn);
}

void RelayLink::start(const Identity& me, const std::string& myName){
    stop();
    id_ = me;
    name_ = myName;
    quit_ = false;
    ready_ = false;
    failed_ = false;
    setNote("Connecting to the StarCom network...");
    ctl_ = std::thread([this]{ ctlLoop(); });
}

void RelayLink::stop(){
    quit_ = true;
    waitCv_.notify_all();
    if(ctl_.joinable()) ctl_.join();
    if(rx_.joinable()) rx_.join();
    dropCircuits();
    hsCh_.close();
    if(hsQ_) hsQ_->shut();
    if(keyQ_) keyQ_->shut();
    sock_.close();
    {
        std::lock_guard<std::mutex> g(sessMu_);
        sessionWipe(link_);
        linked_ = false;
    }
    ready_ = false;
    identityWipe(id_);
    crypto_wipe(relayPub_, sizeof relayPub_);
}

bool RelayLink::waitReady(int maxMs, const std::atomic<bool>* cancel){
    auto start = Clock::now();
    while(!ready_){
        if(failed_ || cancelled(cancel) || quit_ || msSince(start) >= maxMs) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}

void RelayLink::dropCircuits(){
    std::lock_guard<std::mutex> g(circMu_);
    for(auto& c : circuits_) c.second.q->shut();
    circuits_.clear();
}

bool RelayLink::sendFrame(Lane lane, const uint8_t* p, size_t n){
    uint8_t pkt[kMaxPacket];
    size_t len = 0;
    bool ok;
    {
        std::lock_guard<std::mutex> g(sessMu_);
        if(!linked_) return false;
        ok = sessionSeal(link_, lane, p, n, pkt, sizeof pkt, len);
    }
    return ok && sock_.sendTo(pkt, len, relay_) == (int)len;
}

bool RelayLink::sendControl(uint8_t kind, const uint8_t* body, size_t n){
    uint8_t buf[kMaxPacket];
    if(1 + n > sizeof buf) return false;
    buf[0] = kind;
    if(n) std::memcpy(buf + 1, body, n);
    return sendFrame(Lane::Signal, buf, 1 + n);
}

int RelayLink::sendData(uint64_t circuit, const uint8_t* p, size_t n){
    if(n > kMaxInner) return -1;
    uint8_t buf[kMaxPacket];
    buf[0] = kData;
    putU64(buf + 1, circuit);
    std::memcpy(buf + 9, p, n);
    Lane lane = Lane::Signal;
    Header h;
    if(readHeader(p, n, h)) lane = h.lane;
    return sendFrame(lane, buf, 9 + n) ? (int)n : -1;
}

void RelayLink::dispatch(const uint8_t* p, size_t n){
    if(n < 1) return;
    uint8_t kind = p[0];
    if(kind == kRegistered){
        std::lock_guard<std::mutex> g(waitMu_);
        registered_ = n >= 2 && p[1] != 0;
        waitCv_.notify_all();
        return;
    }
    if(kind == kPublished){
        if(n >= 2 && p[1] == 0){
            nameTaken_ = true;
            setNote("The @name @" + name_ + " is already taken by someone else. Choose another one.");
        } else if(n >= 2){
            nameTaken_ = false;
            setNote("Online as @" + name_ + ". Your IP address stays hidden.");
        }
        return;
    }
    if(kind == kResult){
        std::lock_guard<std::mutex> g(waitMu_);
        haveResult_ = true;
        resultOk_ = n >= 2 && p[1] != 0;
        result_.assign(p + (n >= 2 ? 2 : n), p + n);
        waitCv_.notify_all();
        return;
    }
    if(n < 9) return;
    uint64_t c = getU64(p + 1);
    if(kind == kData){
        std::lock_guard<std::mutex> g(circMu_);
        auto it = circuits_.find(c);
        if(it != circuits_.end()) it->second.q->push(p + 9, n - 9, relay_);
        return;
    }
    if(kind == kClose){
        std::lock_guard<std::mutex> g(circMu_);
        auto it = circuits_.find(c);
        if(it != circuits_.end()){ it->second.q->shut(); circuits_.erase(it); }
        return;
    }
    if(kind == kIncoming){
        std::function<void(uint64_t)> fn;
        {
            std::lock_guard<std::mutex> g(incomingMu_);
            fn = incoming_;
        }
        if(fn) fn(c);
        else sendControl(kClose, p + 1, 8);
    }
}

void RelayLink::rxLoop(){
    uint8_t buf[kMaxPacket], plain[kMaxPacket];
    while(!quit_){
        Addr from;
        int r = sock_.recvFrom(buf, sizeof buf, from, 200);
        if(r <= 0 || !(from == relay_)) continue;
        Header h;
        if(!readHeader(buf, (size_t)r, h)) continue;
        lastRecvMs_ = nowMs();
        if(h.type == PacketType::Link){ keyQ_->push(buf, (size_t)r, from); continue; }
        if(h.type == PacketType::Hello || h.type == PacketType::Welcome){ hsQ_->push(buf, (size_t)r, from); continue; }
        if(h.type != PacketType::Data || !linked_) continue;
        Lane lane;
        size_t n = 0;
        bool ok;
        {
            std::lock_guard<std::mutex> g(sessMu_);
            ok = sessionOpen(link_, buf, (size_t)r, from, lane, plain, sizeof plain, n);
        }
        if(ok && n > 0) dispatch(plain, n);
    }
}

bool RelayLink::publish(std::string& err){
    if(name_.empty()) return true;
    PointerRecord r;
    std::memcpy(r.owner, id_.pub, 32);
    r.name = name_;
    r.seq = wallMsNow();
    r.expires = wallSeconds() + kPointerMaxLifeSec;
    r.relay = relay_;
    std::memcpy(r.relayPub, relayPub_, 32);
    uint8_t rec[kMaxPointerLen + 64];
    size_t n = signPointer(id_, r, rec, sizeof rec);
    if(!n){ err = "could not sign your @name record"; return false; }
    if(!sendControl(kPublish, rec, n)){ err = "not connected to your relay"; return false; }
    return true;
}

bool RelayLink::linkUp(std::string& err){
    bool havePub = false;
    for(int i = 0; i < 32; i++) if(relayPub_[i]) havePub = true;
    if(!havePub){
        UdpSocket keyCh;
        keyCh.attach(sock_, keyQ_);
        uint8_t req[kKeyReqLen] = {0};
        writeHeader(req, PacketType::Link, Lane::Signal);
        req[kHeaderLen] = kKeyReq;
        for(int t = 0; t < 3 && !havePub && !quit_; t++){
            sock_.sendTo(req, sizeof req, relay_);
            uint8_t in[kMaxPacket];
            Addr from;
            int r = keyCh.recvFrom(in, sizeof in, from, 1000);
            if(r >= (int)(kHeaderLen + 1 + 32) && in[kHeaderLen] == kKeyRep){
                std::memcpy(relayPub_, in + kHeaderLen + 1, 32);
                havePub = true;
            }
        }
        keyCh.close();
        if(!havePub){ err = "the relay did not answer"; return false; }
    }
    Session s;
    std::string caps, e;
    if(!hsInitiate(hsCh_, relay_, id_, relayPub_, "{}", s, caps, e, 8000)){ err = "the relay did not complete the encrypted link"; return false; }
    {
        std::lock_guard<std::mutex> g(sessMu_);
        link_ = s;
        linked_ = true;
    }
    sessionWipe(s);
    lastRecvMs_ = nowMs();
    {
        std::lock_guard<std::mutex> g(waitMu_);
        registered_ = false;
    }
    sendControl(kRegister, nullptr, 0);
    {
        std::unique_lock<std::mutex> g(waitMu_);
        waitCv_.wait_for(g, std::chrono::seconds(5), [this]{ return registered_ || quit_.load(); });
        if(!registered_){
            std::lock_guard<std::mutex> g2(sessMu_);
            linked_ = false;
            err = "the relay did not accept the link";
            return false;
        }
    }
    std::string pe;
    publish(pe);
    return true;
}

void RelayLink::ctlLoop(){
    std::string err;
    std::vector<Addr> seeds;
    if(!seedNodes(seeds, err) || seeds.empty()){
        setNote("No way into the StarCom network: " + (err.empty() ? std::string("this build has no entry points") : err));
        failed_ = true;
        return;
    }
    relay_ = seeds[0];
    for(const Addr& a : seeds) if(a.family() == familyV4()){ relay_ = a; break; }
    if(!sock_.open(relay_.family(), 0, err)){ setNote(err); failed_ = true; return; }
    hsQ_ = std::make_shared<PacketQueue>();
    keyQ_ = std::make_shared<PacketQueue>();
    hsCh_.attach(sock_, hsQ_);
    rx_ = std::thread([this]{ rxLoop(); });
    int backoff = 1;
    uint32_t lastPing = 0, lastPublish = 0;
    while(!quit_){
        if(!linked_){
            if(linkUp(err)){
                backoff = 1;
                lastPing = lastPublish = nowMs();
                ready_ = true;
                setNote(name_.empty() ? std::string("Connected to your relay.") : "Online as @" + name_ + ". Your IP address stays hidden.");
            } else {
                ready_ = false;
                setNote("Cannot reach the StarCom network yet (" + err + "); retrying...");
                pause(backoff * 1000, &quit_);
                backoff = backoff * 2 > 30 ? 30 : backoff * 2;
            }
            continue;
        }
        pause(200, &quit_);
        uint32_t now = nowMs();
        if(now - lastPing >= 25000){ sendControl(kPing, nullptr, 0); lastPing = now; }
        if(now - lastRecvMs_.load() >= 75000){
            {
                std::lock_guard<std::mutex> g(sessMu_);
                linked_ = false;
                sessionWipe(link_);
            }
            ready_ = false;
            dropCircuits();
            setNote("Lost the connection to your relay; reconnecting...");
            continue;
        }
        if(now - lastPublish >= 1800000){ std::string e; publish(e); lastPublish = now; }
    }
}

bool RelayLink::lookup(const std::string& name, const uint8_t* pin, PointerRecord& out, std::string& err){
    if(!ready_){ err = "not connected to the StarCom network yet"; return false; }
    if(name.empty() || name.size() > 32){ err = "not a valid @name"; return false; }
    std::lock_guard<std::mutex> lk(lookupMu_);
    {
        std::lock_guard<std::mutex> g(waitMu_);
        haveResult_ = false;
        result_.clear();
    }
    uint8_t body[1 + 32 + 32] = {0};
    body[0] = (uint8_t)name.size();
    std::memcpy(body + 1, name.data(), name.size());
    if(pin) std::memcpy(body + 1 + name.size(), pin, 32);
    if(!sendControl(kLookup, body, 1 + name.size() + 32)){ err = "not connected to your relay"; return false; }
    std::vector<uint8_t> rec;
    {
        std::unique_lock<std::mutex> g(waitMu_);
        waitCv_.wait_for(g, std::chrono::seconds(15), [this]{ return haveResult_ || quit_.load(); });
        if(!haveResult_){ err = "the network did not answer in time"; return false; }
        if(!resultOk_){ err = "@" + name + " was not found"; return false; }
        rec = result_;
    }
    PointerRecord r;
    if(!openPointer(rec.data(), rec.size(), wallSeconds(), r)){ err = "the answer failed its signature check"; return false; }
    if(r.name != name){ err = "the answer was for a different @name"; return false; }
    if(pin && std::memcmp(r.owner, pin, 32) != 0){ err = "@" + name + " is now signed by a different key than the friend you saved"; return false; }
    out = r;
    return true;
}

bool RelayLink::open(const PointerRecord& friendRec, UdpSocket& ch, std::string& err){
    uint64_t c = 0;
    while(c == 0) if(!sysRandom((uint8_t*)&c, sizeof c)){ err = "no system randomness"; return false; }
    auto q = std::make_shared<PacketQueue>();
    {
        std::lock_guard<std::mutex> g(circMu_);
        circuits_[c] = Circuit{ &ch, q };
    }
    ch.attach(sock_, q, [this, c](const uint8_t* p, size_t n, const Addr&){ return sendData(c, p, n); });
    uint8_t body[8 + 19 + 32 + 32];
    putU64(body, c);
    encodeAddr(friendRec.relay, body + 8);
    std::memcpy(body + 27, friendRec.relayPub, 32);
    std::memcpy(body + 59, friendRec.owner, 32);
    if(!sendControl(kOpen, body, sizeof body)){ close(ch); err = "not connected to your relay"; return false; }
    return true;
}

void RelayLink::accept(uint64_t circuit, UdpSocket& ch){
    auto q = std::make_shared<PacketQueue>();
    {
        std::lock_guard<std::mutex> g(circMu_);
        circuits_[circuit] = Circuit{ &ch, q };
    }
    ch.attach(sock_, q, [this, circuit](const uint8_t* p, size_t n, const Addr&){ return sendData(circuit, p, n); });
}

void RelayLink::close(UdpSocket& ch){
    uint64_t c = 0;
    bool found = false;
    {
        std::lock_guard<std::mutex> g(circMu_);
        for(auto it = circuits_.begin(); it != circuits_.end(); ++it){
            if(it->second.ch != &ch) continue;
            c = it->first;
            it->second.q->shut();
            circuits_.erase(it);
            found = true;
            break;
        }
    }
    if(found){
        uint8_t body[8];
        putU64(body, c);
        sendControl(kClose, body, sizeof body);
    }
    ch.close();
}

void RelayLink::wake(UdpSocket& ch){
    std::lock_guard<std::mutex> g(circMu_);
    for(auto& c : circuits_) if(c.second.ch == &ch) c.second.q->shut();
}

bool connectMasked(RelayLink& link, UdpSocket& ch, const Identity& me, const std::string& friendName, const uint8_t friendPub[32],
                   const std::string& myCaps, Session& out, std::string& peerCaps, std::string& err,
                   const std::function<void(const std::string&)>& status, const std::atomic<bool>* cancel){
    if(!link.ready()) status("connecting to the StarCom network...");
    if(!link.waitReady(30000, cancel)){
        err = cancelled(cancel) ? std::string("cancelled") : link.failed() ? link.note() : std::string("could not reach the StarCom network");
        return false;
    }
    auto start = Clock::now();
    bool saidMissing = false, saidFound = false, saidRetry = false;
    while(msSince(start) < 120000){
        if(cancelled(cancel)){ err = "cancelled"; return false; }
        auto turn = Clock::now();
        PointerRecord rec;
        std::string e;
        if(!link.lookup(friendName, friendPub, rec, e)){
            if(!saidMissing){ status("@" + friendName + " is not on the StarCom network right now; still looking..."); saidMissing = true; }
            long long spent = msSince(turn);
            if(spent < 2000) pause((int)(2000 - spent), cancel);
            continue;
        }
        saidMissing = false;
        if(!saidFound){ status("found @" + friendName + "; connecting through the relays..."); saidFound = true; }
        if(!link.open(rec, ch, e)){ pause(1000, cancel); continue; }
        Session s;
        std::string caps;
        if(hsInitiate(ch, link.relay(), me, friendPub, myCaps, s, caps, e, 10000)){
            out = s;
            sessionWipe(s);
            peerCaps = caps;
            status("connected to @" + friendName + "; neither of you can see the other's IP address");
            return true;
        }
        link.close(ch);
        if(!saidRetry){ status("@" + friendName + " is not reachable right now; still trying..."); saidRetry = true; }
        pause(1000, cancel);
    }
    err = "could not reach @" + friendName + " within 2 minutes";
    return false;
}

bool answerMasked(RelayLink& link, uint64_t circuit, UdpSocket& ch, const Identity& me, const std::string& myCaps,
                  Session& out, std::string& peerCaps, std::string& err){
    link.accept(circuit, ch);
    Session s;
    std::string caps, e;
    if(!hsRespond(ch, me, myCaps, s, caps, e, 10000, nullptr)){
        link.close(ch);
        err = "an incoming conversation did not complete";
        return false;
    }
    out = s;
    sessionWipe(s);
    peerCaps = caps;
    return true;
}

}

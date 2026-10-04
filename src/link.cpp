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
const uint8_t kOpenMasked = 1;
const size_t kKeyReqLen = 64;

uint32_t nowMs(){
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

uint64_t wallMsNow(){
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

uint64_t nextSeq(){
    static std::atomic<uint64_t> last{0};
    uint64_t now = wallMsNow();
    uint64_t prev = last.load();
    for(;;){
        uint64_t want = now > prev ? now : prev + 1;
        if(last.compare_exchange_weak(prev, want)) return want;
    }
}

long long msSince(Clock::time_point t){
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

bool cancelled(const std::atomic<bool>* c){ return c && c->load(); }

void pause(int ms, const std::atomic<bool>* stop){
    auto until = Clock::now() + std::chrono::milliseconds(ms);
    while(Clock::now() < until && !cancelled(stop)) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

bool zero32(const uint8_t* p){
    uint8_t acc = 0;
    for(int i = 0; i < 32; i++) acc |= p[i];
    return acc == 0;
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
    for(Conn& k : conns_) if(k.rx.joinable()) k.rx.join();
    dropCircuits(-1);
    for(Conn& k : conns_){
        k.hsCh.close();
        if(k.hsQ) k.hsQ->shut();
        if(k.keyQ) k.keyQ->shut();
        k.sock.close();
        {
            std::lock_guard<std::mutex> g(k.sessMu);
            sessionWipe(k.ses);
            k.linked = false;
        }
        k.ready = false;
        k.used = false;
        crypto_wipe(k.relayPub, sizeof k.relayPub);
    }
    {
        std::lock_guard<std::mutex> g(waitMu_);
        for(Conn& k : conns_) k.registered = false;
    }
    ready_ = false;
    identityWipe(id_);
}

bool RelayLink::waitReady(int maxMs, const std::atomic<bool>* cancel){
    auto start = Clock::now();
    while(!ready_){
        if(failed_ || cancelled(cancel) || quit_ || msSince(start) >= maxMs) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}

int RelayLink::primary() const {
    for(int i = 0; i < kConns; i++) if(conns_[i].ready) return i;
    return 0;
}

void RelayLink::dropCircuits(int ci){
    std::lock_guard<std::mutex> g(circMu_);
    for(auto it = circuits_.begin(); it != circuits_.end();){
        if(ci >= 0 && it->second.ci != ci){ ++it; continue; }
        it->second.q->shut();
        it = circuits_.erase(it);
    }
    for(auto it = incomingConn_.begin(); it != incomingConn_.end();){
        if(ci >= 0 && it->second != ci){ ++it; continue; }
        it = incomingConn_.erase(it);
    }
}

bool RelayLink::sendFrame(int ci, Lane lane, const uint8_t* p, size_t n){
    Conn& k = conns_[ci];
    uint8_t pkt[kMaxPacket];
    size_t len = 0;
    bool ok;
    {
        std::lock_guard<std::mutex> g(k.sessMu);
        if(!k.linked) return false;
        ok = sessionSeal(k.ses, lane, p, n, pkt, sizeof pkt, len);
    }
    return ok && k.sock.sendTo(pkt, len, k.relay) == (int)len;
}

bool RelayLink::sendControl(int ci, uint8_t kind, const uint8_t* body, size_t n){
    uint8_t buf[kMaxPacket];
    if(1 + n > sizeof buf) return false;
    buf[0] = kind;
    if(n) std::memcpy(buf + 1, body, n);
    return sendFrame(ci, Lane::Signal, buf, 1 + n);
}

int RelayLink::sendData(int ci, uint64_t circuit, const uint8_t* p, size_t n){
    if(n > kMaxCircuitInner) return -1;
    uint8_t buf[kMaxPacket];
    buf[0] = kData;
    putU64(buf + 1, circuit);
    std::memcpy(buf + 9, p, n);
    Lane lane = Lane::Signal;
    Header h;
    if(readHeader(p, n, h)) lane = h.lane;
    return sendFrame(ci, lane, buf, 9 + n) ? (int)n : -1;
}

void RelayLink::dispatch(int ci, const uint8_t* p, size_t n){
    if(n < 1) return;
    uint8_t kind = p[0];
    if(kind == kRegistered){
        std::lock_guard<std::mutex> g(waitMu_);
        conns_[ci].registered = n >= 2 && p[1] != 0;
        waitCv_.notify_all();
        return;
    }
    if(kind == kPublished){
        if(n >= 2 && p[1] == 0){
            nameTaken_ = true;
            setNote("The @name @" + name_ + " is already taken by someone else. Choose another one.");
        } else if(n >= 2 && p[1] == 3){
            published_[ci] = false;
            setNote("Your @name record was rejected as invalid. Check that your computer's clock is correct.");
        } else if(n >= 2 && p[1] == 2){
            published_[ci] = false;
            bool other = false;
            for(int j = 0; j < kConns; j++) if(published_[j]) other = true;
            if(++publishFails_[ci] >= 4){
                if(!other){
                    nameTaken_ = true;
                    setNote("Could not publish the @name @" + name_ + " after several tries: it may be taken, or the network may be unreachable. Choose another one or check your connection.");
                }
            } else {
                if(!other) setNote("Could not reach the @name directory just now; trying again in a few seconds.");
                republishDue_[ci] = (nowMs() + 10000) | 1;
            }
        } else if(n >= 2){
            publishFails_[ci] = 0;
            published_[ci] = true;
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
        if(it != circuits_.end() && it->second.ci == ci) it->second.q->push(p + 9, n - 9, conns_[ci].relay);
        return;
    }
    if(kind == kClose){
        std::lock_guard<std::mutex> g(circMu_);
        auto it = circuits_.find(c);
        if(it != circuits_.end() && it->second.ci == ci){ it->second.q->shut(); circuits_.erase(it); }
        auto ic = incomingConn_.find(c);
        if(ic != incomingConn_.end() && ic->second == ci) incomingConn_.erase(ic);
        return;
    }
    if(kind == kIncoming){
        std::function<void(uint64_t)> fn;
        {
            std::lock_guard<std::mutex> g(incomingMu_);
            fn = incoming_;
        }
        if(fn){
            {
                std::lock_guard<std::mutex> g(circMu_);
                incomingConn_[c] = ci;
            }
            fn(c);
        } else sendControl(ci, kClose, p + 1, 8);
    }
}

void RelayLink::rxLoop(int ci){
    Conn& k = conns_[ci];
    uint8_t buf[kMaxPacket], plain[kMaxPacket];
    while(!quit_){
        Addr from;
        int r = k.sock.recvFrom(buf, sizeof buf, from, 200);
        if(r <= 0 || !(from == k.relay)) continue;
        Header h;
        if(!readHeader(buf, (size_t)r, h)) continue;
        k.lastRecvMs = nowMs();
        if(h.type == PacketType::Link){ k.keyQ->push(buf, (size_t)r, from); continue; }
        if(h.type == PacketType::Hello || h.type == PacketType::Welcome){ k.hsQ->push(buf, (size_t)r, from); continue; }
        if(h.type != PacketType::Data || !k.linked) continue;
        Lane lane;
        size_t n = 0;
        bool ok;
        {
            std::lock_guard<std::mutex> g(k.sessMu);
            ok = sessionOpen(k.ses, buf, (size_t)r, from, lane, plain, sizeof plain, n);
        }
        if(ok && n > 0) dispatch(ci, plain, n);
    }
}

bool RelayLink::publish(int ci, std::string& err){
    if(name_.empty()) return true;
    const Conn& k = conns_[ci];
    const Conn& o = conns_[1 - ci];
    PointerRecord r;
    std::memcpy(r.owner, id_.pub, 32);
    r.name = name_;
    r.seq = nextSeq();
    r.expires = wallSeconds() + kPointerMaxLifeSec;
    r.relay = k.relay;
    std::memcpy(r.relayPub, k.relayPub, 32);
    if(o.used && o.ready && !zero32(o.relayPub) && std::memcmp(o.relayPub, k.relayPub, 32) != 0){
        r.relays = 2;
        r.relay2 = o.relay;
        std::memcpy(r.relayPub2, o.relayPub, 32);
    }
    uint8_t rec[kMaxPointerLen + 64];
    size_t n = signPointer(id_, r, rec, sizeof rec);
    if(!n){ err = "could not sign your @name record"; return false; }
    if(!sendControl(ci, kPublish, rec, n)){ err = "not connected to your relay"; return false; }
    return true;
}

bool RelayLink::linkUp(int ci, std::string& err){
    Conn& k = conns_[ci];
    if(zero32(k.relayPub)){
        UdpSocket keyCh;
        keyCh.attach(k.sock, k.keyQ);
        uint8_t req[kKeyReqLen] = {0};
        writeHeader(req, PacketType::Link, Lane::Signal);
        req[kHeaderLen] = kKeyReq;
        bool havePub = false;
        for(int t = 0; t < 3 && !havePub && !quit_; t++){
            k.sock.sendTo(req, sizeof req, k.relay);
            uint8_t in[kMaxPacket];
            Addr from;
            int r = keyCh.recvFrom(in, sizeof in, from, 1000);
            if(r >= (int)(kHeaderLen + 1 + 32) && in[kHeaderLen] == kKeyRep && !zero32(in + kHeaderLen + 1)){
                std::memcpy(k.relayPub, in + kHeaderLen + 1, 32);
                havePub = true;
            }
        }
        keyCh.close();
        if(!havePub){ err = "the relay did not answer"; return false; }
        for(int j = 0; j < kConns; j++){
            if(j == ci || !conns_[j].used || zero32(conns_[j].relayPub)) continue;
            if(std::memcmp(conns_[j].relayPub, k.relayPub, 32) == 0){
                k.used = false;
                err = "the same relay twice";
                return false;
            }
        }
    }
    Session s;
    std::string caps, e;
    if(!hsInitiate(k.hsCh, k.relay, id_, k.relayPub, "{}", s, caps, e, 8000)){ err = "the relay did not complete the encrypted link"; return false; }
    {
        std::lock_guard<std::mutex> g(k.sessMu);
        k.ses = s;
        k.linked = true;
    }
    sessionWipe(s);
    k.lastRecvMs = nowMs();
    {
        std::lock_guard<std::mutex> g(waitMu_);
        k.registered = false;
    }
    sendControl(ci, kRegister, nullptr, 0);
    {
        std::unique_lock<std::mutex> g(waitMu_);
        waitCv_.wait_for(g, std::chrono::seconds(5), [this, &k]{ return k.registered || quit_.load(); });
        if(!k.registered){
            std::lock_guard<std::mutex> g2(k.sessMu);
            k.linked = false;
            err = "the relay did not accept the link";
            return false;
        }
    }
    return true;
}

void RelayLink::linkDown(int ci){
    Conn& k = conns_[ci];
    {
        std::lock_guard<std::mutex> g(k.sessMu);
        k.linked = false;
        sessionWipe(k.ses);
    }
    k.ready = false;
    published_[ci] = false;
    publishFails_[ci] = 0;
    republishDue_[ci] = 0;
    dropCircuits(ci);
}

void RelayLink::ctlLoop(){
    std::string err;
    std::vector<Addr> seeds;
    if(!seedNodes(seeds, err) || seeds.empty()){
        setNote("No way into the StarCom network: " + (err.empty() ? std::string("this build has no entry points") : err));
        failed_ = true;
        return;
    }
    std::vector<Addr> use;
    for(const Addr& a : seeds) if(a.family() == familyV4()) use.push_back(a);
    if(use.empty()) use = seeds;
    for(int i = 0; i < kConns && i < (int)use.size(); i++){
        Conn& k = conns_[i];
        k.relay = use[(size_t)i];
        if(!k.sock.open(k.relay.family(), 0, err)){
            if(i == 0){ setNote(err); failed_ = true; return; }
            continue;
        }
        k.hsQ = std::make_shared<PacketQueue>();
        k.keyQ = std::make_shared<PacketQueue>();
        k.hsCh.attach(k.sock, k.hsQ);
        k.used = true;
        k.rx = std::thread([this, i]{ rxLoop(i); });
    }
    int backoff[kConns] = { 1, 1 };
    uint32_t retryAt[kConns] = { 0, 0 };
    uint32_t lastPing[kConns] = { 0, 0 }, lastPublish[kConns] = { 0, 0 };
    int shownMask = -1;
    while(!quit_){
        for(int i = 0; i < kConns && !quit_; i++){
            Conn& k = conns_[i];
            if(!k.used) continue;
            uint32_t now = nowMs();
            if(!k.linked){
                if(retryAt[i] && (int32_t)(now - retryAt[i]) < 0) continue;
                if(linkUp(i, err)){
                    backoff[i] = 1;
                    retryAt[i] = 0;
                    lastPing[i] = lastPublish[i] = nowMs();
                    k.ready = true;
                    for(int j = 0; j < kConns; j++) if(conns_[j].ready){ std::string pe; publish(j, pe); lastPublish[j] = nowMs(); }
                } else {
                    k.ready = false;
                    if(!k.used) continue;
                    retryAt[i] = nowMs() + (uint32_t)backoff[i] * 1000;
                    backoff[i] = backoff[i] * 2 > 30 ? 30 : backoff[i] * 2;
                    if(i == 0 && !ready_) setNote("Cannot reach the StarCom network yet (" + err + "); retrying...");
                }
                continue;
            }
            uint32_t due = republishDue_[i].load();
            bool retryPublish = due != 0 && (int32_t)(nowMs() - due) >= 0;
            if(retryPublish) republishDue_[i].compare_exchange_strong(due, 0);
            if(now - lastPing[i] >= 25000){ sendControl(i, kPing, nullptr, 0); lastPing[i] = now; }
            if((int32_t)(now - k.lastRecvMs.load()) >= 75000){
                linkDown(i);
                for(int j = 0; j < kConns; j++) if(conns_[j].ready){ std::string pe; publish(j, pe); lastPublish[j] = now; }
                continue;
            }
            if(retryPublish || now - lastPublish[i] >= 1800000){ std::string pe; publish(i, pe); lastPublish[i] = now; }
        }
        bool any = false;
        int mask = 0;
        for(int i = 0; i < kConns; i++) if(conns_[i].ready){ any = true; mask |= 1 << i; }
        ready_ = any;
        if(mask != shownMask){
            if(any && shownMask <= 0) setNote(name_.empty() ? std::string("Connected to your relay.") : "Online as @" + name_ + ". Your IP address stays hidden.");
            else if(!any && shownMask > 0) setNote("Lost the connection to your relay; reconnecting...");
            shownMask = mask;
        }
        pause(200, &quit_);
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
    if(!sendControl(primary(), kLookup, body, 1 + name.size() + 32)){ err = "not connected to your relay"; return false; }
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

bool RelayLink::open(const PointerRecord& friendRec, UdpSocket& ch, std::string& err, bool masked){
    const Addr* tAddr[2] = { &friendRec.relay, &friendRec.relay2 };
    const uint8_t* tPub[2] = { friendRec.relayPub, friendRec.relayPub2 };
    int nt = friendRec.relays == 2 ? 2 : 1;
    int ci = -1, ti = 0;
    for(int pass = 0; pass < 2 && ci < 0; pass++){
        for(int i = 0; i < kConns && ci < 0; i++){
            if(!conns_[i].ready) continue;
            for(int t = 0; t < nt; t++){
                if(pass == 0 && std::memcmp(conns_[i].relayPub, tPub[t], 32) == 0) continue;
                ci = i;
                ti = t;
                break;
            }
        }
    }
    if(ci < 0){ err = "not connected to your relay"; return false; }
    uint64_t c = 0;
    while(c == 0) if(!sysRandom((uint8_t*)&c, sizeof c)){ err = "no system randomness"; return false; }
    auto q = std::make_shared<PacketQueue>();
    {
        std::lock_guard<std::mutex> g(circMu_);
        circuits_[c] = Circuit{ &ch, q, ci };
    }
    ch.attach(conns_[ci].sock, q, [this, ci, c](const uint8_t* p, size_t n, const Addr&){ return sendData(ci, c, p, n); });
    uint8_t body[8 + 19 + 32 + 32 + 1];
    putU64(body, c);
    encodeAddr(*tAddr[ti], body + 8);
    std::memcpy(body + 27, tPub[ti], 32);
    std::memcpy(body + 59, friendRec.owner, 32);
    body[91] = masked ? kOpenMasked : 0;
    if(!sendControl(ci, kOpen, body, sizeof body)){ close(ch); err = "not connected to your relay"; return false; }
    return true;
}

void RelayLink::accept(uint64_t circuit, UdpSocket& ch){
    auto q = std::make_shared<PacketQueue>();
    int ci = 0;
    {
        std::lock_guard<std::mutex> g(circMu_);
        auto ic = incomingConn_.find(circuit);
        if(ic != incomingConn_.end()){ ci = ic->second; incomingConn_.erase(ic); }
        circuits_[circuit] = Circuit{ &ch, q, ci };
    }
    ch.attach(conns_[ci].sock, q, [this, ci, circuit](const uint8_t* p, size_t n, const Addr&){ return sendData(ci, circuit, p, n); });
}

void RelayLink::close(UdpSocket& ch){
    uint64_t c = 0;
    int ci = 0;
    bool found = false;
    {
        std::lock_guard<std::mutex> g(circMu_);
        for(auto it = circuits_.begin(); it != circuits_.end(); ++it){
            if(it->second.ch != &ch) continue;
            c = it->first;
            ci = it->second.ci;
            it->second.q->shut();
            circuits_.erase(it);
            found = true;
            break;
        }
    }
    if(found){
        uint8_t body[8];
        putU64(body, c);
        sendControl(ci, kClose, body, sizeof body);
    }
    ch.close();
}

void RelayLink::wake(UdpSocket& ch){
    std::lock_guard<std::mutex> g(circMu_);
    for(auto& c : circuits_) if(c.second.ch == &ch) c.second.q->shut();
}

bool connectMasked(RelayLink& link, UdpSocket& ch, const Identity& me, const std::string& friendName, const uint8_t friendPub[32],
                   const std::string& myCaps, Session& out, std::string& peerCaps, std::string& err,
                   const std::function<void(const std::string&)>& status, const std::atomic<bool>* cancel, bool masked){
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
        if(!link.open(rec, ch, e, masked)){ pause(1000, cancel); continue; }
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

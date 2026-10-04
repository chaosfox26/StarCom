#include "nethub.h"
#include "chat.h"
#include "dht.h"
#include "engine.h"
#include "hwprobe.h"
#include "paths.h"
#include "textsafe.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>

namespace sc {

namespace {

const char* kIdFile = "identity.key";
const char* kFriends = "friends.txt";
const char* kNameFile = "name.txt";
const char* kVolunteerFile = "volunteer.txt";
const int kVolunteerKbps = 2000;

enum Kind { kStatus = 0, kIncoming = 1, kConnected = 2, kEnded = 4, kPeerLeft = 5, kFound = 6, kAdoptEv = 7, kRequestEv = 8,
            kChecked = 9, kTick = 10, kHubChanged = 11, kWant = 12, kHubNote = 13, kInvite = 14, kHubInfo = 15, kSessionReq = 16 };

const uint32_t kRequestLifeMs = 60000;
const size_t kMaxRequests = 5;
const uint32_t kIdleMs = 300000;

uint32_t tickMs(){
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string clockNow(){
    std::time_t t = std::time(nullptr);
    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return b;
}

std::string cleanNick(const std::string& in){
    std::string o;
    for(char c : safeText(in)){ if((unsigned char)c >= 0x20 && c != '\t') o += c; }
    while(!o.empty() && o.back() == ' ') o.pop_back();
    while(!o.empty() && o.front() == ' ') o.erase(0, 1);
    if(o.size() > 32){
        size_t cut = 32;
        while(cut > 0 && ((unsigned char)o[cut] & 0xC0) == 0x80) cut--;
        o.resize(cut);
        while(!o.empty() && o.back() == ' ') o.pop_back();
    }
    return o;
}

std::string readName(){
    std::ifstream f(kNameFile);
    std::string line, n;
    if(std::getline(f, line) && normalizeName(line, n)) return n;
    return std::string();
}

std::string keyStr(const uint8_t k[32]){ return std::string((const char*)k, 32); }

std::string withHint(const std::string& caps, const std::string& capsHint){
    if(capsHint.compare(0, 4, "inv:") != 0 || caps.size() < 2) return caps;
    std::string hex = capsHint.substr(4);
    if(hex.empty() || hex.size() > 64) return caps;
    for(char c : hex) if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return caps;
    return "{\"inv\":\"" + hex + "\"," + caps.substr(1);
}

std::string capsValue(const std::string& caps, const char* key){
    std::string k = std::string("\"") + key + "\":\"";
    size_t i = caps.find(k);
    if(i == std::string::npos) return std::string();
    i += k.size();
    size_t j = caps.find('"', i);
    if(j == std::string::npos || j - i > 64) return std::string();
    std::string v = caps.substr(i, j - i);
    for(char c : v) if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return std::string();
    return v;
}

const char* kConnectedText = "Connected. Neither of you can see the other's IP address.";

}

struct NetHub::Adopt {
    std::unique_ptr<UdpSocket> ch;
    Session ses;
    std::string caps;
    bool respond = false;
    uint8_t key[32] = {0};
    std::string hint;
    ~Adopt(){ sessionWipe(ses); }
};

struct NetHub::Peer {
    uint8_t key[32] = {0};
    std::string keyS;
    std::string name;
    std::string capsHint;
    std::unique_ptr<UdpSocket> ch;
    std::shared_ptr<Adopt> adopt;
    std::thread t;
    std::atomic<bool> quit{false};
    std::atomic<bool> done{false};
    std::atomic<bool> up{false};
    std::atomic<bool> wantChat{false};
    std::atomic<bool> chatQuit{false};
    std::atomic<bool> knock{false};
    uint64_t gen = 0;
    bool own = true;
    std::mutex engMu;
    Engine* eng = nullptr;
    std::mutex outMu;
    std::deque<std::string> out;
    std::mutex callMu;
    VoiceCall* call = nullptr;
    std::atomic<uint32_t> lastHub{0};
    std::mutex preMu;
    bool announced = false;
    std::string verified;
    std::vector<std::string> pre;
    std::atomic<uint32_t> lastReq{0};
};

NetHub::NetHub() {}

NetHub::~NetHub(){
    shutdown();
    if(haveId_) identityWipe(id_);
}

bool NetHub::start(std::string& err){
    bool created = false;
    if(!identityLoadOrCreate(kIdFile, id_, created, err)) return false;
    haveId_ = true;
    myCode_ = friendCode(id_.pub);
    myName_ = readName();
    std::error_code ec;
    dataFolder_ = std::filesystem::current_path(ec).u8string();
    myCaps_ = capsJson(probeHardware(), myName_);
    std::ifstream f(kFriends);
    std::string line;
    while(std::getline(f, line)){
        while(!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        bool met = line.size() > 5 && line.compare(line.size() - 5, 5, " +met") == 0;
        if(met) line.resize(line.size() - 5);
        size_t a = line.find(' ');
        std::string code = a == std::string::npos ? line : line.substr(0, a);
        Friend fr;
        fr.met = met;
        if(!parseFriendCode(code, fr.pub)) continue;
        std::string rest = a == std::string::npos ? std::string() : line.substr(a + 1);
        size_t b = rest.rfind(' ');
        std::string nm;
        if(b != std::string::npos && normalizeName(rest.substr(b + 1), nm)){ fr.name = nm; rest = rest.substr(0, b); }
        fr.code = friendCode(fr.pub);
        fr.nick = cleanNick(rest).empty() ? (fr.name.empty() ? fr.code.substr(0, 9) : fr.name) : cleanNick(rest);
        friends_.push_back(fr);
    }
    syncFriendKeys();
    link_.setIncoming([this](uint64_t c){
        {
            std::lock_guard<std::mutex> g(inMu_);
            if(inQ_.size() < 32) inQ_.push_back(c);
        }
        inCv_.notify_one();
    });
    link_.setOnChange([this]{ if(wake) wake(); });
    link_.start(id_, myName_);
    acceptor_ = std::thread([this]{ acceptLoop(); });
    hubThread_ = std::thread([this]{ hubLoop(); });
    {
        std::ifstream vf(kVolunteerFile);
        std::string v;
        volunteer_ = std::getline(vf, v) && v.compare(0, 2, "on") == 0;
    }
    if(volunteer_) bridge_.start(kVolunteerKbps);
    return true;
}

bool NetHub::setVolunteer(bool on, std::string& err){
    std::string tmp = std::string(kVolunteerFile) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << (on ? "on" : "off") << '\n';
        if(!f){ err = "Could not save the setting."; return false; }
    }
    if(!replaceFile(tmp, kVolunteerFile)){ err = "Could not save the setting."; return false; }
    volunteer_ = on;
    if(on) bridge_.start(kVolunteerKbps);
    else bridge_.stop();
    return true;
}

bool NetHub::setMyName(const std::string& in, std::string& err){
    std::string n;
    std::string t = in;
    while(!t.empty() && t.back() == ' ') t.pop_back();
    while(!t.empty() && t.front() == ' ') t.erase(0, 1);
    if(!normalizeName(t, n)){ err = "An @name is 3 to 32 characters: letters a-z, digits, '_' or '-'."; return false; }
    std::string tmp = std::string(kNameFile) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << n << '\n';
        if(!f){ err = "Could not save your @name."; return false; }
    }
    if(!replaceFile(tmp, kNameFile)){ err = "Could not save your @name."; return false; }
    std::string caps = capsJson(probeHardware(), n);
    {
        std::lock_guard<std::mutex> g(capsMu_);
        myName_ = n;
        myCaps_ = caps;
    }
    link_.start(id_, n);
    nameChanged_ = true;
    hubCv_.notify_one();
    return true;
}

std::string NetHub::capsNow() const {
    std::lock_guard<std::mutex> g(capsMu_);
    return myCaps_;
}

std::string NetHub::nameNow() const {
    std::lock_guard<std::mutex> g(capsMu_);
    return myName_;
}

void NetHub::syncFriendKeys(){
    std::lock_guard<std::mutex> g(pubsMu_);
    friendKeys_.clear();
    metKeys_.clear();
    for(const Friend& fr : friends_){
        friendKeys_.push_back(keyStr(fr.pub));
        if(fr.met) metKeys_.push_back(keyStr(fr.pub));
    }
}

bool NetHub::isMetKey(const std::string& key){
    std::lock_guard<std::mutex> g(pubsMu_);
    for(const std::string& k : metKeys_) if(k == key) return true;
    return false;
}

bool NetHub::isFriendKey(const std::string& key){
    std::lock_guard<std::mutex> g(pubsMu_);
    for(const std::string& k : friendKeys_) if(k == key) return true;
    return false;
}

size_t NetHub::friendIdx(const std::string& key) const {
    for(size_t i = 0; i < friends_.size(); i++) if(std::memcmp(friends_[i].pub, key.data(), 32) == 0) return i;
    return (size_t)-1;
}

void NetHub::saveFriends(){
    std::string tmp = std::string(kFriends) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        for(const Friend& fr : friends_) f << fr.code << ' ' << fr.nick << (fr.name.empty() ? std::string() : " " + fr.name) << (fr.met ? " +met" : "") << '\n';
        if(!f) return;
    }
    replaceFile(tmp, kFriends);
}

void NetHub::clearFound(){
    if(finding_) return;
    foundName_.clear();
    foundCode_.clear();
    foundErr_.clear();
}

void NetHub::find(const std::string& nameIn){
    if(finding_) return;
    foundCode_.clear();
    foundErr_.clear();
    std::string t = nameIn, n;
    while(!t.empty() && t.back() == ' ') t.pop_back();
    while(!t.empty() && t.front() == ' ') t.erase(0, 1);
    if(!normalizeName(t, n)){ foundErr_ = "An @name is 3 to 32 characters: letters a-z, digits, '_' or '-'."; return; }
    if(n == myName_){ foundErr_ = "That is your own @name."; return; }
    foundName_ = n;
    finding_ = true;
    if(finder_.joinable()) finder_.join();
    finder_ = std::thread([this, n]{
        std::string e;
        PointerRecord rec;
        if(!link_.waitReady(20000, &quit_)) e = link_.failed() ? link_.note() : "Not connected to the StarCom network yet. Try again in a moment.";
        else if(link_.lookup(n, nullptr, rec, e)){ post(std::string(), kFound, "OK|" + friendCode(rec.owner), 0); return; }
        post(std::string(), kFound, "ERR|" + e, 0);
    });
}

bool NetHub::addFound(const std::string& nick, std::string& err){
    if(foundCode_.empty() || foundName_.empty()){ err = "Find your friend by their @name first."; return false; }
    Friend fr;
    if(!parseFriendCode(foundCode_, fr.pub)){ err = "That answer was not valid."; return false; }
    if(std::memcmp(fr.pub, id_.pub, 32) == 0){ err = "That is you."; return false; }
    for(Friend& o : friends_){
        if(std::memcmp(o.pub, fr.pub, 32) == 0){
            o.name = foundName_;
            saveFriends();
            err = "That friend is already in your list.";
            return false;
        }
    }
    fr.code = foundCode_;
    fr.name = foundName_;
    fr.nick = cleanNick(nick).empty() ? foundName_ : cleanNick(nick);
    friends_.push_back(fr);
    saveFriends();
    syncFriendKeys();
    foundName_.clear();
    foundCode_.clear();
    foundErr_.clear();
    return true;
}

void NetHub::post(const std::string& key, int kind, const std::string& text, uint64_t gen, std::shared_ptr<Adopt> adopt){
    { std::lock_guard<std::mutex> lk(mu_); events_.push_back(Event{ key, kind, text, gen, std::move(adopt) }); }
    if(wake) wake();
}

std::shared_ptr<NetHub::Peer> NetHub::findPeer(const std::string& key){
    std::lock_guard<std::mutex> g(peersMu_);
    auto it = peers_.find(key);
    return it == peers_.end() ? nullptr : it->second;
}

std::shared_ptr<NetHub::Peer> NetHub::startPeer(const std::string& key, const std::string& name, const std::string& capsHint, std::shared_ptr<Adopt> adopt){
    auto p = std::make_shared<Peer>();
    std::memcpy(p->key, key.data(), 32);
    p->keyS = key;
    p->name = name;
    p->capsHint = capsHint;
    p->gen = nextGen_++;
    p->own = !adopt;
    p->lastHub = 0;
    if(adopt){
        p->ch = std::move(adopt->ch);
        p->adopt = adopt;
    } else p->ch.reset(new UdpSocket());
    {
        std::lock_guard<std::mutex> g(peersMu_);
        auto it = peers_.find(key);
        if(it != peers_.end()) retired_.push_back(it->second);
        peers_[key] = p;
    }
    size_t idx = friendIdx(key);
    if(idx != (size_t)-1){
        Friend& f = friends_[idx];
        bool ready = adopt && !adopt->respond;
        f.state = ready ? LinkState::Connected : LinkState::Connecting;
        f.status = ready ? std::string(kConnectedText) : "Looking for @" + (f.name.empty() ? f.nick : f.name) + "...";
    }
    p->t = std::thread([this, p]{ run(p); });
    return p;
}

void NetHub::ensurePeer(const std::string& key, const std::string& name, const std::string& capsHint, bool forHub){
    std::shared_ptr<Peer> p = findPeer(key);
    if(p && !p->done){
        if(forHub) p->lastHub = tickMs();
        else p->wantChat = true;
        return;
    }
    if(name.empty()) return;
    std::shared_ptr<Peer> np = startPeer(key, name, capsHint, nullptr);
    if(forHub) np->lastHub = tickMs();
}

void NetHub::open(size_t idx){
    if(idx >= friends_.size()) return;
    Friend& f = friends_[idx];
    if(f.name.empty()){ f.status = "Add this friend again by their @name to reach them."; return; }
    ensurePeer(keyStr(f.pub), f.name, std::string(), false);
}

void NetHub::send(size_t idx, const std::string& text){
    if(idx >= friends_.size()) return;
    std::shared_ptr<Peer> p = findPeer(keyStr(friends_[idx].pub));
    if(!p || p->done) return;
    if(text == "/quit"){ close(idx); return; }
    { std::lock_guard<std::mutex> lk(p->outMu); p->out.push_back(text); }
    ChatLine l;
    l.mine = true;
    l.text = text;
    l.time = clockNow();
    friends_[idx].lines.push_back(l);
}

void NetHub::close(size_t idx){
    if(idx >= friends_.size()) return;
    std::shared_ptr<Peer> p = findPeer(keyStr(friends_[idx].pub));
    if(!p) return;
    p->chatQuit = true;
}

CallView NetHub::callView(size_t idx){
    CallView v;
    if(idx >= friends_.size()) return v;
    std::shared_ptr<Peer> p = findPeer(keyStr(friends_[idx].pub));
    if(!p) return v;
    std::lock_guard<std::mutex> lk(p->callMu);
    VoiceCall* c = p->call;
    if(!c) return v;
    v.state = c->state();
    v.note = c->note();
    v.seconds = c->seconds();
    v.ping = c->pingMs();
    v.muted = c->muted();
    v.deafened = c->deafened();
    return v;
}

void NetHub::callAction(size_t idx, CallAction a){
    if(idx >= friends_.size()) return;
    std::shared_ptr<Peer> p = findPeer(keyStr(friends_[idx].pub));
    if(!p) return;
    std::lock_guard<std::mutex> lk(p->callMu);
    VoiceCall* c = p->call;
    if(!c) return;
    switch(a){
    case CallAction::Call: c->call(); break;
    case CallAction::Accept: c->accept(); break;
    case CallAction::Hangup: c->hangup(); break;
    case CallAction::ToggleMute: c->setMuted(!c->muted()); break;
    case CallAction::ToggleDeafen: c->setDeafened(!c->deafened()); break;
    }
}

void NetHub::run(std::shared_ptr<Peer> p){
    const std::string key = p->keyS;
    const uint64_t g = p->gen;
    Session ses;
    std::string caps, err;
    if(p->adopt && p->adopt->respond){
        uint8_t tmp[kMaxPacket];
        Addr from;
        while(p->ch->recvFrom(tmp, sizeof tmp, from, 0) > 0){}
        if(!hsRespond(*p->ch, id_, capsNow(), ses, caps, err, 10000, p->key)){
            link_.close(*p->ch);
            post(key, kEnded, "They stopped trying to reach you for now. Click their name to connect.", g);
            p->done = true;
            return;
        }
    } else if(p->adopt){
        ses = p->adopt->ses;
        sessionWipe(p->adopt->ses);
    } else {
        bool ok = connectMasked(link_, *p->ch, id_, p->name, p->key, withHint(capsNow(), p->capsHint), ses, caps, err,
                                [this, key, g](const std::string& m){ post(key, kStatus, m, g); }, &p->quit, !isFriendKey(key) || !isMetKey(key));
        if(!ok || p->quit){
            if(ok) sessionWipe(ses);
            link_.close(*p->ch);
            post(key, kEnded, p->quit ? "You left the chat." : err, g);
            p->done = true;
            return;
        }
    }
    Engine e(*p->ch, ses);
    auto knockOn = [&e, p]{
        e.onReceive(kChanChat, [p](Lane, const uint8_t* d, size_t n){ if(n >= 9 && d[0] == 1) p->knock = true; });
        e.onReceive(kChanVoiceCtl, [p](Lane, const uint8_t* d, size_t n){ if(n >= 9 && d[0] == 1) p->knock = true; });
    };
    e.onReceive(kChanHub, [this, key, p](Lane, const uint8_t* d, size_t n){
        p->lastHub = tickMs();
        std::string bytes((const char*)d, n);
        std::lock_guard<std::mutex> lk(p->preMu);
        if(!p->announced){ if(p->pre.size() < 64) p->pre.push_back(bytes); return; }
        hubJob([key, bytes](HubCore& h){ h.onFrame((const uint8_t*)key.data(), (const uint8_t*)bytes.data(), bytes.size()); });
    });
    e.onReceive(kChanHubVoice, [this, key, p](Lane, const uint8_t* d, size_t n){
        p->lastHub = tickMs();
        hubVoiceIn(key, d, n);
    });
    knockOn();
    ChatIO io;
    io.nextOutgoing = [p](std::string& line){
        std::lock_guard<std::mutex> lk(p->outMu);
        if(p->out.empty()) return false;
        line = p->out.front();
        p->out.pop_front();
        return true;
    };
    io.onIncoming = [this, key, g](const std::string& text){ post(key, kIncoming, text, g); };
    io.onStatus = [this, key, g](const std::string& m){ post(key, kStatus, m, g); };
    std::unique_ptr<ChatModule> chat;
    std::unique_ptr<VoiceCall> call;
    auto chatOff = [&]{
        {
            std::lock_guard<std::mutex> lk(p->callMu);
            p->call = nullptr;
        }
        call.reset();
        chat.reset();
        knockOn();
    };
    {
        std::lock_guard<std::mutex> lk(p->engMu);
        p->eng = &e;
    }
    auto announce = [this, key, p](const std::string& verified){
        std::lock_guard<std::mutex> lk(p->preMu);
        hubJob([this, key, verified](HubCore& h){
            auto& u = hubUp_[key];
            if(!verified.empty()) u.second = verified;
            if(u.first++ == 0) h.peerUp((const uint8_t*)key.data(), u.second);
        });
        for(const std::string& b : p->pre) hubJob([key, b](HubCore& h){ h.onFrame((const uint8_t*)key.data(), (const uint8_t*)b.data(), b.size()); });
        p->pre.clear();
        p->verified = verified;
        p->announced = true;
    };
    std::thread verifier;
    p->up = true;
    if(isFriendKey(key)) p->wantChat = true;
    else p->lastHub = tickMs();
    if(p->name.empty() && p->adopt){
        std::string claimCaps = p->adopt->respond ? caps : p->adopt->caps;
        verifier = std::thread([this, p, claimCaps, announce]{
            std::string claimed, verified;
            if(normalizeName(capsName(claimCaps), claimed)){
                PointerRecord rec;
                std::string le;
                if(link_.lookup(claimed, p->key, rec, le) && std::memcmp(rec.owner, p->key, 32) == 0) verified = claimed;
            }
            announce(verified);
        });
    } else announce(p->name);
    std::string perr;
    bool failed = false;
    bool quitSent = false;
    for(;;){
        if(p->quit){
            if(chat && !chat->ended()){
                { std::lock_guard<std::mutex> lk(p->outMu); p->out.push_back("/quit"); }
                uint32_t until = tickMs() + 2500;
                while(!chat->ended() && (int32_t)(tickMs() - until) < 0){ chat->tick(); std::string pe; if(!e.poll(50, pe)) break; }
                e.flush(500);
                chatOff();
                post(key, kEnded, "You left the chat.", g);
            }
            break;
        }
        bool want = p->wantChat.exchange(false);
        bool knocked = p->knock.exchange(false);
        if(!chat && (want || knocked) && isFriendKey(key)){
            p->chatQuit = false;
            { std::lock_guard<std::mutex> lk(p->outMu); p->out.clear(); }
            chat.reset(new ChatModule(e, io));
            call.reset(new VoiceCall(e, std::memcmp(id_.pub, p->key, 32) < 0));
            call->setOnChange([this]{ if(wake) wake(); });
            {
                std::lock_guard<std::mutex> lk(p->callMu);
                p->call = call.get();
            }
            post(key, kConnected, "", g);
        } else if(!chat && knocked && tickMs() - p->lastReq.load() >= 30000){
            std::string vn;
            {
                std::lock_guard<std::mutex> lk(p->preMu);
                vn = p->verified;
            }
            if(!vn.empty()){
                p->lastReq = tickMs();
                post(key, kSessionReq, vn, 0);
            }
        }
        if(chat && p->chatQuit.exchange(false)){
            quitSent = true;
            std::lock_guard<std::mutex> lk(p->outMu);
            p->out.push_back("/quit");
        }
        if(chat){
            chat->tick();
            if(chat->ended()){
                bool ok = !chat->failed();
                bool mine = ok && quitSent;
                std::string ce = chat->error();
                if(ok) e.flush(500);
                chatOff();
                if(!ok) post(key, kEnded, ce, g);
                else post(key, mine ? kEnded : kPeerLeft, mine ? "You left the chat." : "", g);
                quitSent = false;
                if(tickMs() - p->lastHub.load() >= kIdleMs) break;
                continue;
            }
        }
        if(!e.poll(50, perr)){ failed = true; break; }
        if(!chat && tickMs() - p->lastHub.load() >= kIdleMs) break;
    }
    p->up = false;
    if(verifier.joinable()) verifier.join();
    bool wasAnnounced;
    {
        std::lock_guard<std::mutex> lk(p->preMu);
        wasAnnounced = p->announced;
    }
    if(wasAnnounced) hubJob([this, key](HubCore& h){
        auto it = hubUp_.find(key);
        if(it == hubUp_.end()) return;
        if(--it->second.first > 0) return;
        hubUp_.erase(it);
        h.peerDown((const uint8_t*)key.data());
    });
    {
        std::lock_guard<std::mutex> lk(p->engMu);
        p->eng = nullptr;
    }
    bool hadChat = (bool)chat;
    {
        std::lock_guard<std::mutex> lk(p->callMu);
        p->call = nullptr;
    }
    call.reset();
    chat.reset();
    for(uint8_t c : { (uint8_t)kChanChat, (uint8_t)kChanVoiceCtl, (uint8_t)kChanHub, (uint8_t)kChanHubVoice }) e.onReceive(c, nullptr);
    sessionWipe(ses);
    link_.close(*p->ch);
    if(hadChat) post(key, kEnded, failed ? perr : std::string("You left the chat."), g);
    p->done = true;
}

void NetHub::acceptLoop(){
    uint32_t lastTick = tickMs();
    while(!quit_){
        uint64_t c = 0;
        {
            std::unique_lock<std::mutex> g(inMu_);
            inCv_.wait_for(g, std::chrono::milliseconds(200), [this]{ return quit_.load() || !inQ_.empty(); });
            if(quit_) break;
            if(!inQ_.empty()){ c = inQ_.front(); inQ_.pop_front(); }
        }
        if(tickMs() - lastTick >= 5000){ lastTick = tickMs(); if(hasRequests_) post(std::string(), kTick, "", 0); }
        if(!c) continue;
        auto a = std::make_shared<Adopt>();
        a->ch.reset(new UdpSocket());
        link_.accept(c, *a->ch);
        uint8_t pkt[kMaxPacket];
        Addr from;
        int r = a->ch->recvFrom(pkt, sizeof pkt, from, 3000);
        std::string caps;
        uint64_t stamp = 0;
        if(r <= 0 || !hsPeek(pkt, (size_t)r, id_, a->key, caps, stamp)){ link_.close(*a->ch); continue; }
        std::string key = keyStr(a->key);
        std::string inv = capsValue(caps, "inv");
        bool known = isFriendKey(key);
        if(!known){
            uint8_t k[32];
            std::memcpy(k, a->key, 32);
            known = hubSync([k, inv](HubCore& h){ return h.knows(k) || (!inv.empty() && h.expectsJoin(k, inv)); });
        }
        if(!known){
            a->hint = capsName(caps);
            post(key, kRequestEv, "", 0, a);
            continue;
        }
        a->ch->unread(pkt, (size_t)r, from);
        std::string err;
        if(!hsRespond(*a->ch, id_, capsNow(), a->ses, a->caps, err, 10000, a->key)){ link_.close(*a->ch); continue; }
        post(key, kAdoptEv, "", 0, a);
    }
}

std::vector<RequestView> NetHub::requests() const {
    std::vector<RequestView> v;
    for(const Request& r : requests_){
        RequestView x;
        x.hint = r.hint;
        x.code = r.code;
        x.checking = r.checking;
        x.inHub = r.onSession;
        v.push_back(x);
    }
    return v;
}

void NetHub::ignoreRequest(size_t i){
    if(i >= requests_.size() || requests_[i].checking) return;
    if(requests_[i].ch) link_.close(*requests_[i].ch);
    requests_.erase(requests_.begin() + (long)i);
}

void NetHub::acceptRequest(size_t i){
    if(i >= requests_.size() || requests_[i].checking) return;
    Request& r = requests_[i];
    requestNote_.clear();
    if(r.hint.empty() || r.onSession){ post(std::string(), kChecked, "OK|" + r.code, 0); r.checking = true; return; }
    if(checkerBusy_) return;
    r.checking = true;
    std::string hint = r.hint, code = r.code;
    uint8_t key[32];
    std::memcpy(key, r.key, 32);
    if(checker_.joinable()) checker_.join();
    checkerBusy_ = true;
    checker_ = std::thread([this, hint, code, key]{
        PointerRecord rec;
        std::string e;
        bool ok = link_.lookup(hint, nullptr, rec, e) && std::memcmp(rec.owner, key, 32) == 0;
        checkerBusy_ = false;
        post(std::string(), kChecked, (ok ? "OK|" : "NO|") + code, 0);
    });
}

void NetHub::hubJob(std::function<void(HubCore&)> job){
    {
        std::lock_guard<std::mutex> g(hubMu_);
        if(hubJobs_.size() < 4096) hubJobs_.push_back(std::move(job));
    }
    hubCv_.notify_one();
}

bool NetHub::hubSync(std::function<bool(HubCore&)> fn){
    auto pr = std::make_shared<std::promise<bool>>();
    std::future<bool> fu = pr->get_future();
    hubJob([fn, pr](HubCore& h){ pr->set_value(fn(h)); });
    for(int i = 0; i < 40; i++){
        if(fu.wait_for(std::chrono::milliseconds(50)) == std::future_status::ready) return fu.get();
        if(quit_) return false;
    }
    return false;
}

bool NetHub::sendHub(const uint8_t peer[32], const uint8_t* p, size_t n){
    std::shared_ptr<Peer> pe = findPeer(keyStr(peer));
    if(!pe || pe->done) return false;
    pe->lastHub = tickMs();
    std::lock_guard<std::mutex> lk(pe->engMu);
    if(!pe->eng) return false;
    return pe->eng->send(Lane::Signal, kChanHub, p, n);
}

void NetHub::hubLoop(){
    std::unique_ptr<HubCore> hub(new HubCore(id_, nameNow(), dataFolder_));
    auto wire = [this](HubCore& h){
        h.setSender([this](const uint8_t peer[32], const uint8_t* p, size_t n){ return sendHub(peer, p, n); });
        h.setWanted([this](const uint8_t peer[32], const std::string& name, const std::string& hint){
            post(keyStr(peer), kWant, name + "|" + hint, 0);
        });
    };
    wire(*hub);
    uint32_t lastTick = 0, lastUses = 0, lastVoice = 0;
    while(!quit_){
        std::deque<std::function<void(HubCore&)>> jobs;
        {
            std::unique_lock<std::mutex> g(hubMu_);
            hubCv_.wait_for(g, std::chrono::milliseconds(100), [this]{ return quit_.load() || !hubJobs_.empty() || nameChanged_.load(); });
            if(quit_) break;
            jobs.swap(hubJobs_);
        }
        if(nameChanged_.exchange(false)){
            hub.reset(new HubCore(id_, nameNow(), dataFolder_));
            wire(*hub);
            for(auto& kv : hubUp_) hub->peerUp((const uint8_t*)kv.first.data(), kv.second.second);
        }
        for(auto& j : jobs) j(*hub);
        uint32_t now = tickMs();
        if(now - lastTick >= 100){ hub->tick(now); lastTick = now; }
        if(now - lastUses >= 1000){
            lastUses = now;
            std::vector<std::shared_ptr<Peer>> live;
            {
                std::lock_guard<std::mutex> g(peersMu_);
                for(auto& kv : peers_) if(kv.second->up) live.push_back(kv.second);
            }
            for(auto& p : live) if(hub->uses(p->key)) p->lastHub = now;
        }
        std::vector<HubEvent> ev = hub->drain();
        std::vector<HubView> views = hub->hubs();
        bool changed = !ev.empty();
        {
            std::lock_guard<std::mutex> g(snapMu_);
            if(views.size() != hubSnap_.size()) changed = true;
            else for(size_t i = 0; i < views.size() && !changed; i++){
                const HubView& a = views[i];
                const HubView& b = hubSnap_[i];
                if(std::memcmp(a.id, b.id, 32) != 0 || a.name != b.name || a.channels.size() != b.channels.size() || a.members.size() != b.members.size()) changed = true;
                else for(size_t m = 0; m < a.members.size() && !changed; m++)
                    if(a.members[m].online != b.members[m].online || a.members[m].voiceChannel != b.members[m].voiceChannel || a.members[m].role != b.members[m].role) changed = true;
            }
            hubSnap_ = views;
            for(HubEvent& e : ev) if(hubEvents_.size() < 1000) hubEvents_.push_back(e);
        }
        if(changed || now - lastVoice >= 1000){ lastVoice = now; refreshVoice(*hub); tuneVoice(); }
        if(changed) post(std::string(), kHubChanged, "", 0);
    }
    stopAudio();
    hub.reset();
}

void NetHub::refreshVoice(HubCore& h){
    bool stillIn = false;
    {
        std::lock_guard<std::mutex> g(voiceMu_);
        if(!voiceOn_){ voicePeers_.clear(); return; }
        for(const HubView& v : h.hubs()){
            if(std::memcmp(v.id, voiceHub_, 32) != 0) continue;
            for(const HubMemberView& m : v.members) if(std::memcmp(m.key, id_.pub, 32) == 0 && m.voiceChannel == voiceChan_) stillIn = true;
        }
    }
    if(!stillIn){ voiceOff(h); return; }
    std::lock_guard<std::mutex> g(voiceMu_);
    std::vector<VoicePeer> next;
    voiceNames_.clear();
    for(const HubView& v : h.hubs()){
        if(std::memcmp(v.id, voiceHub_, 32) != 0) continue;
        for(const HubChannelView& c : v.channels) if(c.id == voiceChan_) voiceChanName_ = safeText(c.name);
        for(const HubMemberView& m : v.members){
            if(m.voiceChannel != voiceChan_ || !m.online) continue;
            voiceNames_.push_back(safeText(m.name));
            if(std::memcmp(m.key, id_.pub, 32) == 0) continue;
            VoicePeer vp;
            vp.key = keyStr(m.key);
            if(!h.voiceStreamOut(m.key, vp.outStream)) continue;
            for(int s = 0; s < 256; s++){
                uint8_t hid[32];
                uint64_t vc = 0;
                if(h.voiceStreamIn(m.key, (uint8_t)s, hid, vc) && std::memcmp(hid, voiceHub_, 32) == 0 && vc == voiceChan_){ vp.inStream = s; break; }
            }
            for(const VoicePeer& old : voicePeers_) if(old.key == vp.key) vp.play = old.play;
            if(!vp.play) vp.play = std::make_shared<VoicePlayout>(60);
            next.push_back(vp);
        }
    }
    voicePeers_ = next;
}

void NetHub::tuneVoice(){
    std::vector<std::pair<std::string, std::shared_ptr<VoicePlayout>>> plays;
    {
        std::lock_guard<std::mutex> g(voiceMu_);
        for(const VoicePeer& vp : voicePeers_) plays.push_back({ vp.key, vp.play });
    }
    for(auto& pl : plays){
        std::shared_ptr<Peer> pe = findPeer(pl.first);
        if(!pe) continue;
        EngineStats st;
        {
            std::lock_guard<std::mutex> lk(pe->engMu);
            if(!pe->eng) continue;
            st = pe->eng->stats();
        }
        int target = 2 * st.jitterMs + st.voiceFrameMs;
        pl.second->setTargetMs(target < 40 ? 40 : target > 200 ? 200 : target);
    }
}

void NetHub::hubVoiceIn(const std::string& key, const uint8_t* p, size_t n){
    if(n < 1) return;
    std::lock_guard<std::mutex> g(voiceMu_);
    if(!voiceOn_) return;
    for(VoicePeer& vp : voicePeers_){
        if(vp.key != key || vp.inStream != (int)p[0]) continue;
        vp.play->push(p + 1, n - 1);
        return;
    }
}

void NetHub::startAudio(){
    std::string err;
    bool ok = audio_.start(
        [this](const float* frame){
            uint8_t body[1 + kVoiceHeader + kMaxOpusPacket];
            size_t n = enc_.packet(frame, body + 1, sizeof body - 1);
            if(!n) return;
            std::vector<std::pair<std::string, uint8_t>> targets;
            {
                std::lock_guard<std::mutex> g(voiceMu_);
                for(const VoicePeer& vp : voicePeers_) targets.push_back({ vp.key, vp.outStream });
            }
            for(auto& t : targets){
                std::shared_ptr<Peer> pe = findPeer(t.first);
                if(!pe) continue;
                pe->lastHub = tickMs();
                body[0] = t.second;
                std::lock_guard<std::mutex> lk(pe->engMu);
                if(pe->eng) pe->eng->send(Lane::Voice, kChanHubVoice, body, 1 + n);
            }
        },
        [this](float* frame){
            std::memset(frame, 0, sizeof(float) * kVoiceFrame);
            if(vDeafened_) return;
            std::vector<std::shared_ptr<VoicePlayout>> plays;
            {
                std::lock_guard<std::mutex> g(voiceMu_);
                for(const VoicePeer& vp : voicePeers_) plays.push_back(vp.play);
            }
            float tmp[kVoiceFrame];
            for(auto& pl : plays){
                pl->pull(tmp);
                for(int i = 0; i < kVoiceFrame; i++) frame[i] += tmp[i];
            }
            for(int i = 0; i < kVoiceFrame; i++) frame[i] = frame[i] > 1.0f ? 1.0f : frame[i] < -1.0f ? -1.0f : frame[i];
        },
        err);
    if(ok) audio_.setMuted(vMuted_);
    else post(std::string(), kHubNote, err.empty() ? std::string("Your microphone or speakers could not start.") : err, 0);
}

void NetHub::stopAudio(){
    audio_.stop();
}

void NetHub::voiceJoin(const uint8_t hub[32], uint64_t channel){
    uint8_t h[32];
    std::memcpy(h, hub, 32);
    hubJob([this, h, channel](HubCore& c){
        std::string e;
        if(!c.voiceJoin(h, channel, e)){ post(std::string(), kHubNote, e.empty() ? std::string("Could not join the voice channel.") : e, 0); return; }
        bool was;
        {
            std::lock_guard<std::mutex> g(voiceMu_);
            was = voiceOn_;
            voiceOn_ = true;
            std::memcpy(voiceHub_, h, 32);
            voiceChan_ = channel;
        }
        if(!was) startAudio();
        refreshVoice(c);
        post(std::string(), kHubChanged, "", 0);
    });
}

void NetHub::voiceOff(HubCore& c){
    {
        std::lock_guard<std::mutex> g(voiceMu_);
        if(!voiceOn_) return;
        voiceOn_ = false;
        voicePeers_.clear();
        voiceNames_.clear();
    }
    stopAudio();
    c.voiceLeave();
    post(std::string(), kHubChanged, "", 0);
}

void NetHub::voiceLeave(){
    hubJob([this](HubCore& c){ voiceOff(c); });
}

HubVoiceView NetHub::hubVoice(){
    HubVoiceView v;
    v.muted = vMuted_;
    v.deafened = vDeafened_;
    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> g(voiceMu_);
        v.active = voiceOn_;
        if(!voiceOn_) return v;
        std::memcpy(v.hub, voiceHub_, 32);
        v.channel = voiceChan_;
        v.channelName = voiceChanName_;
        v.names = voiceNames_;
        for(const VoicePeer& vp : voicePeers_) keys.push_back(vp.key);
    }
    int sum = 0, n = 0;
    for(const std::string& k : keys){
        std::shared_ptr<Peer> pe = findPeer(k);
        if(!pe) continue;
        std::lock_guard<std::mutex> lk(pe->engMu);
        if(!pe->eng) continue;
        int r = pe->eng->stats().rttMs;
        if(r > 0){ sum += r; n++; }
    }
    v.ping = n ? sum / n : 0;
    return v;
}

void NetHub::hubVoiceMute(){
    vMuted_ = !vMuted_;
    audio_.setMuted(vMuted_);
}

void NetHub::hubVoiceDeafen(){ vDeafened_ = !vDeafened_; }

void NetHub::createHub(const std::string& name){
    std::string n = cleanNick(name);
    if(n.empty()){ hubNote_ = "Give the hub a name."; return; }
    hubJob([this, n](HubCore& h){
        uint8_t id[32];
        std::string e;
        if(!h.create(n, id, e)) post(std::string(), kHubNote, e, 0);
    });
}

void NetHub::joinHub(const std::string& invite){
    std::string t;
    for(char c : invite) if(c != ' ' && c != '\r' && c != '\n' && c != '\t') t += c;
    if(t.empty()){ hubNote_ = "Paste an invite code first."; return; }
    hubJob([this, t](HubCore& h){
        std::string e;
        if(h.join(t, e)) post(std::string(), kHubInfo, "Joining... this takes a moment.", 0);
        else post(std::string(), kHubNote, e, 0);
    });
}

void NetHub::makeInvite(const uint8_t hub[32]){
    uint8_t h[32];
    std::memcpy(h, hub, 32);
    hubJob([this, h](HubCore& c){
        std::string e;
        std::string code = c.makeInvite(h, 10, 86400, e);
        if(code.empty()) post(std::string(), kHubNote, e, 0);
        else post(std::string(), kInvite, code, 0);
    });
}

void NetHub::postHub(const uint8_t hub[32], uint64_t channel, const std::string& text){
    uint8_t h[32];
    std::memcpy(h, hub, 32);
    std::string t = text;
    hubJob([this, h, channel, t](HubCore& c){
        std::string e;
        if(!c.post(h, channel, t, e)) post(std::string(), kHubNote, e, 0);
    });
}

void NetHub::manageHub(const uint8_t hub[32], ManageOp op, const ManageArgs& args){
    uint8_t h[32];
    std::memcpy(h, hub, 32);
    ManageArgs a = args;
    a.name = cleanNick(a.name);
    if((op == ManageOp::Rename || op == ManageOp::AddChannel || op == ManageOp::RenameChannel) && a.name.empty()){ hubNote_ = "Type a name first."; hubNoteErr_ = true; return; }
    hubJob([this, h, op, a](HubCore& c){
        std::string e;
        if(!c.manage(h, op, a, e)) post(std::string(), kHubNote, e, 0);
    });
}

void NetHub::leaveHub(const uint8_t hub[32]){
    uint8_t h[32];
    std::memcpy(h, hub, 32);
    hubJob([this, h](HubCore& c){
        bool here;
        {
            std::lock_guard<std::mutex> g(voiceMu_);
            here = voiceOn_ && std::memcmp(voiceHub_, h, 32) == 0;
        }
        if(here) voiceOff(c);
        std::string e;
        if(!c.leave(h, e)) post(std::string(), kHubNote, e, 0);
    });
}

void NetHub::applyHubSnapshot(){
    std::vector<HubView> views;
    std::deque<HubEvent> ev;
    {
        std::lock_guard<std::mutex> g(snapMu_);
        views = hubSnap_;
        ev.swap(hubEvents_);
    }
    std::vector<HubState> next;
    for(HubView& v : views){
        v.name = safeText(v.name);
        for(HubChannelView& c : v.channels) c.name = safeText(c.name);
        for(HubMemberView& m : v.members) m.name = safeText(m.name);
        HubState s;
        s.view = v;
        for(HubState& old : hubs_){
            if(std::memcmp(old.view.id, v.id, 32) != 0) continue;
            s.chans = std::move(old.chans);
            s.unread = old.unread;
        }
        for(const HubChannelView& c : v.channels){
            bool have = false;
            for(HubChannelLines& cl : s.chans) if(cl.channel == c.id) have = true;
            if(!have){ HubChannelLines cl; cl.channel = c.id; s.chans.push_back(cl); }
        }
        next.push_back(std::move(s));
    }
    hubs_ = std::move(next);
    for(const HubEvent& e : ev){
        if(e.kind == HubEvent::Error || e.kind == HubEvent::Joined || e.kind == HubEvent::Left){ if(!e.text.empty()){ hubNote_ = safeText(e.text); hubNoteErr_ = e.kind == HubEvent::Error; } continue; }
        if(e.kind != HubEvent::Message) continue;
        bool mine = std::memcmp(e.author, id_.pub, 32) == 0;
        for(HubState& hs : hubs_){
            if(std::memcmp(hs.view.id, e.hub, 32) != 0) continue;
            std::string who;
            for(const HubMemberView& m : hs.view.members) if(std::memcmp(m.key, e.author, 32) == 0) who = m.name;
            for(HubChannelLines& cl : hs.chans){
                if(cl.channel != e.channel) continue;
                ChatLine l;
                l.mine = mine;
                l.who = who.empty() ? friendCode(e.author).substr(0, 9) : who;
                l.text = safeText(e.text);
                l.time = clockNow();
                cl.lines.push_back(l);
                if(cl.lines.size() > 5000){ cl.lines.erase(cl.lines.begin(), cl.lines.begin() + 1000); cl.trimmed += 1000; }
                if(!mine){ cl.unread++; hs.unread++; }
            }
        }
    }
}

void NetHub::reap(){
    std::lock_guard<std::mutex> g(peersMu_);
    for(size_t i = 0; i < retired_.size();){
        if(retired_[i]->done){
            if(retired_[i]->t.joinable()) retired_[i]->t.join();
            retired_.erase(retired_.begin() + (long)i);
        } else i++;
    }
    for(auto it = peers_.begin(); it != peers_.end();){
        if(it->second->done && !it->second->up){
            if(it->second->t.joinable()) it->second->t.join();
            it = peers_.erase(it);
        } else ++it;
    }
}

bool NetHub::pump(){
    std::deque<Event> ev;
    { std::lock_guard<std::mutex> lk(mu_); ev.swap(events_); }
    bool changed = false;
    for(Event& e : ev){
        if(e.kind == kTick){
            uint32_t now = tickMs();
            for(size_t i = 0; i < requests_.size();){
                if(!requests_[i].checking && (int32_t)(now - requests_[i].expiresMs) >= 0){
                    if(requests_[i].ch) link_.close(*requests_[i].ch);
                    requests_.erase(requests_.begin() + (long)i);
                    changed = true;
                } else i++;
            }
            continue;
        }
        changed = true;
        if(e.kind == kHubChanged){ applyHubSnapshot(); continue; }
        if(e.kind == kHubNote || e.kind == kHubInfo){ hubNote_ = e.text; hubNoteErr_ = e.kind == kHubNote; continue; }
        if(e.kind == kInvite){ invite_ = e.text; continue; }
        if(e.kind == kWant){
            size_t bar = e.text.find('|');
            std::string name = bar == std::string::npos ? e.text : e.text.substr(0, bar);
            std::string hint = bar == std::string::npos ? std::string() : e.text.substr(bar + 1);
            ensurePeer(e.key, name, hint, true);
            continue;
        }
        if(e.kind == kSessionReq){
            bool merged = false;
            for(Request& r : requests_){
                if(std::memcmp(r.key, e.key.data(), 32) != 0) continue;
                if(!r.checking) r.expiresMs = tickMs() + kRequestLifeMs;
                merged = true;
                break;
            }
            if(merged || friendIdx(e.key) != (size_t)-1) continue;
            if(requests_.size() >= kMaxRequests){
                if(requests_.front().ch) link_.close(*requests_.front().ch);
                requests_.erase(requests_.begin());
            }
            Request r;
            std::memcpy(r.key, e.key.data(), 32);
            r.hint = safeText(e.text);
            r.code = friendCode(r.key);
            r.onSession = true;
            r.expiresMs = tickMs() + kRequestLifeMs;
            requests_.push_back(std::move(r));
            continue;
        }
        if(e.kind == kRequestEv){
            bool merged = false;
            for(Request& r : requests_){
                if(std::memcmp(r.key, e.adopt->key, 32) != 0) continue;
                if(r.checking){ link_.close(*e.adopt->ch); merged = true; break; }
                if(r.ch) link_.close(*r.ch);
                r.onSession = false;
                r.ch = std::move(e.adopt->ch);
                if(!e.adopt->hint.empty()) r.hint = e.adopt->hint;
                r.expiresMs = tickMs() + kRequestLifeMs;
                merged = true;
                break;
            }
            if(merged) continue;
            if(requests_.size() >= kMaxRequests){
                if(requests_.front().ch) link_.close(*requests_.front().ch);
                requests_.erase(requests_.begin());
            }
            Request r;
            std::memcpy(r.key, e.adopt->key, 32);
            r.hint = e.adopt->hint;
            r.code = friendCode(r.key);
            r.ch = std::move(e.adopt->ch);
            r.expiresMs = tickMs() + kRequestLifeMs;
            requests_.push_back(std::move(r));
            continue;
        }
        if(e.kind == kChecked){
            bool ok = e.text.compare(0, 3, "OK|") == 0;
            std::string code = e.text.substr(3);
            for(size_t i = 0; i < requests_.size(); i++){
                if(requests_[i].code != code) continue;
                Request r = std::move(requests_[i]);
                requests_.erase(requests_.begin() + (long)i);
                if(!ok){
                    requestNote_ = "This person's key does not match @" + r.hint + "'s public record, so they were not added.";
                    if(r.ch) link_.close(*r.ch);
                    break;
                }
                std::string key = keyStr(r.key);
                size_t idx = friendIdx(key);
                if(idx == (size_t)-1){
                    Friend fr;
                    std::memcpy(fr.pub, r.key, 32);
                    fr.code = r.code;
                    fr.name = r.hint;
                    fr.nick = r.hint.empty() ? r.code.substr(0, 9) : r.hint;
                    friends_.push_back(fr);
                    saveFriends();
                    syncFriendKeys();
                    idx = friends_.size() - 1;
                }
                if(r.onSession){
                    ensurePeer(key, friends_[idx].name, std::string(), false);
                    requestNote_ = "Added " + friends_[idx].nick + ".";
                    break;
                }
                std::shared_ptr<Peer> cur = findPeer(key);
                if(cur && !cur->done){ link_.close(*r.ch); break; }
                auto a = std::make_shared<Adopt>();
                a->ch = std::move(r.ch);
                a->respond = true;
                std::memcpy(a->key, r.key, 32);
                startPeer(key, friends_[idx].name, std::string(), a);
                requestNote_ = "Added " + friends_[idx].nick + ".";
                break;
            }
            continue;
        }
        if(e.kind == kFound){
            finding_ = false;
            if(e.text.compare(0, 3, "OK|") == 0){ foundCode_ = e.text.substr(3); foundErr_.clear(); }
            else { foundErr_ = e.text.size() > 4 ? e.text.substr(4) : std::string("Not found."); foundCode_.clear(); }
            continue;
        }
        if(e.kind == kAdoptEv){
            std::shared_ptr<Peer> cur = findPeer(e.key);
            bool busy = cur && !cur->done;
            bool lower = std::memcmp(id_.pub, e.key.data(), 32) < 0;
            if(busy && lower && cur->own){ link_.close(*e.adopt->ch); continue; }
            if(busy) cur->quit = true;
            size_t fi = friendIdx(e.key);
            startPeer(e.key, fi == (size_t)-1 ? std::string() : friends_[fi].name, std::string(), e.adopt);
            continue;
        }
        size_t idx = friendIdx(e.key);
        if(idx == (size_t)-1) continue;
        std::shared_ptr<Peer> cur = findPeer(e.key);
        if(!cur || cur->gen != e.gen) continue;
        Friend& f = friends_[idx];
        switch(e.kind){
        case kStatus: f.status = e.text; break;
        case kIncoming: {
            ChatLine l;
            l.text = safeText(e.text);
            l.time = clockNow();
            f.lines.push_back(l);
            if(f.lines.size() > 5000) f.lines.erase(f.lines.begin(), f.lines.begin() + 1000);
            f.unread++;
            break; }
        case kConnected:
            f.state = LinkState::Connected;
            f.status = kConnectedText;
            if(!f.met){
                f.met = true;
                saveFriends();
                syncFriendKeys();
            }
            break;
        case kEnded:
        case kPeerLeft: {
            f.state = LinkState::Ended;
            f.status = e.kind == kPeerLeft ? f.nick + " left the chat." : e.text;
            ChatLine l;
            l.system = true;
            l.text = f.status;
            l.time = clockNow();
            f.lines.push_back(l);
            break; }
        default: break;
        }
    }
    hasRequests_ = !requests_.empty();
    reap();
    return changed;
}

void NetHub::shutdown(){
    bridge_.stop();
    quit_ = true;
    inCv_.notify_all();
    hubCv_.notify_all();
    std::vector<std::shared_ptr<Peer>> all;
    {
        std::lock_guard<std::mutex> g(peersMu_);
        for(auto& kv : peers_) all.push_back(kv.second);
        for(auto& r : retired_) all.push_back(r);
    }
    for(auto& p : all){
        p->quit = true;
        link_.wake(*p->ch);
    }
    uint32_t until = tickMs() + 1500;
    for(;;){
        bool busy = false;
        for(auto& p : all) if(!p->done) busy = true;
        if(!busy || (int32_t)(tickMs() - until) >= 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    for(Request& r : requests_) if(r.ch) link_.close(*r.ch);
    requests_.clear();
    link_.stop();
    for(auto& p : all){
        while(!p->done){
            link_.wake(*p->ch);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if(p->t.joinable()) p->t.join();
    }
    {
        std::lock_guard<std::mutex> g(peersMu_);
        peers_.clear();
        retired_.clear();
    }
    if(hubThread_.joinable()) hubThread_.join();
    if(acceptor_.joinable()) acceptor_.join();
    if(finder_.joinable()) finder_.join();
    if(checker_.joinable()) checker_.join();
}

}

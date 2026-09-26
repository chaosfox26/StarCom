#include "nethub.h"
#include "../chat.h"
#include "../hwprobe.h"
#include "../p2p.h"
#include "../paths.h"
#include "../textsafe.h"
#include "../seed.h"
#include <atomic>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace sc {

namespace {

const char* kIdFile = "identity.key";
const char* kConf = "starcom.conf";
const char* kFriends = "friends.txt";

enum Kind { kStatus = 0, kIncoming = 1, kDirect = 2, kRelay = 3, kEnded = 4 };

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
    if(o.size() > 32) o.resize(32);
    return o;
}

}

struct NetHub::Worker {
    std::mutex mu;
    std::deque<std::string> out;
    std::atomic<bool> quit{false};
    std::atomic<bool> done{false};
    std::thread t;
    OfflineNotice notice;
    uint8_t pub[32] = {0};
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
    server_ = meetingPoint(kConf);
    hideIp_ = configValue(kConf, "hideip") == "1";
    if(server_.empty()) serverNote_ = "This copy of StarCom has no meeting point built in, so it cannot find friends yet. Ask whoever gave it to you for a copy that has one.";
    std::ifstream f(kFriends);
    std::string line;
    while(std::getline(f, line)){
        if(!line.empty() && line.back() == '\r') line.pop_back();
        size_t sp = line.find(' ');
        std::string code = sp == std::string::npos ? line : line.substr(0, sp);
        std::string nick = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        Friend fr;
        if(!parseFriendCode(code, fr.pub)) continue;
        fr.code = code;
        fr.nick = cleanNick(nick).empty() ? code.substr(0, 9) : cleanNick(nick);
        friends_.push_back(fr);
        workers_.push_back(nullptr);
    }
    return true;
}

void NetHub::saveFriends(){
    std::string tmp = std::string(kFriends) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        for(const Friend& fr : friends_) f << fr.code << ' ' << fr.nick << '\n';
        if(!f) return;
    }
#ifdef _WIN32
    MoveFileExA(tmp.c_str(), kFriends, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    std::rename(tmp.c_str(), kFriends);
#endif
}

bool NetHub::addFriend(const std::string& codeIn, const std::string& nickIn, std::string& err){
    std::string code;
    for(char c : codeIn) if(c != ' ' && c != '\r' && c != '\n' && c != '\t') code += c;
    Friend fr;
    if(!parseFriendCode(code, fr.pub)){ err = "That is not a valid friend code."; return false; }
    if(code == myCode_ || friendCode(fr.pub) == myCode_){ err = "That is your own friend code."; return false; }
    for(const Friend& o : friends_) if(o.code == friendCode(fr.pub)){ err = "That friend is already in your list."; return false; }
    fr.code = friendCode(fr.pub);
    fr.nick = cleanNick(nickIn).empty() ? fr.code.substr(0, 9) : cleanNick(nickIn);
    friends_.push_back(fr);
    workers_.push_back(nullptr);
    saveFriends();
    return true;
}

void NetHub::post(size_t idx, int kind, const std::string& text){
    { std::lock_guard<std::mutex> lk(mu_); events_.push_back(Event{ idx, kind, text }); }
    if(wake) wake();
}

void NetHub::open(size_t idx){
    if(idx >= friends_.size()) return;
    if(workers_[idx] && !workers_[idx]->done) return;
    if(workers_[idx] && workers_[idx]->t.joinable()) workers_[idx]->t.join();
    if(server_.empty()){ friends_[idx].status = serverNote_; return; }
    auto w = std::make_shared<Worker>();
    std::memcpy(w->pub, friends_[idx].pub, 32);
    workers_[idx] = w;
    friends_[idx].state = LinkState::Connecting;
    friends_[idx].status = "Looking for " + friends_[idx].nick + "...";
    w->t = std::thread([this, idx, w]{ run(idx, w); });
}

void NetHub::send(size_t idx, const std::string& text){
    if(idx >= friends_.size() || !workers_[idx] || workers_[idx]->done) return;
    { std::lock_guard<std::mutex> lk(workers_[idx]->mu); workers_[idx]->out.push_back(text); }
    ChatLine l;
    l.mine = true;
    l.text = text;
    l.time = clockNow();
    friends_[idx].lines.push_back(l);
}

void NetHub::close(size_t idx){
    if(idx >= friends_.size() || !workers_[idx]) return;
    workers_[idx]->quit = true;
    std::lock_guard<std::mutex> lk(workers_[idx]->mu);
    workers_[idx]->out.push_back("/quit");
}

void NetHub::run(size_t idx, std::shared_ptr<Worker> w){
    const uint8_t* pub = w->pub;
    std::string rv = server_;
    size_t colon = rv.rfind(':');
    std::vector<Addr> addrs;
    std::string err;
    int port = colon == std::string::npos ? 0 : std::atoi(rv.substr(colon + 1).c_str());
    if(colon == std::string::npos || port <= 0 || port > 65535 || !resolve(rv.substr(0, colon), (uint16_t)port, addrs, err)){
        post(idx, kEnded, "Cannot reach the meeting point: " + (err.empty() ? std::string("bad rendezvous= setting") : err));
        w->done = true;
        return;
    }
    Addr server = addrs[0];
    for(const Addr& a : addrs) if(a.family() == familyV4()){ server = a; break; }
    UdpSocket s;
    if(!s.open(server.family(), 0, err)){ post(idx, kEnded, err); w->done = true; return; }
    w->notice.arm(s, id_, pub, server);
    Session ses;
    std::string peerCaps;
    std::string myCaps = capsJson(probeHardware());
    bool ok = connectFriend(s, id_, pub, server, myCaps, hideIp_, ses, peerCaps, err, [this, idx](const std::string& m){ post(idx, kStatus, m); }, &w->quit);
    if(!ok || w->quit){
        w->notice.send();
        post(idx, kEnded, ok ? "Closed." : err);
        w->done = true;
        return;
    }
    std::string mode;
    post(idx, ses.peer == server ? kRelay : kDirect, "");
    ChatIO io;
    io.nextOutgoing = [w](std::string& line){
        std::lock_guard<std::mutex> lk(w->mu);
        if(w->out.empty()) return false;
        line = w->out.front();
        w->out.pop_front();
        return true;
    };
    io.onIncoming = [this, idx](const std::string& text){ post(idx, kIncoming, text); };
    io.onStatus = [this, idx](const std::string& m){ post(idx, kStatus, m); };
    bool clean = runChat(s, ses, io, err);
    sessionWipe(ses);
    w->notice.send();
    post(idx, kEnded, clean ? "Chat closed." : err);
    w->done = true;
}

bool NetHub::pump(){
    std::deque<Event> ev;
    { std::lock_guard<std::mutex> lk(mu_); ev.swap(events_); }
    for(const Event& e : ev){
        if(e.idx >= friends_.size()) continue;
        Friend& f = friends_[e.idx];
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
        case kDirect: f.state = LinkState::Direct; f.status = "Connected directly. Your friend can see your IP address."; break;
        case kRelay: f.state = LinkState::Relay; f.status = "Connected through the relay. Your friend cannot see your IP address."; break;
        case kEnded: {
            f.state = LinkState::Ended;
            f.status = e.text;
            ChatLine l;
            l.system = true;
            l.text = e.text;
            l.time = clockNow();
            f.lines.push_back(l);
            break; }
        default: break;
        }
    }
    return !ev.empty();
}

void NetHub::shutdown(){
    for(size_t i = 0; i < workers_.size(); i++){
        if(!workers_[i]) continue;
        close(i);
    }
    for(auto& w : workers_){
        if(w && w->t.joinable()) w->t.join();
    }
    workers_.clear();
}

}

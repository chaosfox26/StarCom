#include "nethub.h"
#include "../chat.h"
#include "../dht.h"
#include "../engine.h"
#include "../hwprobe.h"
#include "../textsafe.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace sc {

namespace {

const char* kIdFile = "identity.key";
const char* kFriends = "friends.txt";
const char* kNameFile = "name.txt";
const size_t kNoFriend = (size_t)-1;

enum Kind { kStatus = 0, kIncoming = 1, kConnected = 2, kEnded = 4, kPeerLeft = 5, kFound = 6, kAdoptEv = 7, kRequestEv = 8, kChecked = 9, kTick = 10 };

const uint32_t kRequestLifeMs = 60000;
const size_t kMaxRequests = 5;

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
    if(o.size() > 32) o.resize(32);
    return o;
}

std::string readName(){
    std::ifstream f(kNameFile);
    std::string line, n;
    if(std::getline(f, line) && normalizeName(line, n)) return n;
    return std::string();
}

bool replaceFile(const std::string& tmp, const char* dest){
#ifdef _WIN32
    return MoveFileExA(tmp.c_str(), dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(tmp.c_str(), dest) == 0;
#endif
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

struct NetHub::Worker {
    std::mutex mu;
    std::deque<std::string> out;
    std::atomic<bool> quit{false};
    std::atomic<bool> done{false};
    std::thread t;
    uint8_t pub[32] = {0};
    std::string name;
    uint64_t gen = 0;
    bool own = true;
    std::unique_ptr<UdpSocket> ch;
    std::shared_ptr<Adopt> adopt;
    std::mutex callMu;
    VoiceCall* call = nullptr;
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
        size_t a = line.find(' ');
        std::string code = a == std::string::npos ? line : line.substr(0, a);
        Friend fr;
        if(!parseFriendCode(code, fr.pub)) continue;
        std::string rest = a == std::string::npos ? std::string() : line.substr(a + 1);
        size_t b = rest.rfind(' ');
        std::string nm;
        if(b != std::string::npos && normalizeName(rest.substr(b + 1), nm)){ fr.name = nm; rest = rest.substr(0, b); }
        fr.code = friendCode(fr.pub);
        fr.nick = cleanNick(rest).empty() ? (fr.name.empty() ? fr.code.substr(0, 9) : fr.name) : cleanNick(rest);
        friends_.push_back(fr);
        workers_.push_back(nullptr);
    }
    syncPubs();
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
    myName_ = n;
    myCaps_ = capsJson(probeHardware(), myName_);
    link_.start(id_, myName_);
    return true;
}

void NetHub::syncPubs(){
    std::lock_guard<std::mutex> g(pubsMu_);
    pubs_.clear();
    for(const Friend& fr : friends_) pubs_.push_back(std::vector<uint8_t>(fr.pub, fr.pub + 32));
}

void NetHub::saveFriends(){
    std::string tmp = std::string(kFriends) + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        for(const Friend& fr : friends_) f << fr.code << ' ' << fr.nick << (fr.name.empty() ? std::string() : " " + fr.name) << '\n';
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
        else if(link_.lookup(n, nullptr, rec, e)){ post(kNoFriend, kFound, "OK|" + friendCode(rec.owner), 0); return; }
        post(kNoFriend, kFound, "ERR|" + e, 0);
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
    workers_.push_back(nullptr);
    saveFriends();
    syncPubs();
    foundName_.clear();
    foundCode_.clear();
    foundErr_.clear();
    return true;
}

void NetHub::post(size_t idx, int kind, const std::string& text, uint64_t gen, std::shared_ptr<Adopt> adopt){
    { std::lock_guard<std::mutex> lk(mu_); events_.push_back(Event{ idx, kind, text, gen, std::move(adopt) }); }
    if(wake) wake();
}

void NetHub::startWorker(size_t idx, std::shared_ptr<Adopt> adopt){
    if(workers_[idx]) retired_.push_back(workers_[idx]);
    Friend& f = friends_[idx];
    auto w = std::make_shared<Worker>();
    std::memcpy(w->pub, f.pub, 32);
    w->name = f.name;
    w->gen = nextGen_++;
    w->own = !adopt;
    if(adopt){
        w->ch = std::move(adopt->ch);
        w->adopt = adopt;
    } else w->ch.reset(new UdpSocket());
    workers_[idx] = w;
    bool ready = adopt && !adopt->respond;
    f.state = ready ? LinkState::Connected : LinkState::Connecting;
    f.status = ready ? std::string(kConnectedText) : "Looking for @" + (f.name.empty() ? f.nick : f.name) + "...";
    w->t = std::thread([this, idx, w]{ run(idx, w); });
}

void NetHub::open(size_t idx){
    if(idx >= friends_.size()) return;
    Friend& f = friends_[idx];
    if(f.name.empty()){ f.status = "Add this friend again by their @name to reach them."; return; }
    if(workers_[idx] && !workers_[idx]->done) return;
    startWorker(idx, nullptr);
}

void NetHub::send(size_t idx, const std::string& text){
    if(idx >= friends_.size() || !workers_[idx] || workers_[idx]->done) return;
    if(text == "/quit"){ close(idx); return; }
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

CallView NetHub::callView(size_t idx){
    CallView v;
    if(idx >= workers_.size() || !workers_[idx]) return v;
    std::lock_guard<std::mutex> lk(workers_[idx]->callMu);
    VoiceCall* c = workers_[idx]->call;
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
    if(idx >= workers_.size() || !workers_[idx]) return;
    std::lock_guard<std::mutex> lk(workers_[idx]->callMu);
    VoiceCall* c = workers_[idx]->call;
    if(!c) return;
    switch(a){
    case CallAction::Call: c->call(); break;
    case CallAction::Accept: c->accept(); break;
    case CallAction::Hangup: c->hangup(); break;
    case CallAction::ToggleMute: c->setMuted(!c->muted()); break;
    case CallAction::ToggleDeafen: c->setDeafened(!c->deafened()); break;
    }
}

void NetHub::chatOn(size_t idx, std::shared_ptr<Worker> w, Session& ses){
    ChatIO io;
    io.nextOutgoing = [w](std::string& line){
        std::lock_guard<std::mutex> lk(w->mu);
        if(w->out.empty()) return false;
        line = w->out.front();
        w->out.pop_front();
        return true;
    };
    uint64_t g = w->gen;
    io.onIncoming = [this, idx, g](const std::string& text){ post(idx, kIncoming, text, g); };
    io.onStatus = [this, idx, g](const std::string& m){ post(idx, kStatus, m, g); };
    std::string err;
    bool clean;
    {
        Engine e(*w->ch, ses);
        VoiceCall vc(e, std::memcmp(id_.pub, w->pub, 32) < 0);
        vc.setOnChange([this]{ if(wake) wake(); });
        { std::lock_guard<std::mutex> lk(w->callMu); w->call = &vc; }
        clean = runChatWith(e, io, err);
        { std::lock_guard<std::mutex> lk(w->callMu); w->call = nullptr; }
    }
    sessionWipe(ses);
    link_.close(*w->ch);
    if(clean && !w->quit) post(idx, kPeerLeft, "", g);
    else post(idx, kEnded, clean || w->quit ? "You left the chat." : err, g);
    w->done = true;
}

void NetHub::run(size_t idx, std::shared_ptr<Worker> w){
    if(w->adopt && w->adopt->respond){
        uint8_t tmp[kMaxPacket];
        Addr from;
        while(w->ch->recvFrom(tmp, sizeof tmp, from, 0) > 0){}
        Session ses;
        std::string caps, err;
        if(!hsRespond(*w->ch, id_, myCaps_, ses, caps, err, 10000, w->adopt->key)){
            link_.close(*w->ch);
            post(idx, kEnded, "They stopped trying to reach you for now. Click their name to connect.", w->gen);
            w->done = true;
            return;
        }
        post(idx, kConnected, "", w->gen);
        chatOn(idx, w, ses);
        return;
    }
    if(w->adopt){
        Session ses = w->adopt->ses;
        sessionWipe(w->adopt->ses);
        post(idx, kConnected, "", w->gen);
        chatOn(idx, w, ses);
        return;
    }
    Session ses;
    std::string caps, err;
    uint64_t g = w->gen;
    bool ok = connectMasked(link_, *w->ch, id_, w->name, w->pub, myCaps_, ses, caps, err,
                            [this, idx, g](const std::string& m){ post(idx, kStatus, m, g); }, &w->quit);
    if(!ok || w->quit){
        if(ok) sessionWipe(ses);
        link_.close(*w->ch);
        post(idx, kEnded, w->quit ? "You left the chat." : err, g);
        w->done = true;
        return;
    }
    post(idx, kConnected, "", g);
    chatOn(idx, w, ses);
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
        if(tickMs() - lastTick >= 5000){ lastTick = tickMs(); post(kNoFriend, kTick, "", 0); }
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
        size_t idx = kNoFriend;
        {
            std::lock_guard<std::mutex> g(pubsMu_);
            for(size_t i = 0; i < pubs_.size(); i++) if(std::memcmp(pubs_[i].data(), a->key, 32) == 0){ idx = i; break; }
        }
        if(idx == kNoFriend){
            a->hint = capsName(caps);
            post(kNoFriend, kRequestEv, "", 0, a);
            continue;
        }
        a->ch->unread(pkt, (size_t)r, from);
        std::string err;
        if(!hsRespond(*a->ch, id_, myCaps_, a->ses, a->caps, err, 10000, a->key)){ link_.close(*a->ch); continue; }
        post(idx, kAdoptEv, "", 0, a);
    }
}

std::vector<RequestView> NetHub::requests() const {
    std::vector<RequestView> v;
    for(const Request& r : requests_){
        RequestView x;
        x.hint = r.hint;
        x.code = r.code;
        x.checking = r.checking;
        v.push_back(x);
    }
    return v;
}

void NetHub::ignoreRequest(size_t i){
    if(i >= requests_.size() || requests_[i].checking) return;
    link_.close(*requests_[i].ch);
    requests_.erase(requests_.begin() + (long)i);
}

void NetHub::acceptRequest(size_t i){
    if(i >= requests_.size() || requests_[i].checking) return;
    Request& r = requests_[i];
    requestNote_.clear();
    if(r.hint.empty()){ post(kNoFriend, kChecked, "OK|" + r.code, 0); r.checking = true; return; }
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
        post(kNoFriend, kChecked, (ok ? "OK|" : "NO|") + code, 0);
    });
}

void NetHub::reap(){
    for(size_t i = 0; i < retired_.size();){
        if(retired_[i]->done){
            if(retired_[i]->t.joinable()) retired_[i]->t.join();
            retired_.erase(retired_.begin() + (long)i);
        } else i++;
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
                    link_.close(*requests_[i].ch);
                    requests_.erase(requests_.begin() + (long)i);
                    changed = true;
                } else i++;
            }
            continue;
        }
        changed = true;
        if(e.kind == kRequestEv){
            bool merged = false;
            for(Request& r : requests_){
                if(std::memcmp(r.key, e.adopt->key, 32) != 0) continue;
                link_.close(*r.ch);
                r.ch = std::move(e.adopt->ch);
                if(!e.adopt->hint.empty()) r.hint = e.adopt->hint;
                r.expiresMs = tickMs() + kRequestLifeMs;
                merged = true;
                break;
            }
            if(merged) continue;
            if(requests_.size() >= kMaxRequests){
                link_.close(*requests_.front().ch);
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
                    link_.close(*r.ch);
                    break;
                }
                size_t idx = friends_.size();
                for(size_t k = 0; k < friends_.size(); k++) if(std::memcmp(friends_[k].pub, r.key, 32) == 0) idx = k;
                if(idx == friends_.size()){
                    Friend fr;
                    std::memcpy(fr.pub, r.key, 32);
                    fr.code = r.code;
                    fr.name = r.hint;
                    fr.nick = r.hint.empty() ? r.code.substr(0, 9) : r.hint;
                    friends_.push_back(fr);
                    workers_.push_back(nullptr);
                    saveFriends();
                    syncPubs();
                }
                auto a = std::make_shared<Adopt>();
                a->ch = std::move(r.ch);
                a->respond = true;
                std::memcpy(a->key, r.key, 32);
                if(workers_[idx] && !workers_[idx]->done){ link_.close(*a->ch); break; }
                startWorker(idx, a);
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
        if(e.idx >= friends_.size()){ if(e.adopt) link_.close(*e.adopt->ch); continue; }
        Friend& f = friends_[e.idx];
        if(e.kind == kAdoptEv){
            std::shared_ptr<Worker>& w = workers_[e.idx];
            bool busy = w && !w->done;
            bool lower = std::memcmp(id_.pub, f.pub, 32) < 0;
            if(busy && lower && w->own){ link_.close(*e.adopt->ch); continue; }
            if(busy){
                w->quit = true;
                { std::lock_guard<std::mutex> lk(w->mu); w->out.push_back("/quit"); }
            }
            startWorker(e.idx, e.adopt);
            continue;
        }
        if(!workers_[e.idx] || workers_[e.idx]->gen != e.gen) continue;
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
        case kConnected: f.state = LinkState::Connected; f.status = kConnectedText; break;
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
    reap();
    return changed;
}

void NetHub::shutdown(){
    quit_ = true;
    inCv_.notify_all();
    for(size_t i = 0; i < workers_.size(); i++) if(workers_[i]) close(i);
    for(auto& w : workers_) if(w && w->t.joinable()) w->t.join();
    for(auto& w : retired_) if(w && w->t.joinable()) w->t.join();
    workers_.clear();
    retired_.clear();
    if(acceptor_.joinable()) acceptor_.join();
    if(finder_.joinable()) finder_.join();
    if(checker_.joinable()) checker_.join();
    for(Request& r : requests_) link_.close(*r.ch);
    requests_.clear();
    link_.stop();
}

}

#include "chat.h"
#include <chrono>
#include <cstring>
#include <deque>
#include <map>

namespace sc {

namespace {

using Clock = std::chrono::steady_clock;

enum Kind : uint8_t { kText = 1, kClose = 2, kPing = 3, kAck = 4 };

const int kKeepaliveMs = 25000;
const int kSilentMs = 75000;
const int kFirstRtoMs = 500;
const int kMaxRtoMs = 4000;
const int kMaxTries = 10;
const size_t kMaxInFlight = 64;
const size_t kMaxQueued = 256;
const uint64_t kMaxAhead = 256;

struct Pending {
    uint64_t seq = 0;
    std::string text;
    Clock::time_point due;
    int rto = kFirstRtoMs;
    int tries = 0;
};

long long msSince(Clock::time_point t){
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

struct Pacer {
    double tokens = 20;
    Clock::time_point last = Clock::now();
    bool take(){
        auto now = Clock::now();
        tokens += std::chrono::duration<double>(now - last).count() * 5.0;
        if(tokens > 20) tokens = 20;
        last = now;
        if(tokens < 1) return false;
        tokens -= 1;
        return true;
    }
};

class Chat {
public:
    Chat(UdpSocket& s, Session& ses, ChatIO& io) : s_(s), ses_(ses), io_(io) {}

    bool run(std::string& err){
        lastRecv_ = Clock::now();
        lastSend_ = Clock::now();
        uint8_t in[kMaxPacket], plain[kMaxPacket];
        for(;;){
            std::string line;
            while(!closing_ && queue_.size() < kMaxQueued && io_.nextOutgoing && io_.nextOutgoing(line)){
                if(line.empty()) continue;
                if(line == "/quit"){
                    closing_ = true;
                    closeAt_ = Clock::now() + std::chrono::milliseconds(2000);
                    break;
                }
                if(line.size() > kMaxChatText){
                    status("message too long (" + std::to_string(line.size()) + " bytes, limit " + std::to_string(kMaxChatText) + ")");
                    continue;
                }
                queue_.push_back(line);
            }
            if(closing_ && ((queue_.empty() && inFlight_.empty()) || Clock::now() >= closeAt_)){
                if(!queue_.empty() || !inFlight_.empty()) status(std::to_string(queue_.size() + inFlight_.size()) + " message(s) were not confirmed before leaving");
                sendControl(kClose, 0);
                sendControl(kClose, 0);
                status("you left the chat");
                return true;
            }
            while(!queue_.empty() && inFlight_.size() < kMaxInFlight && pacer_.take()){
                Pending p;
                p.seq = nextSeq_++;
                p.text = queue_.front();
                queue_.pop_front();
                p.due = Clock::now();
                inFlight_.push_back(p);
            }
            for(Pending& p : inFlight_){
                if(Clock::now() < p.due) continue;
                if(p.tries >= kMaxTries){
                    err = "the peer stopped acknowledging messages";
                    return false;
                }
                sendText(p);
                p.tries++;
                p.due = Clock::now() + std::chrono::milliseconds(p.rto);
                if(p.rto < kMaxRtoMs) p.rto *= 2;
            }
            if(msSince(lastSend_) >= kKeepaliveMs) sendControl(kPing, 0);
            if(msSince(lastRecv_) >= kSilentMs){
                err = "connection lost (no reply for " + std::to_string(kSilentMs / 1000) + " s)";
                return false;
            }
            Addr from;
            int r = s_.recvFrom(in, sizeof in, from, 50);
            if(r <= 0) continue;
            Lane lane;
            size_t n = 0;
            if(!sessionOpen(ses_, in, (size_t)r, from, lane, plain, sizeof plain, n)) continue;
            lastRecv_ = Clock::now();
            if(n == 0 || lane != Lane::Signal) continue;
            if(!handle(plain, n)) return true;
        }
    }

private:
    UdpSocket& s_;
    Session& ses_;
    ChatIO& io_;
    Clock::time_point lastRecv_, lastSend_;
    std::deque<std::string> queue_;
    std::deque<Pending> inFlight_;
    std::map<uint64_t, std::string> early_;
    uint64_t nextSeq_ = 1;
    uint64_t expect_ = 1;
    Pacer pacer_;
    bool closing_ = false;
    Clock::time_point closeAt_;

    void status(const std::string& m){
        if(io_.onStatus) io_.onStatus(m);
    }

    void send(const uint8_t* body, size_t n){
        uint8_t pkt[kMaxPacket];
        size_t len = 0;
        if(!sessionSeal(ses_, Lane::Signal, body, n, pkt, sizeof pkt, len)) return;
        s_.sendTo(pkt, len, ses_.peer);
        lastSend_ = Clock::now();
    }

    void sendControl(Kind k, uint64_t seq){
        uint8_t body[9];
        body[0] = k;
        putU64(body + 1, seq);
        send(body, sizeof body);
    }

    void sendText(const Pending& p){
        uint8_t body[9 + kMaxChatText];
        body[0] = kText;
        putU64(body + 1, p.seq);
        std::memcpy(body + 9, p.text.data(), p.text.size());
        send(body, 9 + p.text.size());
    }

    bool handle(const uint8_t* b, size_t n){
        if(n < 9) return true;
        uint64_t seq = getU64(b + 1);
        switch(b[0]){
        case kText:
            if(n - 9 > kMaxChatText) return true;
            if(seq >= expect_ && seq - expect_ > kMaxAhead) return true;
            sendControl(kAck, seq);
            if(seq < expect_) return true;
            early_.emplace(seq, std::string((const char*)b + 9, n - 9));
            for(auto it = early_.find(expect_); it != early_.end(); it = early_.find(expect_)){
                if(io_.onIncoming) io_.onIncoming(it->second);
                early_.erase(it);
                expect_++;
            }
            return true;
        case kAck:
            for(auto it = inFlight_.begin(); it != inFlight_.end(); ++it){
                if(it->seq == seq){ inFlight_.erase(it); break; }
            }
            return true;
        case kClose:
            status("the peer left the chat");
            return false;
        default:
            return true;
        }
    }
};

}

bool runChat(UdpSocket& s, Session& ses, ChatIO& io, std::string& err){
    Chat chat(s, ses, io);
    return chat.run(err);
}

}

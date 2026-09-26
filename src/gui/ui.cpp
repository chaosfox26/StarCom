#include "ui.h"
#include "../textsafe.h"

namespace sc {

namespace {

const Color kRail = rgb(0x0B0B12);
const Color kColumn = rgb(0x12121A);
const Color kChat = rgb(0x181822);
const Color kPanel = rgb(0x0F0F16);
const Color kHover = rgb(0x24243A);
const Color kSelected = rgb(0x2E2E4A);
const Color kText = rgb(0xF4F4FA);
const Color kMuted = rgb(0xA3A6BD);
const Color kMagenta = rgb(0xE0359B);
const Color kBlue = rgb(0x3D7BFF);
const Color kField = rgb(0x22222E);
const Color kLine = rgb(0x26263A);

const int A_HOME = 1, A_ADDOPEN = 2, A_COPY = 3, A_ADDOK = 4, A_ADDCANCEL = 5, A_FCODE = 6, A_FNICK = 7, A_FMSG = 8,
          A_SEND = 9, A_CALL = 10, A_HEALTH = 11, A_RECONNECT = 12, A_FRIEND = 1000;

Color avatarColor(const std::string& code){
    unsigned h = 2166136261u;
    for(char ch : code) h = (h ^ (unsigned char)ch) * 16777619u;
    static const uint32_t pal[] = { 0xE0359B, 0x3D7BFF, 0xB03CE0, 0x2FA9D9, 0xD94C7C, 0x5B5BF0 };
    return rgb(pal[h % 6]);
}

std::string initial(const std::string& s){
    if(s.empty()) return "?";
    unsigned char c = (unsigned char)s[0];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    std::string o = s.substr(0, n);
    if(n == 1 && o[0] >= 'a' && o[0] <= 'z') o[0] = (char)(o[0] - 32);
    return o;
}

void popUtf8(std::string& s){
    if(s.empty()) return;
    size_t i = s.size() - 1;
    while(i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    s.erase(i);
}

void appendUtf8(std::string& s, uint32_t cp){
    if(cp < 0x80) s += (char)cp;
    else if(cp < 0x800){ s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 63)); }
    else if(cp < 0x10000){ s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 63)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
}

}

StarUI::StarUI(Host& host, NetHub& hub) : host_(host), hub_(hub) {}

void StarUI::hot(const Rect& r, int action){ hots_.push_back(Hot{ r, action }); }

int StarUI::hit(float x, float y) const {
    for(size_t i = hots_.size(); i-- > 0;) if(hots_[i].r.contains(x, y)) return hots_[i].action;
    return 0;
}

void StarUI::button(Canvas& c, const Rect& r, const std::string& label, Color bg, Color fg, int action){
    Color b = bg;
    if(hovered(action)){ b.r = b.r * 0.85f + 0.15f; b.g = b.g * 0.85f + 0.15f; b.b = b.b * 0.85f + 0.15f; }
    c.round(r, 4, b);
    c.text(label, r, Font::Bold, fg, Align::Center, false, true);
    hot(r, action);
}

void StarUI::drawField(Canvas& c, const Rect& r, const std::string& text, const std::string& placeholder, bool focused, int action){
    c.round(r, 6, kField);
    if(focused) c.outline(r, 6, 1.5f, kBlue);
    Rect t = rc(r.x + 12, r.y, r.w - 24, r.h);
    c.clip(r);
    if(text.empty()) c.text(placeholder, t, Font::Body, kMuted, Align::Left, false, true);
    else {
        float tw = c.textWidth(text, Font::Body);
        Rect tt = t;
        if(tw > t.w - 8){ tt.x -= (tw - (t.w - 8)); tt.w = tw + 8; }
        c.text(text, tt, Font::Body, kText, Align::Left, false, true);
        if(focused){ float cx = tt.x + tw + 1; c.line(cx, r.y + 10, cx, r.y + r.h - 10, 1.5f, kText); }
    }
    if(text.empty() && focused) c.line(t.x + 1, r.y + 10, t.x + 1, r.y + r.h - 10, 1.5f, kText);
    c.unclip();
    hot(r, action);
}

void StarUI::paint(Canvas& c, float w, float h){
    hots_.clear();
    c.fill(rc(0, 0, w, h), kChat);
    drawColumn(c, h);
    float right = showHealth_ && selected_ >= 0 ? 240.0f : 0.0f;
    chatX_ = 240;
    chatW_ = w - 240 - right;
    if(chatW_ < 200) chatW_ = 200;
    if(selected_ < 0) drawWelcome(c, chatX_, w - 240, h);
    else {
        drawChat(c, chatX_, chatW_, h);
        if(right > 0) drawHealth(c, w - right, h);
    }
    if(showAdd_) drawAdd(c, w, h);
    hoverAction_ = hit(mx_, my_);
}



void StarUI::drawColumn(Canvas& c, float h){
    c.fill(rc(0, 0, 240, h), kColumn);
    c.fill(rc(0, 47, 240, 1), rgb(0x0A0A10));
    Rect find = rc(10, 10, 220, 28);
    c.round(find, 4, kRail);
    c.text("Find or add a friend", rc(18, 10, 200, 28), Font::Small, kMuted, Align::Left, false, true);
    hot(find, A_ADDOPEN);
    Rect homeRow = rc(8, 56, 224, 42);
    if(selected_ < 0) c.round(homeRow, 4, kSelected);
    else if(hovered(A_HOME)) c.round(homeRow, 4, kHover);
    c.round(rc(18, 65, 24, 24), 7, kMagenta);
    c.text("SC", rc(18, 65, 24, 24), Font::Small, kText, Align::Center, false, true);
    c.text("Home", rc(52, 56, 160, 42), Font::Bold, selected_ < 0 || hovered(A_HOME) ? kText : kMuted, Align::Left, false, true);
    hot(homeRow, A_HOME);
    c.text("DIRECT MESSAGES", rc(16, 108, 170, 20), Font::Small, kMuted, Align::Left, false, true);
    Rect plus = rc(208, 108, 20, 20);
    c.text("+", plus, Font::Bold, hovered(A_ADDOPEN) ? kText : kMuted, Align::Center, false, true);
    hot(plus, A_ADDOPEN);
    std::vector<Friend>& fr = hub_.friends();
    float y = 134;
    c.clip(rc(0, 130, 240, h - 130 - 52));
    for(size_t i = 0; i < fr.size(); i++){
        int act = A_FRIEND + (int)i;
        Rect row = rc(8, y, 224, 42);
        if((int)i == selected_) c.round(row, 4, kSelected);
        else if(hovered(act)) c.round(row, 4, kHover);
        c.circle(28, y + 21, 16, avatarColor(fr[i].code));
        c.text(initial(fr[i].nick), rc(12, y + 5, 32, 32), Font::Bold, kText, Align::Center, false, true);
        bool on = fr[i].state == LinkState::Direct || fr[i].state == LinkState::Relay;
        c.circle(39, y + 32, 6, kColumn);
        c.circle(39, y + 32, 4, on ? kBlue : fr[i].state == LinkState::Connecting ? kMagenta : rgb(0x5A5D70));
        c.text(fr[i].nick, rc(52, y, 140, 42), Font::Body, (int)i == selected_ || hovered(act) ? kText : kMuted, Align::Left, false, true);
        if(fr[i].unread > 0 && (int)i != selected_){
            Rect badge = rc(200, y + 13, 24, 16);
            c.round(badge, 8, kMagenta);
            c.text(std::to_string(fr[i].unread > 99 ? 99 : fr[i].unread), badge, Font::Small, kText, Align::Center, false, true);
        }
        hot(row, act);
        y += 44;
    }
    if(fr.empty()) c.text("No friends yet. Click + to add one with their friend code.", rc(16, 138, 208, 60), Font::Small, kMuted, Align::Left, true, false);
    c.unclip();
    c.fill(rc(0, h - 52, 240, 52), kPanel);
    c.circle(24, h - 26, 16, kMagenta);
    c.text("Y", rc(8, h - 42, 32, 32), Font::Bold, kText, Align::Center, false, true);
    c.text("You", rc(48, h - 46, 120, 20), Font::Bold, kText, Align::Left, false, true);
    c.text(flash_.empty() ? hub_.myCode().substr(0, 15) + "..." : flash_, rc(48, h - 26, 140, 18), Font::Small, flash_.empty() ? kMuted : kBlue, Align::Left, false, true);
    Rect copy = rc(190, h - 42, 42, 32);
    if(hovered(A_COPY)) c.round(copy, 4, kHover);
    c.text("Copy", copy, Font::Small, kText, Align::Center, false, true);
    hot(copy, A_COPY);
}

void StarUI::drawWelcome(Canvas& c, float x, float w, float h){
    float cw = w - 96 > 600 ? 600 : w - 96;
    float cx = x + (w - cw) / 2;
    float y = h / 2 - 250;
    if(y < 16) y = 16;
    c.text("Welcome to StarCom", rc(cx, y, cw, 40), Font::Big, kText, Align::Center, false, true);
    c.text("No accounts, no email, no tracking. Your identity is a key that lives only on this computer.", rc(cx, y + 46, cw, 44), Font::Body, kMuted, Align::Center, true, false);
    c.text("YOUR FRIEND CODE", rc(cx, y + 104, cw, 20), Font::Small, kMuted, Align::Left, false, true);
    Rect box = rc(cx, y + 128, cw, 64);
    c.round(box, 6, kRail);
    c.text(hub_.myCode(), rc(cx + 14, y + 128, cw - 110, 64), Font::Bold, kText, Align::Left, true, true);
    button(c, rc(cx + cw - 86, y + 142, 74, 36), flash_.empty() ? "Copy" : "Copied", kMagenta, kText, A_COPY);
    const char* steps[] = {
        "1.  Copy your friend code and send it to a friend.",
        "2.  Click + next to Direct Messages and paste their code.",
        "3.  Click their name. When they have StarCom open too, you connect and can chat.",
        "4.  The right-hand panel shows how you are connected and who can see your IP address."
    };
    float sy = y + 212;
    for(const char* st : steps){ c.text(st, rc(cx, sy, cw, 24), Font::Body, kText, Align::Left, false, true); sy += 28; }
    if(!hub_.serverNote().empty()){ c.text(hub_.serverNote(), rc(cx, sy + 6, cw, 44), Font::Small, kMagenta, Align::Left, true, false); sy += 50; }
    Rect ai = rc(cx, sy + 16, cw, 92);
    c.round(ai, 6, kPanel);
    c.text("Built with AI, openly. StarCom's code is written with AI coding assistants (two instances of Claude by Anthropic) under the direction of Chaos, who designs the project and makes every decision. StarCom itself contains no AI: no AI features, no AI models, and nothing is ever sent to an AI service.",
        rc(cx + 14, sy + 24, cw - 28, 80), Font::Small, kMuted, Align::Left, true, false);
}

void StarUI::drawChat(Canvas& c, float x, float w, float h){
    Friend& f = hub_.friends()[(size_t)selected_];
    c.fill(rc(x, 0, w, 48), kChat);
    c.fill(rc(x, 47, w, 1), rgb(0x0A0A10));
    c.text("@", rc(x + 16, 0, 20, 48), Font::Title, kMuted, Align::Left, false, true);
    float nw = c.textWidth(f.nick, Font::Bold);
    c.text(f.nick, rc(x + 38, 0, nw + 4, 48), Font::Bold, kText, Align::Left, false, true);
    Color sc = f.state == LinkState::Direct || f.state == LinkState::Relay ? kBlue : f.state == LinkState::Connecting ? kMagenta : kMuted;
    float statusW = w - (nw + 60) - 150;
    if(statusW > 40) c.text(f.status, rc(x + nw + 56, 0, statusW, 48), Font::Small, sc, Align::Left, false, true);
    Rect call = rc(x + w - 132, 8, 32, 32);
    if(hovered(A_CALL)) c.round(call, 4, kHover);
    c.text("\xF0\x9F\x93\x9E", call, Font::Icon, kText, Align::Center, false, true);
    hot(call, A_CALL);
    Rect health = rc(x + w - 92, 8, 32, 32);
    if(hovered(A_HEALTH) || showHealth_) c.round(health, 4, showHealth_ ? kSelected : kHover);
    c.text("\xF0\x9F\x93\xB6", health, Font::Icon, kText, Align::Center, false, true);
    hot(health, A_HEALTH);
    if(f.state == LinkState::Ended || f.state == LinkState::Idle){
        Rect rec = rc(x + w - 52, 10, 44, 28);
        button(c, rec, "\xE2\x86\xBB", kBlue, kText, A_RECONNECT);
    }
    float top = 48, bottom = h - 72;
    Rect area = rc(x, top, w, bottom - top);
    c.clip(area);
    float textW = w - 88;
    std::vector<float> heights(f.lines.size());
    float total = 0;
    for(size_t i = 0; i < f.lines.size(); i++){
        bool head = i == 0 || f.lines[i].mine != f.lines[i - 1].mine || f.lines[i].system != f.lines[i - 1].system || f.lines[i - 1].system;
        ChatLine& cl = f.lines[i];
        if(cl.cachedWidth != textW){ cl.cachedHeight = c.textHeight(cl.text, textW, cl.system ? Font::Small : Font::Body); cl.cachedWidth = textW; }
        float th = cl.cachedHeight;
        heights[i] = th + (head ? 30 : 4);
        total += heights[i];
    }
    float maxScroll = total > area.h - 16 ? total - (area.h - 16) : 0;
    if(scroll_ > maxScroll) scroll_ = maxScroll;
    if(scroll_ < 0) scroll_ = 0;
    float y = bottom - 12 - total + scroll_;
    if(f.lines.empty()){
        c.text(f.nick, rc(x + 16, bottom - 150, w - 32, 36), Font::Big, kText, Align::Left, false, true);
        c.text("This is the beginning of your direct messages with " + f.nick + ". Messages are end-to-end encrypted and never stored anywhere; they disappear when you close StarCom.", rc(x + 16, bottom - 110, w - 32, 60), Font::Body, kMuted, Align::Left, true, false);
    }
    for(size_t i = 0; i < f.lines.size(); i++){
        const ChatLine& l = f.lines[i];
        bool head = heights[i] > 0 && (i == 0 || l.mine != f.lines[i - 1].mine || l.system != f.lines[i - 1].system || f.lines[i - 1].system);
        float lh = heights[i];
        if(y + lh >= top && y <= bottom){
            if(l.system){
                c.text(l.text, rc(x + 72, y + 26, textW, lh - 26), Font::Small, kMuted, Align::Left, true, false);
                c.text(l.time, rc(x + 72, y + 6, 80, 18), Font::Small, kMuted, Align::Left, false, true);
            } else {
                if(head){
                    c.circle(x + 36, y + 24, 18, l.mine ? kMagenta : avatarColor(f.code));
                    c.text(l.mine ? "Y" : initial(f.nick), rc(x + 18, y + 6, 36, 36), Font::Bold, kText, Align::Center, false, true);
                    std::string who = l.mine ? "You" : f.nick;
                    float ww = c.textWidth(who, Font::Bold);
                    c.text(who, rc(x + 72, y + 4, ww + 4, 22), Font::Bold, l.mine ? kMagenta : kBlue, Align::Left, false, true);
                    c.text(l.time, rc(x + 80 + ww, y + 4, 80, 22), Font::Small, kMuted, Align::Left, false, true);
                }
                c.text(l.text, rc(x + 72, y + (head ? 28 : 2), textW, lh - (head ? 28 : 2)), Font::Body, kText, Align::Left, true, false);
            }
        }
        y += lh;
    }
    c.unclip();
    bool live = f.state == LinkState::Direct || f.state == LinkState::Relay;
    drawField(c, rc(x + 16, h - 60, w - 32, 44), msg_, live ? "Message @" + f.nick : "Waiting for a connection to " + f.nick + "...", focus_ == FMessage, A_FMSG);
}

void StarUI::drawHealth(Canvas& c, float x, float h){
    c.fill(rc(x, 0, 240, h), kPanel);
    c.text("NETWORK HEALTH", rc(x + 16, 12, 200, 24), Font::Small, kMuted, Align::Left, false, true);
    Friend& f = hub_.friends()[(size_t)selected_];
    std::string conn = f.state == LinkState::Direct ? "Direct (peer to peer)" : f.state == LinkState::Relay ? "Through the relay" : f.state == LinkState::Connecting ? "Connecting..." : "Not connected";
    std::string ip = f.state == LinkState::Direct ? "Your friend can see your IP address." : f.state == LinkState::Relay ? "Your friend cannot see your IP. The relay sees both addresses, never content." : "Nobody: you are not connected.";
    float y = 44;
    c.text("Connection", rc(x + 16, y, 208, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text(conn, rc(x + 16, y + 18, 208, 22), Font::Bold, f.state == LinkState::Direct ? kBlue : f.state == LinkState::Relay ? kMagenta : kText, Align::Left, false, true);
    y += 52;
    c.text("Who sees your IP", rc(x + 16, y, 208, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text(ip, rc(x + 16, y + 18, 208, 60), Font::Small, kText, Align::Left, true, false);
    y += 84;
    c.text("Encryption", rc(x + 16, y, 208, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text("End to end (Noise IK, ChaCha20-Poly1305)", rc(x + 16, y + 18, 208, 40), Font::Small, kText, Align::Left, true, false);
    y += 64;
    c.text("Latency, jitter, loss, codec, bandwidth lanes", rc(x + 16, y, 208, 36), Font::Small, kMuted, Align::Left, true, false);
    c.text("Live numbers arrive with the engine update.", rc(x + 16, y + 36, 208, 40), Font::Small, kText, Align::Left, true, false);
}

void StarUI::drawAdd(Canvas& c, float w, float h){
    c.fill(rc(0, 0, w, h), rgb(0x000000, 0.7f));
    float bw = 460, bh = 330;
    Rect box = rc((w - bw) / 2, (h - bh) / 2, bw, bh);
    c.round(box, 8, kChat);
    hot(box, -1);
    float x = box.x + 20, y = box.y + 18, fw = bw - 40;
    c.text("Add a friend", rc(x, y, fw, 30), Font::Title, kText, Align::Left, false, true);
    c.text("Ask your friend for their friend code (it starts with SC1-) and paste it here.", rc(x, y + 34, fw, 40), Font::Small, kMuted, Align::Left, true, false);
    c.text("THEIR FRIEND CODE", rc(x, y + 80, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    drawField(c, rc(x, y + 100, fw, 40), addCode_, "SC1-XXXXX-...", focus_ == FAddCode, A_FCODE);
    c.text("NICKNAME (ONLY YOU SEE IT)", rc(x, y + 152, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    drawField(c, rc(x, y + 172, fw, 40), addNick_, "for example: River", focus_ == FAddNick, A_FNICK);
    if(!addErr_.empty()) c.text(addErr_, rc(x, y + 218, fw, 22), Font::Small, kMagenta, Align::Left, false, true);
    button(c, rc(box.x + bw - 220, box.y + bh - 56, 96, 38), "Cancel", rgb(0x2A2A3A), kText, A_ADDCANCEL);
    button(c, rc(box.x + bw - 116, box.y + bh - 56, 96, 38), "Add", kMagenta, kText, A_ADDOK);
}

void StarUI::mouseMove(float x, float y){
    mx_ = x;
    my_ = y;
    int h = hit(x, y);
    if(h != hoverAction_){ hoverAction_ = h; host_.redraw(); }
}

void StarUI::mouseDown(float x, float y){
    int a = hit(x, y);
    if(showAdd_ && a != A_FCODE && a != A_FNICK && a != A_ADDOK && a != A_ADDCANCEL){ if(a != -1) { showAdd_ = false; focus_ = FNone; } host_.redraw(); return; }
    act(a);
    host_.redraw();
}

void StarUI::wheel(float x, float y, float delta){
    if(selected_ >= 0 && x >= chatX_ && x < chatX_ + chatW_ && y > 48){ scroll_ += delta; host_.redraw(); }
}

std::string* StarUI::focused(){
    switch(focus_){
    case FMessage: return &msg_;
    case FAddCode: return &addCode_;
    case FAddNick: return &addNick_;
    default: return nullptr;
    }
}

void StarUI::charInput(uint32_t cp){
    std::string* s = focused();
    if(!s || cp < 0x20 || cp == 0x7F) return;
    size_t cap = focus_ == FMessage ? 1100 : focus_ == FAddCode ? 80 : 32;
    if(s->size() >= cap) return;
    appendUtf8(*s, cp);
    host_.redraw();
}

void StarUI::key(Key k){
    std::string* s = focused();
    switch(k){
    case Key::Backspace: if(s) popUtf8(*s); break;
    case Key::Enter:
        if(focus_ == FMessage) sendMessage();
        else if(focus_ == FAddCode || focus_ == FAddNick) submitAdd();
        break;
    case Key::Escape: if(showAdd_){ showAdd_ = false; focus_ = FNone; } break;
    case Key::Tab: if(showAdd_) focus_ = focus_ == FAddCode ? FAddNick : FAddCode; break;
    case Key::Paste:
        if(s){
            std::string p = host_.pasteText();
            for(char& ch : p) if(ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
            p = safeText(p);
            size_t cap = focus_ == FMessage ? 1100 : focus_ == FAddCode ? 80 : 32;
            if(s->size() + p.size() > cap){
                p.resize(cap > s->size() ? cap - s->size() : 0);
                while(!p.empty() && ((unsigned char)p.back() & 0xC0) == 0x80) p.pop_back();
                if(!p.empty() && ((unsigned char)p.back() & 0x80)) p.pop_back();
            }
            *s += p;
        }
        break;
    }
    host_.redraw();
}

void StarUI::act(int a){
    flash_.clear();
    if(a == 0){ focus_ = FNone; return; }
    if(a >= A_FRIEND){
        selected_ = a - A_FRIEND;
        Friend& f = hub_.friends()[(size_t)selected_];
        f.unread = 0;
        scroll_ = 0;
        if(f.state == LinkState::Idle) hub_.open((size_t)selected_);
        focus_ = FMessage;
        return;
    }
    switch(a){
    case A_HOME: selected_ = -1; focus_ = FNone; break;
    case A_ADDOPEN: showAdd_ = true; addErr_.clear(); focus_ = FAddCode; break;
    case A_ADDCANCEL: showAdd_ = false; focus_ = FNone; break;
    case A_ADDOK: submitAdd(); break;
    case A_FCODE: focus_ = FAddCode; break;
    case A_FNICK: focus_ = FAddNick; break;
    case A_FMSG: focus_ = FMessage; break;
    case A_COPY: host_.copyText(hub_.myCode()); flash_ = "Copied your friend code"; break;
    case A_HEALTH: showHealth_ = !showHealth_; break;
    case A_RECONNECT: if(selected_ >= 0){ hub_.friends()[(size_t)selected_].state = LinkState::Idle; hub_.open((size_t)selected_); } break;
    case A_CALL:
        if(selected_ >= 0){
            ChatLine l;
            l.system = true;
            l.text = "Voice calls in the window arrive with the engine update. For now, both of you can run: starcom call <friend code>";
            hub_.friends()[(size_t)selected_].lines.push_back(l);
        }
        break;
    default: break;
    }
}

void StarUI::submitAdd(){
    std::string err;
    if(hub_.addFriend(addCode_, addNick_, err)){
        showAdd_ = false;
        addCode_.clear();
        addNick_.clear();
        addErr_.clear();
        selected_ = (int)hub_.friends().size() - 1;
        scroll_ = 0;
        hub_.open((size_t)selected_);
        focus_ = FMessage;
    } else addErr_ = err;
}

void StarUI::sendMessage(){
    if(selected_ < 0) return;
    Friend& f = hub_.friends()[(size_t)selected_];
    bool live = f.state == LinkState::Direct || f.state == LinkState::Relay;
    std::string t = msg_;
    while(!t.empty() && t.back() == ' ') t.pop_back();
    if(t.empty() || !live) return;
    hub_.send((size_t)selected_, t);
    msg_.clear();
    scroll_ = 0;
}

}

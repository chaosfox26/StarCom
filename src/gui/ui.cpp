#include "ui.h"
#include "../textsafe.h"
#include <cstdio>

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
const Color kGrey = rgb(0x5A5D70);
const Color kEdge = rgb(0x0A0A10);

const float kRailW = 72, kColW = 240, kHeadH = 48;

const uint32_t gAdd = 0xE710, gMore = 0xE712, gSettings = 0xE713, gPhone = 0xE717, gMic = 0xE720, gSend = 0xE724,
               gStar = 0xE735, gEmoji = 0xE76E, gHangUp = 0xE778, gHeadphone = 0xE7F6, gMessage = 0xE8BD, gBell = 0xEA8F,
               gHealth = 0xE701, gSpeaker = 0xE767, gCheck = 0xE73E, gCancel = 0xE711;

enum Action {
    A_HOME = 1, A_ADDOPEN, A_COPY, A_ADDOK, A_ADDCANCEL, A_FCODE, A_FNICK, A_FMSG, A_SEND, A_CALL, A_HEALTH, A_RECONNECT,
    A_FMYNAME, A_SAVENAME, A_SETTINGS, A_HUBS, A_MODALCLOSE, A_EMOJI, A_ATTACH, A_MUTE, A_DEAFEN, A_BELL, A_HANGUP,
    A_ACCEPT, A_DECLINE, A_CALLOK, A_MENU, A_COPYCODE, A_MENUADD, A_MENUCOPY, A_MODALBOX = 900, A_EMOJIPICK = 2000,
    A_REQACCEPT = 3000, A_REQIGNORE = 3100, A_FRIEND = 1000
};

const char* kEmoji[] = {
    "\xF0\x9F\x98\x80", "\xF0\x9F\x98\x82", "\xF0\x9F\x98\x8A", "\xF0\x9F\x98\x8D", "\xF0\x9F\x98\x8E", "\xF0\x9F\x98\x98", "\xF0\x9F\xA5\xB0", "\xF0\x9F\x98\x89",
    "\xF0\x9F\x98\xAE", "\xF0\x9F\x98\xA2", "\xF0\x9F\x98\xAD", "\xF0\x9F\x98\xA1", "\xF0\x9F\x98\xB4", "\xF0\x9F\xA4\x94", "\xF0\x9F\x99\x84", "\xF0\x9F\x98\x87",
    "\xF0\x9F\x91\x8D", "\xF0\x9F\x91\x8E", "\xF0\x9F\x91\x8B", "\xF0\x9F\x99\x8F", "\xF0\x9F\x91\x8F", "\xF0\x9F\x92\xAA", "\xF0\x9F\xA4\x9D", "\xE2\x9C\x8C\xEF\xB8\x8F",
    "\xE2\x9D\xA4\xEF\xB8\x8F", "\xF0\x9F\x92\x9C", "\xF0\x9F\x92\x99", "\xF0\x9F\x94\xA5", "\xE2\x9C\xA8", "\xE2\xAD\x90", "\xF0\x9F\x8E\x89", "\xF0\x9F\x8E\xAE",
    "\xF0\x9F\x9A\x80", "\xF0\x9F\x8C\x99", "\xF0\x9F\xA6\x8A", "\xF0\x9F\x90\xBA", "\xF0\x9F\x90\xB1", "\xF0\x9F\x8D\x95", "\xE2\x98\x95", "\xF0\x9F\x8E\xB5"
};
const int kEmojiCount = (int)(sizeof kEmoji / sizeof kEmoji[0]);

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

std::string glyph(uint32_t cp){
    std::string s;
    appendUtf8(s, cp);
    return s;
}

std::string clock2(int secs){
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", secs / 60, secs % 60);
    return b;
}

Color lighten(Color b){
    b.r = b.r * 0.85f + 0.15f;
    b.g = b.g * 0.85f + 0.15f;
    b.b = b.b * 0.85f + 0.15f;
    return b;
}

}

StarUI::StarUI(Host& host, NetHub& hub) : host_(host), hub_(hub) {}

void StarUI::hot(const Rect& r, int action, const std::string& tip){ hots_.push_back(Hot{ r, action, tip }); }

int StarUI::hit(float x, float y, std::string* tip) const {
    for(size_t i = hots_.size(); i-- > 0;){
        if(!hots_[i].r.contains(x, y)) continue;
        if(tip) *tip = hots_[i].tip;
        return hots_[i].action;
    }
    if(tip) tip->clear();
    return 0;
}

void StarUI::button(Canvas& c, const Rect& r, const std::string& label, Color bg, Color fg, int action, const std::string& tip){
    c.round(r, 4, hovered(action) ? lighten(bg) : bg);
    c.text(label, r, Font::Bold, fg, Align::Center, false, true);
    hot(r, action, tip);
}

void StarUI::iconButton(Canvas& c, const Rect& r, uint32_t g, Color fg, int action, const std::string& tip, bool active, bool slashed){
    if(active) c.round(r, 6, kSelected);
    else if(hovered(action)) c.round(r, 6, kHover);
    c.text(glyph(g), r, Font::Glyph, hovered(action) ? kText : fg, Align::Center, false, true);
    if(slashed) c.line(r.x + 8, r.y + r.h - 8, r.x + r.w - 8, r.y + 8, 2.0f, kMagenta);
    hot(r, action, tip);
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
    if(callDismissed_.size() < hub_.friends().size()) callDismissed_.resize(hub_.friends().size(), false);
    c.fill(rc(0, 0, w, h), kChat);
    drawRail(c, h);
    drawColumn(c, kRailW, h);
    float left = kRailW + kColW;
    float right = showHealth_ && selected_ >= 0 ? 240.0f : 0.0f;
    chatX_ = left;
    chatW_ = w - left - right;
    if(chatW_ < 260) chatW_ = 260;
    if(selected_ < 0) drawHome(c, left, w - left, h);
    else {
        drawChat(c, chatX_, chatW_, h);
        if(right > 0) drawHealth(c, w - right, h);
    }
    if(modal_ == MAdd) drawAdd(c, w, h);
    else if(modal_ == MSettings) drawSettings(c, w, h);
    else if(modal_ == MHubs) drawHubs(c, w, h);
    else if(modal_ == MEmoji) drawEmoji(c, w, h);
    else if(modal_ == MMenu) drawMenu(c);
    hoverAction_ = hit(mx_, my_, &hoverTip_);
    drawTip(c, w, h);
}

void StarUI::drawRail(Canvas& c, float h){
    c.fill(rc(0, 0, kRailW, h), kRail);
    c.circle(36, 36, 24, kMagenta);
    c.text(glyph(gStar), rc(12, 12, 48, 48), Font::GlyphLarge, kText, Align::Center, false, true);
    c.fill(rc(22, 70, 28, 2), kHover);
    Rect home = rc(12, 84, 48, 48);
    bool homeSel = true;
    if(homeSel) c.fill(rc(0, 94, 4, 28), kText);
    c.round(home, 16, homeSel ? kBlue : hovered(A_HOME) ? kSelected : kHover);
    c.text(glyph(gMessage), home, Font::GlyphLarge, kText, Align::Center, false, true);
    hot(home, A_HOME, "Direct messages");
    Rect add = rc(12, 148, 48, 48);
    c.circle(36, 172, 24, hovered(A_HUBS) ? kSelected : kHover);
    c.text(glyph(gAdd), add, Font::GlyphLarge, kBlue, Align::Center, false, true);
    hot(add, A_HUBS, "Add a server");
    c.text("Add Server", rc(0, 198, kRailW, 16), Font::Tiny, kMuted, Align::Center, false, true);
}

void StarUI::drawColumn(Canvas& c, float x, float h){
    c.fill(rc(x, 0, kColW, h), kColumn);
    c.fill(rc(x, kHeadH - 1, kColW, 1), kEdge);
    c.text("Direct Messages", rc(x + 16, 0, kColW - 60, kHeadH), Font::Bold, kText, Align::Left, false, true);
    iconButton(c, rc(x + kColW - 40, 8, 32, 32), gMore, kMuted, A_MENU, "More");
    float top = 60;
    std::vector<RequestView> reqs = hub_.requests();
    if(!reqs.empty() || !hub_.requestNote().empty()){
        if(!reqs.empty()) c.text("MESSAGE REQUESTS", rc(x + 16, top, 200, 20), Font::Small, kMagenta, Align::Left, false, true);
        float ry = top + 24;
        for(size_t i = 0; i < reqs.size() && i < 5; i++){
            Rect row = rc(x + 8, ry, kColW - 16, 48);
            c.round(row, 4, kHover);
            std::string who = reqs[i].hint.empty() ? std::string("Someone") : "@" + reqs[i].hint + "?";
            c.circle(x + 30, ry + 24, 16, avatarColor(reqs[i].code));
            c.text(initial(reqs[i].hint.empty() ? std::string("?") : reqs[i].hint), rc(x + 14, ry + 8, 32, 32), Font::Bold, kText, Align::Center, false, true);
            c.text(who, rc(x + 54, ry + 4, 110, 20), Font::Bold, kText, Align::Left, false, true);
            c.text(reqs[i].checking ? std::string("checking...") : std::string("not verified yet"), rc(x + 54, ry + 24, 110, 18), Font::Small, kMuted, Align::Left, false, true);
            hot(row, A_MODALBOX, "Friend code: " + reqs[i].code);
            if(!reqs[i].checking){
                iconButton(c, rc(x + kColW - 84, ry + 8, 32, 32), gCheck, kBlue, A_REQACCEPT + (int)i, reqs[i].hint.empty() ? std::string("Accept and add this friend code") : "Accept: check that @" + reqs[i].hint + "'s public record matches, then add and answer");
                iconButton(c, rc(x + kColW - 48, ry + 8, 32, 32), gCancel, kMagenta, A_REQIGNORE + (int)i, "Ignore");
            }
            ry += 52;
        }
        if(!hub_.requestNote().empty()){
            float nh = c.textHeight(hub_.requestNote(), kColW - 32, Font::Small);
            c.text(hub_.requestNote(), rc(x + 16, ry, kColW - 32, nh + 4), Font::Small, kMuted, Align::Left, true, false);
            ry += nh + 8;
        }
        top = ry + 6;
    }
    c.text("DIRECT MESSAGES", rc(x + 16, top, 170, 20), Font::Small, kMuted, Align::Left, false, true);
    Rect plus = rc(x + kColW - 34, top - 2, 24, 24);
    c.text(glyph(gAdd), plus, Font::Glyph, hovered(A_ADDOPEN) ? kText : kMuted, Align::Center, false, true);
    hot(plus, A_ADDOPEN, "Add a friend by their @name");
    std::vector<Friend>& fr = hub_.friends();
    float y = top + 26;
    c.clip(rc(x, top + 24, kColW, h - (top + 24) - 58));
    for(size_t i = 0; i < fr.size(); i++){
        int act = A_FRIEND + (int)i;
        Rect row = rc(x + 8, y, kColW - 16, 44);
        if((int)i == selected_) c.round(row, 4, kSelected);
        else if(hovered(act)) c.round(row, 4, kHover);
        c.circle(x + 30, y + 22, 16, avatarColor(fr[i].code));
        c.text(initial(fr[i].nick), rc(x + 14, y + 6, 32, 32), Font::Bold, kText, Align::Center, false, true);
        bool on = fr[i].state == LinkState::Connected;
        c.circle(x + 42, y + 34, 6, kColumn);
        c.circle(x + 42, y + 34, 4, on ? kBlue : fr[i].state == LinkState::Connecting ? kMagenta : kGrey);
        c.text(fr[i].nick, rc(x + 56, y, 130, 44), Font::Body, (int)i == selected_ || hovered(act) ? kText : kMuted, Align::Left, false, true);
        CallView cv = hub_.callView(i);
        if(cv.state == CallState::Ringing || cv.state == CallState::InCall){
            c.text(glyph(gPhone), rc(x + kColW - 60, y, 24, 44), Font::Glyph, cv.state == CallState::Ringing ? kMagenta : kBlue, Align::Center, false, true);
        }
        if(fr[i].unread > 0 && (int)i != selected_){
            Rect badge = rc(x + kColW - 38, y + 14, 24, 16);
            c.round(badge, 8, kMagenta);
            c.text(std::to_string(fr[i].unread > 99 ? 99 : fr[i].unread), badge, Font::Small, kText, Align::Center, false, true);
        }
        hot(row, act, "@" + (fr[i].name.empty() ? fr[i].nick : fr[i].name));
        y += 46;
    }
    if(fr.empty()) c.text("No friends yet. Click + to add one by their @name.", rc(x + 16, top + 30, kColW - 32, 60), Font::Small, kMuted, Align::Left, true, false);
    c.unclip();
    float py = h - 56;
    c.fill(rc(x, py, kColW, 56), kPanel);
    c.circle(x + 28, py + 28, 18, kMagenta);
    c.text(initial(hub_.myName().empty() ? std::string("you") : hub_.myName()), rc(x + 10, py + 10, 36, 36), Font::Bold, kText, Align::Center, false, true);
    c.circle(x + 41, py + 41, 6, kPanel);
    c.circle(x + 41, py + 41, 4, hub_.networkReady() ? kBlue : kGrey);
    c.text(hub_.myName().empty() ? std::string("You") : "@" + hub_.myName(), rc(x + 54, py + 8, 140, 20), Font::Bold, kText, Align::Left, false, true);
    c.text(!flash_.empty() ? flash_ : hub_.networkReady() ? std::string("Online") : std::string("Connecting..."), rc(x + 54, py + 28, 140, 18), Font::Small, hub_.networkReady() || !flash_.empty() ? kBlue : kMuted, Align::Left, false, true);
    iconButton(c, rc(x + kColW - 42, py + 12, 32, 32), gSettings, kMuted, A_SETTINGS, "Settings");
}

void StarUI::drawHome(Canvas& c, float x, float w, float h){
    c.fill(rc(x, 0, w, kHeadH), kChat);
    c.fill(rc(x, kHeadH - 1, w, 1), kEdge);
    c.text("Home", rc(x + 16, 0, w - 32, kHeadH), Font::Bold, kText, Align::Left, false, true);
    float cw = w - 96 > 600 ? 600 : w - 96;
    float cx = x + (w - cw) / 2;
    float y = h / 2 - 250;
    if(y < kHeadH + 16) y = kHeadH + 16;
    c.text("Welcome to StarCom", rc(cx, y, cw, 40), Font::Big, kText, Align::Center, false, true);
    c.text("No accounts, no email, no tracking. Your identity is a key that lives only on this computer.", rc(cx, y + 46, cw, 44), Font::Body, kMuted, Align::Center, true, false);
    float sy = y + 104;
    if(hub_.myName().empty() || hub_.nameTaken()){
        c.text("CHOOSE YOUR @NAME (YOUR FRIENDS FIND YOU BY IT)", rc(cx, sy, cw, 20), Font::Small, kMuted, Align::Left, false, true);
        drawField(c, rc(cx, sy + 24, cw - 100, 44), myName_, "for example: river", focus_ == FMyName, A_FMYNAME);
        button(c, rc(cx + cw - 88, sy + 28, 88, 36), "Save", kMagenta, kText, A_SAVENAME, "Save your @name");
        if(!nameErr_.empty()) c.text(nameErr_, rc(cx, sy + 72, cw, 20), Font::Small, kMagenta, Align::Left, false, true);
        sy += 100;
    } else {
        c.text("YOUR @NAME", rc(cx, sy, cw, 20), Font::Small, kMuted, Align::Left, false, true);
        c.round(rc(cx, sy + 24, cw, 52), 6, kRail);
        c.text("@" + hub_.myName(), rc(cx + 14, sy + 24, cw - 110, 52), Font::Title, kText, Align::Left, false, true);
        button(c, rc(cx + cw - 86, sy + 32, 74, 36), "Copy", kMagenta, kText, A_COPY, "Copy your @name to send it to a friend");
        sy += 88;
    }
    c.text(hub_.networkNote(), rc(cx, sy, cw, 22), Font::Small, hub_.networkReady() ? kBlue : kMagenta, Align::Left, false, true);
    sy += 32;
    const char* steps[] = {
        "1.  Tell your friends your @name.",
        "2.  Click + next to Direct Messages and type their @name.",
        "3.  Click their name and start typing. They get your messages whenever StarCom is open.",
        "4.  Nobody in a conversation can see anybody's IP address: everything goes through relays, encrypted."
    };
    for(const char* st : steps){ float th = c.textHeight(st, cw, Font::Body); c.text(st, rc(cx, sy, cw, th + 4), Font::Body, kText, Align::Left, true, false); sy += th + 8; }
    Rect ai = rc(cx, sy + 16, cw, 92);
    c.round(ai, 6, kPanel);
    c.text("Built with AI, openly. StarCom's code is written with AI coding assistants (two instances of Claude by Anthropic) under the direction of Chaos, who designs the project and makes every decision. StarCom itself contains no AI: no AI features, no AI models, and nothing is ever sent to an AI service.",
        rc(cx + 14, sy + 24, cw - 28, 80), Font::Small, kMuted, Align::Left, true, false);
}

void StarUI::drawChat(Canvas& c, float x, float w, float h){
    Friend& f = hub_.friends()[(size_t)selected_];
    CallView cv = hub_.callView((size_t)selected_);
    bool live = f.state == LinkState::Connected;
    c.fill(rc(x, 0, w, kHeadH), kChat);
    c.fill(rc(x, kHeadH - 1, w, 1), kEdge);
    c.text("@", rc(x + 16, 0, 20, kHeadH), Font::Title, kMuted, Align::Left, false, true);
    float nw = c.textWidth(f.nick, Font::Bold);
    c.text(f.nick, rc(x + 38, 0, nw + 4, kHeadH), Font::Bold, kText, Align::Left, false, true);
    c.fill(rc(x + 48 + nw, 16, 1, 16), kHover);
    Color sc = live ? kBlue : f.state == LinkState::Connecting ? kMagenta : kMuted;
    float icons = 5 * 40 + 8;
    float statusW = w - (nw + 60) - icons;
    if(statusW > 40) c.text(f.status, rc(x + nw + 58, 0, statusW, kHeadH), Font::Small, sc, Align::Left, false, true);
    float ix = x + w - icons;
    bool inCall = cv.state == CallState::InCall || cv.state == CallState::Calling;
    iconButton(c, rc(ix, 8, 32, 32), gPhone, live ? kText : kGrey, A_CALL, inCall ? (cv.state == CallState::InCall ? "You are in a call" : "Calling...") : live ? "Start a voice call" : "Voice calls work once you are connected", inCall);
    iconButton(c, rc(ix + 40, 8, 32, 32), gMic, kMuted, A_MUTE, cv.muted ? "Unmute your microphone" : "Mute your microphone", false, cv.muted);
    iconButton(c, rc(ix + 80, 8, 32, 32), gHeadphone, kMuted, A_DEAFEN, cv.deafened ? "Hear your friend again" : "Deafen (hear nothing)", false, cv.deafened);
    iconButton(c, rc(ix + 120, 8, 32, 32), gBell, kMuted, A_BELL, "Notifications (coming soon)");
    iconButton(c, rc(ix + 160, 8, 32, 32), gHealth, kMuted, A_HEALTH, "Network health", showHealth_);
    float top = kHeadH, bottom = h - 76;
    Rect area = rc(x, top, w, bottom - top);
    c.clip(area);
    float textW = w - 88;
    std::vector<float> heights(f.lines.size());
    float total = 0;
    for(size_t i = 0; i < f.lines.size(); i++){
        bool head = i == 0 || f.lines[i].mine != f.lines[i - 1].mine || f.lines[i].system != f.lines[i - 1].system || f.lines[i - 1].system;
        ChatLine& cl = f.lines[i];
        if(cl.cachedWidth != textW){ cl.cachedHeight = c.textHeight(cl.text, textW, cl.system ? Font::Small : Font::Body); cl.cachedWidth = textW; }
        heights[i] = cl.cachedHeight + (head ? 30 : 4);
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
        bool head = i == 0 || l.mine != f.lines[i - 1].mine || l.system != f.lines[i - 1].system || f.lines[i - 1].system;
        float lh = heights[i];
        if(y + lh >= top && y <= bottom){
            if(l.system){
                c.text(l.text, rc(x + 72, y + 26, textW, lh - 26), Font::Small, kMuted, Align::Left, true, false);
                c.text(l.time, rc(x + 72, y + 6, 80, 18), Font::Small, kMuted, Align::Left, false, true);
            } else {
                if(head){
                    c.circle(x + 36, y + 24, 18, l.mine ? kMagenta : avatarColor(f.code));
                    std::string me = hub_.myName().empty() ? std::string("you") : hub_.myName();
                    c.text(l.mine ? initial(me) : initial(f.nick), rc(x + 18, y + 6, 36, 36), Font::Bold, kText, Align::Center, false, true);
                    std::string who = l.mine ? (hub_.myName().empty() ? std::string("You") : hub_.myName()) : f.nick;
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
    Rect bar = rc(x + 16, h - 64, w - 32, 48);
    c.round(bar, 8, kField);
    iconButton(c, rc(bar.x + 8, bar.y + 8, 32, 32), gAdd, kMuted, A_ATTACH, "Send a file (coming soon)");
    Rect fieldR = rc(bar.x + 44, bar.y, bar.w - 44 - 88, bar.h);
    c.clip(fieldR);
    std::string ph = live ? "Message @" + f.nick + "..." : f.state == LinkState::Connecting ? "Connecting to @" + f.nick + "..." : "Click Reconnect or wait for " + f.nick;
    if(msg_.empty()) c.text(ph, rc(fieldR.x + 4, fieldR.y, fieldR.w - 8, fieldR.h), Font::Body, kMuted, Align::Left, false, true);
    else {
        float tw = c.textWidth(msg_, Font::Body);
        Rect tt = rc(fieldR.x + 4, fieldR.y, fieldR.w - 8, fieldR.h);
        if(tw > tt.w - 8){ tt.x -= (tw - (tt.w - 8)); tt.w = tw + 8; }
        c.text(msg_, tt, Font::Body, kText, Align::Left, false, true);
        if(focus_ == FMessage){ float cxp = tt.x + tw + 1; c.line(cxp, bar.y + 12, cxp, bar.y + bar.h - 12, 1.5f, kText); }
    }
    if(msg_.empty() && focus_ == FMessage) c.line(fieldR.x + 5, bar.y + 12, fieldR.x + 5, bar.y + bar.h - 12, 1.5f, kText);
    c.unclip();
    hot(fieldR, A_FMSG);
    iconButton(c, rc(bar.x + bar.w - 84, bar.y + 8, 32, 32), gEmoji, kMuted, A_EMOJI, "Emoji");
    iconButton(c, rc(bar.x + bar.w - 44, bar.y + 8, 32, 32), gSend, live && !msg_.empty() ? kBlue : kMuted, A_SEND, "Send");
    if(f.state == LinkState::Ended || f.state == LinkState::Idle) button(c, rc(x + w - 124, h - 118, 108, 34), "Reconnect", kBlue, kText, A_RECONNECT, "Connect to your friend again");
    if(!flash_.empty() && selected_ >= 0) c.text(flash_, rc(x + 20, h - 88, w - 40, 20), Font::Small, kMagenta, Align::Left, false, true);
    drawCallPanel(c, x, w, h - 76);
}

void StarUI::drawCallPanel(Canvas& c, float x, float w, float bottom){
    size_t idx = (size_t)selected_;
    CallView cv = hub_.callView(idx);
    if(cv.state == CallState::Idle) return;
    if(cv.state != CallState::Ended) callDismissed_[idx] = false;
    else if(callDismissed_[idx]) return;
    Friend& f = hub_.friends()[idx];
    float pw = 300, ph = 124;
    Rect box = rc(x + w - pw - 16, bottom - ph - 8, pw, ph);
    c.round(box, 8, kPanel);
    c.outline(box, 8, 1.0f, kHover);
    hot(box, A_MODALBOX);
    std::string title = "CALL";
    float tw = c.textWidth(title, Font::Bold);
    c.text(title, rc(box.x + 14, box.y + 8, tw + 4, 22), Font::Bold, kText, Align::Left, false, true);
    c.fill(rc(box.x + 22 + tw, box.y + 13, 1, 14), kMuted);
    std::string sub = cv.state == CallState::InCall ? "@" + f.nick + "  " + clock2(cv.seconds) : cv.state == CallState::Calling ? "Calling @" + f.nick + "..." : cv.state == CallState::Ringing ? "@" + f.nick + " is calling you" : cv.note;
    c.text(sub, rc(box.x + 30 + tw, box.y + 8, pw - tw - 70, 22), Font::Body, kMuted, Align::Left, false, true);
    c.text(glyph(gSpeaker), rc(box.x + pw - 34, box.y + 6, 24, 24), Font::Glyph, kMuted, Align::Center, false, true);
    if(cv.state == CallState::InCall || cv.state == CallState::Calling){
        c.text(cv.state == CallState::InCall ? "Use headphones: there is no echo cancellation yet." : "Waiting for your friend to answer...", rc(box.x + 14, box.y + 32, pw - 28, 18), Font::Tiny, kMuted, Align::Left, false, true);
        iconButton(c, rc(box.x + 14, box.y + 58, 40, 36), gMic, kText, A_MUTE, cv.muted ? "Unmute" : "Mute", false, cv.muted);
        c.text("Mute", rc(box.x + 6, box.y + 94, 56, 16), Font::Tiny, kMuted, Align::Center, false, true);
        iconButton(c, rc(box.x + 66, box.y + 58, 40, 36), gHeadphone, kText, A_DEAFEN, cv.deafened ? "Undeafen" : "Deafen", false, cv.deafened);
        c.text("Deafen", rc(box.x + 58, box.y + 94, 56, 16), Font::Tiny, kMuted, Align::Center, false, true);
        button(c, rc(box.x + 124, box.y + 58, pw - 138, 36), "DISCONNECT", kMagenta, kText, A_HANGUP, "Hang up");
        if(cv.state == CallState::InCall) c.text("latency: " + std::to_string(cv.ping) + " ms Ping", rc(box.x + 124, box.y + 96, pw - 138, 16), Font::Tiny, kMuted, Align::Center, false, true);
    } else if(cv.state == CallState::Ringing){
        c.text("Use headphones: there is no echo cancellation yet.", rc(box.x + 14, box.y + 32, pw - 28, 18), Font::Tiny, kMuted, Align::Left, false, true);
        button(c, rc(box.x + 14, box.y + 64, (pw - 42) / 2, 38), "Accept", kBlue, kText, A_ACCEPT, "Answer the call");
        button(c, rc(box.x + 28 + (pw - 42) / 2, box.y + 64, (pw - 42) / 2, 38), "Decline", kMagenta, kText, A_DECLINE, "Decline the call");
    } else {
        c.text(cv.note, rc(box.x + 14, box.y + 36, pw - 28, 40), Font::Small, kText, Align::Left, true, false);
        button(c, rc(box.x + pw - 110, box.y + 76, 96, 34), "OK", rgb(0x2A2A3A), kText, A_CALLOK);
    }
}

void StarUI::drawHealth(Canvas& c, float x, float h){
    c.fill(rc(x, 0, 240, h), kPanel);
    c.text("NETWORK HEALTH", rc(x + 16, 12, 200, 24), Font::Small, kMuted, Align::Left, false, true);
    Friend& f = hub_.friends()[(size_t)selected_];
    CallView cv = hub_.callView((size_t)selected_);
    std::string conn = f.state == LinkState::Connected ? "Through the relays" : f.state == LinkState::Connecting ? "Connecting..." : "Not connected";
    float y = 44;
    c.text("Connection", rc(x + 16, y, 208, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text(conn, rc(x + 16, y + 18, 208, 22), Font::Bold, f.state == LinkState::Connected ? kBlue : kText, Align::Left, false, true);
    y += 52;
    c.text("Who sees your IP", rc(x + 16, y, 208, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text("Nobody in the conversation. Your friend never receives your IP address; the relays only pass along encrypted packets they cannot read.", rc(x + 16, y + 18, 208, 76), Font::Small, kText, Align::Left, true, false);
    y += 100;
    c.text("Encryption", rc(x + 16, y, 208, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text("End to end (Noise IK, ChaCha20-Poly1305)", rc(x + 16, y + 18, 208, 40), Font::Small, kText, Align::Left, true, false);
    y += 64;
    c.text("Latency", rc(x + 16, y, 208, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text(cv.state == CallState::InCall ? std::to_string(cv.ping) + " ms" : std::string("shown during a call"), rc(x + 16, y + 18, 208, 20), Font::Small, kText, Align::Left, false, true);
}

void StarUI::drawAdd(Canvas& c, float w, float h){
    c.fill(rc(0, 0, w, h), rgb(0x000000, 0.7f));
    hot(rc(0, 0, w, h), A_MODALCLOSE);
    float bw = 460, bh = 360;
    Rect box = rc((w - bw) / 2, (h - bh) / 2, bw, bh);
    c.round(box, 8, kChat);
    hot(box, A_MODALBOX);
    float x = box.x + 20, y = box.y + 18, fw = bw - 40;
    c.text("Add a friend", rc(x, y, fw, 30), Font::Title, kText, Align::Left, false, true);
    c.text("Type your friend's @name.", rc(x, y + 34, fw, 22), Font::Small, kMuted, Align::Left, false, true);
    c.text("THEIR @NAME", rc(x, y + 64, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    drawField(c, rc(x, y + 84, fw, 40), addCode_, "for example: river", focus_ == FAddCode, A_FCODE);
    bool found = !hub_.foundCode().empty();
    std::string note = !addErr_.empty() ? addErr_ : hub_.finding() ? std::string("Looking on the StarCom network...") : !hub_.foundError().empty() ? hub_.foundError() : std::string();
    if(found){
        c.text("Found @" + hub_.foundName() + ". To be sure it is really them, you can compare this friend code with them: " + hub_.foundCode(), rc(x, y + 132, fw, 64), Font::Small, kBlue, Align::Left, true, false);
        c.text("NICKNAME (ONLY YOU SEE IT)", rc(x, y + 200, fw, 18), Font::Small, kMuted, Align::Left, false, true);
        drawField(c, rc(x, y + 220, fw, 40), addNick_, "optional", focus_ == FAddNick, A_FNICK);
    } else if(!note.empty()) c.text(note, rc(x, y + 132, fw, 44), Font::Small, hub_.finding() ? kMuted : kMagenta, Align::Left, true, false);
    button(c, rc(box.x + bw - 220, box.y + bh - 56, 96, 38), "Cancel", rgb(0x2A2A3A), kText, A_ADDCANCEL);
    button(c, rc(box.x + bw - 116, box.y + bh - 56, 96, 38), found ? "Add" : "Find", kMagenta, kText, A_ADDOK, found ? "Add to your friends" : "Look for this @name");
}

void StarUI::drawSettings(Canvas& c, float w, float h){
    c.fill(rc(0, 0, w, h), rgb(0x000000, 0.7f));
    hot(rc(0, 0, w, h), A_MODALCLOSE);
    float bw = 520, bh = 470;
    Rect box = rc((w - bw) / 2, (h - bh) / 2, bw, bh);
    c.round(box, 8, kChat);
    hot(box, A_MODALBOX);
    float x = box.x + 20, y = box.y + 18, fw = bw - 40;
    c.text("Settings", rc(x, y, fw, 30), Font::Title, kText, Align::Left, false, true);
    y += 44;
    c.text("YOUR @NAME", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    drawField(c, rc(x, y + 20, fw - 100, 40), myName_, hub_.myName().empty() ? std::string("choose one") : "@" + hub_.myName(), focus_ == FMyName, A_FMYNAME);
    button(c, rc(x + fw - 88, y + 22, 88, 36), "Save", kMagenta, kText, A_SAVENAME, "Change your @name");
    if(!nameErr_.empty()) c.text(nameErr_, rc(x, y + 62, fw, 18), Font::Small, kMagenta, Align::Left, false, true);
    y += 86;
    c.text("YOUR FRIEND CODE (FOR CHECKING IT IS REALLY YOU)", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text(hub_.myCode(), rc(x, y + 20, fw - 90, 44), Font::Small, kText, Align::Left, true, false);
    button(c, rc(x + fw - 76, y + 22, 76, 32), "Copy", rgb(0x2A2A3A), kText, A_COPYCODE, "Copy your friend code");
    y += 72;
    c.text("DATA FOLDER (YOUR IDENTITY, @NAME AND FRIENDS LIST)", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text(hub_.dataFolder(), rc(x, y + 20, fw, 40), Font::Small, kText, Align::Left, true, false);
    y += 66;
    c.text("ABOUT", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text("StarCom: direct messages and voice calls with no accounts and no servers that read anything. Built with AI, openly: the code is written with AI coding assistants (two instances of Claude by Anthropic) under the direction of Chaos. StarCom itself contains no AI.", rc(x, y + 20, fw, 80), Font::Small, kText, Align::Left, true, false);
    button(c, rc(box.x + bw - 116, box.y + bh - 56, 96, 38), "Close", rgb(0x2A2A3A), kText, A_MODALCLOSE);
}

void StarUI::drawHubs(Canvas& c, float w, float h){
    c.fill(rc(0, 0, w, h), rgb(0x000000, 0.7f));
    hot(rc(0, 0, w, h), A_MODALCLOSE);
    float bw = 420, bh = 190;
    Rect box = rc((w - bw) / 2, (h - bh) / 2, bw, bh);
    c.round(box, 8, kChat);
    hot(box, A_MODALBOX);
    c.text("Servers", rc(box.x + 20, box.y + 18, bw - 40, 30), Font::Title, kText, Align::Left, false, true);
    c.text("Servers (group spaces with channels and voice rooms) are being built next. For now StarCom has direct messages and voice calls.", rc(box.x + 20, box.y + 56, bw - 40, 70), Font::Body, kMuted, Align::Left, true, false);
    button(c, rc(box.x + bw - 116, box.y + bh - 56, 96, 38), "OK", kMagenta, kText, A_MODALCLOSE);
}

void StarUI::drawEmoji(Canvas& c, float w, float h){
    hot(rc(0, 0, w, h), A_MODALCLOSE);
    float cell = 36, cols = 8;
    float bw = cols * cell + 16, bh = (float)((kEmojiCount + 7) / 8) * cell + 16;
    Rect box = rc(chatX_ + chatW_ - bw - 16, h - 72 - bh, bw, bh);
    c.round(box, 8, kPanel);
    c.outline(box, 8, 1.0f, kHover);
    hot(box, A_MODALBOX);
    for(int i = 0; i < kEmojiCount; i++){
        Rect r = rc(box.x + 8 + (float)(i % 8) * cell, box.y + 8 + (float)(i / 8) * cell, cell, cell);
        if(hovered(A_EMOJIPICK + i)) c.round(r, 6, kHover);
        c.text(kEmoji[i], r, Font::Icon, kText, Align::Center, false, true);
        hot(r, A_EMOJIPICK + i);
    }
}

void StarUI::drawMenu(Canvas& c){
    Rect box = rc(kRailW + kColW - 200, 44, 196, 84);
    c.round(box, 6, kPanel);
    c.outline(box, 6, 1.0f, kHover);
    hot(box, A_MODALBOX);
    Rect a = rc(box.x + 4, box.y + 4, box.w - 8, 36), b = rc(box.x + 4, box.y + 44, box.w - 8, 36);
    if(hovered(A_MENUADD)) c.round(a, 4, kHover);
    if(hovered(A_MENUCOPY)) c.round(b, 4, kHover);
    c.text("Add a friend", rc(a.x + 12, a.y, a.w - 12, a.h), Font::Body, kText, Align::Left, false, true);
    c.text("Copy my @name", rc(b.x + 12, b.y, b.w - 12, b.h), Font::Body, kText, Align::Left, false, true);
    hot(a, A_MENUADD);
    hot(b, A_MENUCOPY);
}

void StarUI::drawTip(Canvas& c, float w, float h){
    if(hoverTip_.empty() || mx_ < 0) return;
    float tw = c.textWidth(hoverTip_, Font::Small) + 20;
    float x = mx_ + 14, y = my_ + 18;
    if(x + tw > w - 4) x = w - tw - 4;
    if(y + 28 > h - 4) y = my_ - 34;
    Rect r = rc(x, y, tw, 26);
    c.round(r, 4, rgb(0x050508));
    c.text(hoverTip_, r, Font::Small, kText, Align::Center, false, true);
}

void StarUI::mouseMove(float x, float y){
    mx_ = x;
    my_ = y;
    std::string tip;
    int h = hit(x, y, &tip);
    if(h != hoverAction_ || tip != hoverTip_){ hoverAction_ = h; hoverTip_ = tip; host_.redraw(); }
}

void StarUI::mouseDown(float x, float y){
    int a = hit(x, y, nullptr);
    if(a == A_MODALBOX){ host_.redraw(); return; }
    if(modal_ != MNone && a == A_MODALCLOSE){ modal_ = MNone; focus_ = FNone; host_.redraw(); return; }
    act(a);
    host_.redraw();
}

void StarUI::wheel(float x, float y, float delta){
    if(selected_ >= 0 && x >= chatX_ && x < chatX_ + chatW_ && y > kHeadH){ scroll_ += delta; host_.redraw(); }
}

std::string* StarUI::focused(){
    switch(focus_){
    case FMessage: return &msg_;
    case FAddCode: return &addCode_;
    case FAddNick: return &addNick_;
    case FMyName: return &myName_;
    default: return nullptr;
    }
}

size_t StarUI::capFor(Field f) const { return f == FMessage ? 1100 : 32; }

void StarUI::charInput(uint32_t cp){
    std::string* s = focused();
    if(!s || cp < 0x20 || cp == 0x7F) return;
    if(s->size() >= capFor(focus_)) return;
    appendUtf8(*s, cp);
    if(focus_ == FAddCode){ hub_.clearFound(); addErr_.clear(); }
    if(focus_ == FMyName) nameErr_.clear();
    host_.redraw();
}

void StarUI::key(Key k){
    std::string* s = focused();
    switch(k){
    case Key::Backspace:
        if(s) popUtf8(*s);
        if(focus_ == FAddCode){ hub_.clearFound(); addErr_.clear(); }
        break;
    case Key::Enter:
        if(focus_ == FMessage) sendMessage();
        else if(focus_ == FAddCode || focus_ == FAddNick) submitAdd();
        else if(focus_ == FMyName) saveName();
        break;
    case Key::Escape: if(modal_ != MNone){ modal_ = MNone; focus_ = FNone; } break;
    case Key::Tab: if(modal_ == MAdd && !hub_.foundCode().empty()) focus_ = focus_ == FAddCode ? FAddNick : FAddCode; break;
    case Key::Paste:
        if(s){
            std::string p = host_.pasteText();
            for(char& ch : p) if(ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
            p = safeText(p);
            size_t cap = capFor(focus_);
            if(focus_ == FAddCode){ hub_.clearFound(); addErr_.clear(); }
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
    if(a == 0){ focus_ = FNone; if(modal_ == MEmoji || modal_ == MMenu) modal_ = MNone; return; }
    if(a >= A_REQIGNORE && a < A_REQIGNORE + 100){ hub_.ignoreRequest((size_t)(a - A_REQIGNORE)); return; }
    if(a >= A_REQACCEPT && a < A_REQACCEPT + 100){ hub_.acceptRequest((size_t)(a - A_REQACCEPT)); return; }
    if(a >= A_EMOJIPICK){
        int i = a - A_EMOJIPICK;
        if(i < kEmojiCount && msg_.size() + 8 < capFor(FMessage)) msg_ += kEmoji[i];
        modal_ = MNone;
        focus_ = FMessage;
        return;
    }
    if(a >= A_FRIEND){
        selected_ = a - A_FRIEND;
        modal_ = MNone;
        Friend& f = hub_.friends()[(size_t)selected_];
        f.unread = 0;
        scroll_ = 0;
        if(f.state == LinkState::Idle || f.state == LinkState::Ended) hub_.open((size_t)selected_);
        focus_ = FMessage;
        return;
    }
    size_t idx = (size_t)selected_;
    switch(a){
    case A_HOME: selected_ = -1; modal_ = MNone; focus_ = FNone; break;
    case A_ADDOPEN: case A_MENUADD: modal_ = MAdd; addErr_.clear(); addCode_.clear(); addNick_.clear(); hub_.clearFound(); focus_ = FAddCode; break;
    case A_ADDCANCEL: modal_ = MNone; focus_ = FNone; break;
    case A_ADDOK: submitAdd(); break;
    case A_FCODE: focus_ = FAddCode; break;
    case A_FNICK: focus_ = FAddNick; break;
    case A_FMSG: focus_ = FMessage; break;
    case A_FMYNAME: focus_ = FMyName; break;
    case A_SAVENAME: saveName(); break;
    case A_COPY: case A_MENUCOPY:
        host_.copyText(hub_.myName().empty() ? hub_.myCode() : "@" + hub_.myName());
        flash_ = "Copied";
        modal_ = modal_ == MMenu ? MNone : modal_;
        break;
    case A_COPYCODE: host_.copyText(hub_.myCode()); flash_ = "Copied"; break;
    case A_SETTINGS: modal_ = MSettings; myName_.clear(); nameErr_.clear(); focus_ = FNone; break;
    case A_HUBS: modal_ = MHubs; focus_ = FNone; break;
    case A_MENU: modal_ = modal_ == MMenu ? MNone : MMenu; break;
    case A_MODALCLOSE: modal_ = MNone; focus_ = FNone; break;
    case A_EMOJI: modal_ = modal_ == MEmoji ? MNone : MEmoji; break;
    case A_ATTACH: flash_ = "Sending files is coming soon."; break;
    case A_BELL: flash_ = "Notification settings are coming soon."; break;
    case A_HEALTH: showHealth_ = !showHealth_; break;
    case A_SEND: sendMessage(); break;
    case A_RECONNECT: if(selected_ >= 0){ hub_.friends()[idx].state = LinkState::Idle; hub_.open(idx); } break;
    case A_CALL:
        if(selected_ >= 0){
            if(hub_.friends()[idx].state == LinkState::Connected) hub_.callAction(idx, CallAction::Call);
            else flash_ = "Connect to your friend first: open the chat and wait until it says Connected.";
        }
        break;
    case A_MUTE: if(selected_ >= 0) hub_.callAction(idx, CallAction::ToggleMute); break;
    case A_DEAFEN: if(selected_ >= 0) hub_.callAction(idx, CallAction::ToggleDeafen); break;
    case A_HANGUP: if(selected_ >= 0) hub_.callAction(idx, CallAction::Hangup); break;
    case A_ACCEPT: if(selected_ >= 0) hub_.callAction(idx, CallAction::Accept); break;
    case A_DECLINE: if(selected_ >= 0) hub_.callAction(idx, CallAction::Hangup); break;
    case A_CALLOK: if(selected_ >= 0 && idx < callDismissed_.size()) callDismissed_[idx] = true; break;
    default: break;
    }
}

void StarUI::saveName(){
    std::string err;
    if(hub_.setMyName(myName_, err)){ myName_.clear(); nameErr_.clear(); focus_ = FNone; flash_ = "Saved"; }
    else nameErr_ = err;
}

void StarUI::submitAdd(){
    std::string err;
    addErr_.clear();
    if(hub_.foundCode().empty()){ if(!hub_.finding()) hub_.find(addCode_); return; }
    if(hub_.addFound(addNick_, err)){
        modal_ = MNone;
        addCode_.clear();
        addNick_.clear();
        selected_ = (int)hub_.friends().size() - 1;
        scroll_ = 0;
        hub_.open((size_t)selected_);
        focus_ = FMessage;
    } else addErr_ = err;
}

void StarUI::sendMessage(){
    if(selected_ < 0) return;
    Friend& f = hub_.friends()[(size_t)selected_];
    std::string t = msg_;
    while(!t.empty() && t.back() == ' ') t.pop_back();
    if(t.empty() || f.state != LinkState::Connected) return;
    hub_.send((size_t)selected_, t);
    msg_.clear();
    scroll_ = 0;
}

}

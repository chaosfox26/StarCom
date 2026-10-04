#include "ui.h"
#include "../identity.h"
#include "../textsafe.h"
#include <cstdio>
#include <cstring>

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
const Color kDark = rgb(0x2A2A3A);

const float kRailW = 72, kColW = 240, kHeadH = 48;

const uint32_t gAdd = 0xE710, gMore = 0xE712, gSettings = 0xE713, gPhone = 0xE717, gMic = 0xE720, gSend = 0xE724,
               gStar = 0xE735, gEmoji = 0xE76E, gHangUp = 0xE778, gHeadphone = 0xE7F6, gMessage = 0xE8BD, gBell = 0xEA8F,
               gHealth = 0xE701, gSpeaker = 0xE767, gCheck = 0xE73E, gCancel = 0xE711, gPeople = 0xE716;

enum Action {
    A_HOME = 1, A_ADDOPEN, A_COPY, A_ADDOK, A_ADDCANCEL, A_FCODE, A_FNICK, A_FMSG, A_SEND, A_CALL, A_HEALTH, A_RECONNECT,
    A_FMYNAME, A_SAVENAME, A_SETTINGS, A_HUBS, A_MODALCLOSE, A_EMOJI, A_ATTACH, A_MUTE, A_DEAFEN, A_BELL, A_HANGUP,
    A_ACCEPT, A_DECLINE, A_CALLOK, A_MENU, A_COPYCODE, A_MENUADD, A_MENUCOPY,
    A_HUBMENU, A_HUBINVITE, A_HUBSET, A_HUBLEAVE, A_LEAVEOK, A_CREATE, A_JOIN, A_FHUBNAME, A_FINVITE, A_FRENAME, A_FCHAN,
    A_RENAME, A_ADDCHAN, A_CHANKIND, A_COPYINV, A_VLEAVE, A_VMUTE, A_VDEAFEN, A_MEMBERS, A_VOLUNTEER,
    A_MODALBOX = 900, A_FRIEND = 1000, A_EMOJIPICK = 2000, A_REQACCEPT = 3000, A_REQIGNORE = 3100,
    A_HUB = 4000, A_CHAN = 4100, A_KICK = 4200, A_BAN = 4300, A_ROLE = 4400, A_RMCHAN = 4500, A_UNBAN = 4600, A_HUBEND = 4700
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

std::string upper(const std::string& s){
    std::string o = s;
    for(char& ch : o) if(ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
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

std::string idOf(const uint8_t id[32]){ return std::string((const char*)id, 32); }

Color hubColour(const HubView& v){ return avatarColor(idOf(v.id)); }

int onlineCount(const HubView& v){
    int n = 0;
    for(const HubMemberView& m : v.members) if(m.online) n++;
    return n;
}

const HubChannelView* channelById(const HubView& v, uint64_t id){
    for(const HubChannelView& c : v.channels) if(c.id == id) return &c;
    return nullptr;
}

HubChannelLines* linesFor(HubState& hs, uint64_t id){
    for(HubChannelLines& l : hs.chans) if(l.channel == id) return &l;
    return nullptr;
}

std::string roleName(HubRole r){ return r == HubRole::Owner ? "Owner" : r == HubRole::Admin ? "Admin" : std::string(); }

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

HubState* StarUI::curHub(){
    if(selHub_.empty()) return nullptr;
    for(HubState& hs : hub_.hubs()) if(idOf(hs.view.id) == selHub_) return &hs;
    return nullptr;
}

void StarUI::selectChannel(HubState& hs, uint64_t channel){
    selChan_ = channel;
    scroll_ = 0;
    HubChannelLines* l = linesFor(hs, channel);
    if(l) l->unread = 0;
    hs.unread = 0;
    for(const HubChannelLines& cl : hs.chans) hs.unread += cl.unread;
}

void StarUI::selectHub(const std::string& id){
    selHub_ = id;
    selected_ = -1;
    selChan_ = 0;
    colScroll_ = 0;
    HubState* hs = curHub();
    if(!hs) return;
    for(const HubChannelView& ch : hs->view.channels) if(ch.kind == ChannelKind::Text){ selectChannel(*hs, ch.id); break; }
    focus_ = FMessage;
}

bool StarUI::anyDmCall(){
    for(size_t i = 0; i < hub_.friends().size(); i++){
        CallState s = hub_.callView(i).state;
        if(s == CallState::Calling || s == CallState::Ringing || s == CallState::InCall) return true;
    }
    return false;
}

void StarUI::paint(Canvas& c, float w, float h){
    hots_.clear();
    if(callDismissed_.size() < hub_.friends().size()) callDismissed_.resize(hub_.friends().size(), false);
    if(wantNewHub_){
        for(HubState& hs : hub_.hubs()){
            std::string id = idOf(hs.view.id);
            bool known = false;
            for(const std::string& k : knownHubs_) if(k == id) known = true;
            if(known) continue;
            wantNewHub_ = false;
            modal_ = MNone;
            selectHub(id);
            break;
        }
    }
    HubState* hs = curHub();
    if(!selHub_.empty() && !hs){ selHub_.clear(); selChan_ = 0; if(modal_ == MHubMenu || modal_ == MInvite || modal_ == MHubSettings || modal_ == MLeave) modal_ = MNone; }
    if(hs && !channelById(hs->view, selChan_)){
        selChan_ = 0;
        for(const HubChannelView& ch : hs->view.channels) if(ch.kind == ChannelKind::Text){ selectChannel(*hs, ch.id); break; }
    }
    c.fill(rc(0, 0, w, h), kChat);
    drawRail(c, h);
    float left = kRailW + kColW;
    if(hs){
        drawHubColumn(c, kRailW, h, *hs);
        float right = showMembers_ ? 240.0f : 0.0f;
        chatX_ = left;
        chatW_ = w - left - right;
        if(chatW_ < 260) chatW_ = 260;
        drawHubChat(c, chatX_, chatW_, h, *hs);
        if(right > 0) drawMembers(c, w - right, h, *hs);
    } else {
        drawColumn(c, kRailW, h);
        float right = showHealth_ && selected_ >= 0 ? 240.0f : 0.0f;
        chatX_ = left;
        chatW_ = w - left - right;
        if(chatW_ < 260) chatW_ = 260;
        if(selected_ < 0){
            drawHome(c, left, w - left, h);
            drawVoicePanel(c, chatX_, chatW_, h - 16);
        } else {
            drawChat(c, chatX_, chatW_, h);
            if(right > 0) drawHealth(c, w - right, h);
        }
    }
    if(modal_ == MAdd) drawAdd(c, w, h);
    else if(modal_ == MSettings) drawSettings(c, w, h);
    else if(modal_ == MHubs) drawHubs(c, w, h);
    else if(modal_ == MEmoji) drawEmoji(c, w, h);
    else if(modal_ == MMenu) drawMenu(c);
    else if(modal_ == MHubMenu) drawHubMenu(c);
    else if(modal_ == MInvite) drawInvite(c, w, h);
    else if(modal_ == MHubSettings) drawHubSettings(c, w, h);
    else if(modal_ == MLeave) drawLeave(c, w, h);
    hoverAction_ = hit(mx_, my_, &hoverTip_);
    drawTip(c, w, h);
}

void StarUI::drawRail(Canvas& c, float h){
    c.fill(rc(0, 0, kRailW, h), kRail);
    c.circle(36, 36, 24, kMagenta);
    c.text(glyph(gStar), rc(12, 12, 48, 48), Font::GlyphLarge, kText, Align::Center, false, true);
    c.fill(rc(22, 70, 28, 2), kHover);
    Rect home = rc(12, 84, 48, 48);
    bool homeSel = selHub_.empty();
    if(homeSel) c.fill(rc(0, 94, 4, 28), kText);
    c.round(home, 16, homeSel ? kBlue : hovered(A_HOME) ? kSelected : kHover);
    c.text(glyph(gMessage), home, Font::GlyphLarge, kText, Align::Center, false, true);
    hot(home, A_HOME, "Direct messages");
    std::vector<HubState>& hubs = hub_.hubs();
    float listTop = 140;
    float listH = h - listTop;
    float content = (float)hubs.size() * 56 + 80;
    float maxScroll = content > listH ? content - listH : 0;
    if(railScroll_ > maxScroll) railScroll_ = maxScroll;
    if(railScroll_ < 0) railScroll_ = 0;
    c.clip(rc(0, listTop, kRailW, listH));
    float y = listTop + 8 - railScroll_;
    for(size_t i = 0; i < hubs.size() && i < 100; i++){
        const HubView& v = hubs[i].view;
        int act = A_HUB + (int)i;
        bool sel = idOf(v.id) == selHub_;
        Rect r = rc(12, y, 48, 48);
        if(sel) c.fill(rc(0, y + 10, 4, 28), kText);
        else if(hubs[i].unread > 0) c.fill(rc(0, y + 20, 4, 8), kText);
        if(sel || hovered(act)) c.round(r, 16, hubColour(v));
        else c.circle(36, y + 24, 24, hubColour(v));
        c.text(initial(v.name), r, Font::Title, kText, Align::Center, false, true);
        if(hubs[i].unread > 0 && !sel){ c.circle(54, y + 6, 7, kRail); c.circle(54, y + 6, 5, kMagenta); }
        hot(r, act, v.name);
        y += 56;
    }
    Rect add = rc(12, y, 48, 48);
    c.circle(36, y + 24, 24, hovered(A_HUBS) ? kSelected : kHover);
    c.text(glyph(gAdd), add, Font::GlyphLarge, kBlue, Align::Center, false, true);
    hot(add, A_HUBS, "Create a hub or join one with an invite");
    c.text("Add Server", rc(0, y + 50, kRailW, 16), Font::Tiny, kMuted, Align::Center, false, true);
    c.unclip();
}

void StarUI::drawMePanel(Canvas& c, float x, float h){
    float py = h - 56;
    c.fill(rc(x, py, kColW, 56), kPanel);
    c.circle(x + 28, py + 28, 18, kMagenta);
    c.text(initial(hub_.myName().empty() ? std::string("you") : hub_.myName()), rc(x + 10, py + 10, 36, 36), Font::Bold, kText, Align::Center, false, true);
    c.circle(x + 41, py + 41, 6, kPanel);
    c.circle(x + 41, py + 41, 4, hub_.networkReady() ? kBlue : kGrey);
    c.text(hub_.myName().empty() ? std::string("You") : "@" + hub_.myName(), rc(x + 54, py + 8, 140, 20), Font::Bold, kText, Align::Left, false, true);
    bool ok = flashOk_ && !flash_.empty();
    c.text(ok ? flash_ : hub_.networkReady() ? std::string("Online") : std::string("Connecting..."), rc(x + 54, py + 28, 140, 18), Font::Small, hub_.networkReady() || ok ? kBlue : kMuted, Align::Left, false, true);
    iconButton(c, rc(x + kColW - 42, py + 12, 32, 32), gSettings, kMuted, A_SETTINGS, "Settings");
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
            std::string who = reqs[i].hint.empty() ? std::string("Someone") : "@" + reqs[i].hint + (reqs[i].inHub ? "" : "?");
            c.circle(x + 30, ry + 24, 16, avatarColor(reqs[i].code));
            c.text(initial(reqs[i].hint.empty() ? std::string("?") : reqs[i].hint), rc(x + 14, ry + 8, 32, 32), Font::Bold, kText, Align::Center, false, true);
            c.text(who, rc(x + 54, ry + 4, 110, 20), Font::Bold, kText, Align::Left, false, true);
            c.text(reqs[i].checking ? std::string("checking...") : reqs[i].inHub ? std::string("in a hub with you") : std::string("not verified yet"), rc(x + 54, ry + 24, 110, 18), Font::Small, kMuted, Align::Left, false, true);
            hot(row, A_MODALBOX, "Friend code: " + reqs[i].code);
            if(!reqs[i].checking){
                iconButton(c, rc(x + kColW - 84, ry + 8, 32, 32), gCheck, kBlue, A_REQACCEPT + (int)i, reqs[i].hint.empty() ? std::string("Accept and add this friend code") : reqs[i].inHub ? "Accept: add @" + reqs[i].hint + " as a friend and start chatting" : "Accept: check that @" + reqs[i].hint + "'s public record matches, then add and answer");
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
    drawMePanel(c, x, h);
}

void StarUI::drawHubColumn(Canvas& c, float x, float h, HubState& hs){
    const HubView& v = hs.view;
    c.fill(rc(x, 0, kColW, h), kColumn);
    c.fill(rc(x, kHeadH - 1, kColW, 1), kEdge);
    c.text(upper(v.name), rc(x + 16, 0, kColW - 60, kHeadH), Font::Bold, kText, Align::Left, false, true);
    iconButton(c, rc(x + kColW - 40, 8, 32, 32), gMore, kMuted, A_HUBMENU, v.myRole == HubRole::Member ? std::string("Members, leave") : v.myRole == HubRole::Owner ? std::string("Invite people, hub settings") : std::string("Invite people, hub settings, leave"));
    HubVoiceView vv = hub_.hubVoice();
    bool voiceHere = vv.active && std::memcmp(vv.hub, v.id, 32) == 0;
    float listTop = kHeadH + 4, listH = h - 56 - listTop;
    float content = 40 + 20;
    for(const HubChannelView& ch : v.channels){
        content += 36;
        if(ch.kind == ChannelKind::Voice) for(const HubMemberView& m : v.members) if(m.online && m.voiceChannel == ch.id) content += 26;
    }
    content += 40;
    float maxScroll = content > listH ? content - listH : 0;
    if(colScroll_ > maxScroll) colScroll_ = maxScroll;
    if(colScroll_ < 0) colScroll_ = 0;
    c.clip(rc(x, listTop, kColW, listH));
    float y = listTop + 12 - colScroll_;
    c.text(upper(v.name), rc(x + 16, y, kColW - 32, 20), Font::Small, kMuted, Align::Left, false, true);
    y += 26;
    for(size_t i = 0; i < v.channels.size() && i < 100; i++){
        const HubChannelView& ch = v.channels[i];
        if(ch.kind != ChannelKind::Text) continue;
        int act = A_CHAN + (int)i;
        Rect row = rc(x + 8, y, kColW - 16, 34);
        bool sel = ch.id == selChan_;
        if(sel) c.round(row, 4, kSelected);
        else if(hovered(act)) c.round(row, 4, kHover);
        HubChannelLines* cl = linesFor(hs, ch.id);
        bool unread = cl && cl->unread > 0 && !sel;
        c.text("#", rc(x + 16, y, 22, 34), Font::Title, kMuted, Align::Center, false, true);
        c.text(ch.name, rc(x + 44, y, kColW - 100, 34), unread ? Font::Bold : Font::Body, sel || unread || hovered(act) ? kText : kMuted, Align::Left, false, true);
        if(unread){
            Rect badge = rc(x + kColW - 44, y + 9, 24, 16);
            c.round(badge, 8, kMagenta);
            c.text(std::to_string(cl->unread > 99 ? 99 : cl->unread), badge, Font::Small, kText, Align::Center, false, true);
        }
        hot(row, act, "#" + ch.name);
        y += 36;
    }
    y += 8;
    c.text(glyph(gSpeaker), rc(x + 14, y, 20, 20), Font::Glyph, kMuted, Align::Center, false, true);
    c.text("VOICE", rc(x + 38, y, 120, 20), Font::Small, kMuted, Align::Left, false, true);
    y += 26;
    for(size_t i = 0; i < v.channels.size() && i < 100; i++){
        const HubChannelView& ch = v.channels[i];
        if(ch.kind != ChannelKind::Voice) continue;
        int act = A_CHAN + (int)i;
        Rect row = rc(x + 8, y, kColW - 16, 34);
        bool mine = voiceHere && vv.channel == ch.id;
        if(mine) c.round(row, 4, kSelected);
        else if(hovered(act)) c.round(row, 4, kHover);
        c.text(glyph(gSpeaker), rc(x + 16, y, 22, 34), Font::Glyph, mine ? kBlue : kMuted, Align::Center, false, true);
        c.text(ch.name, rc(x + 44, y, kColW - 60, 34), Font::Body, mine || hovered(act) ? kText : kMuted, Align::Left, false, true);
        hot(row, act, mine ? std::string("You are in this voice channel") : "Join the voice channel " + ch.name);
        y += 36;
        for(const HubMemberView& m : v.members){
            if(!m.online || m.voiceChannel != ch.id) continue;
            c.circle(x + 54, y + 12, 10, avatarColor(m.name));
            c.text(initial(m.name), rc(x + 44, y + 2, 20, 20), Font::Tiny, kText, Align::Center, false, true);
            bool me = friendCode(m.key) == hub_.myCode();
            c.text(me ? m.name + " (you)" : m.name, rc(x + 72, y, kColW - 88, 24), Font::Small, kMuted, Align::Left, false, true);
            y += 26;
        }
    }
    if(v.channels.empty()) c.text("This hub has no channels yet.", rc(x + 16, y, kColW - 32, 40), Font::Small, kMuted, Align::Left, true, false);
    c.unclip();
    drawMePanel(c, x, h);
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
        "4.  For a group, click + on the left to create a hub, then use ... next to its name to invite people.",
        "5.  Nobody in a conversation can see anybody's IP address: everything goes through relays, encrypted."
    };
    for(const char* st : steps){ float th = c.textHeight(st, cw, Font::Body); c.text(st, rc(cx, sy, cw, th + 4), Font::Body, kText, Align::Left, true, false); sy += th + 8; }
    Rect ai = rc(cx, sy + 16, cw, 92);
    c.round(ai, 6, kPanel);
    c.text("Built with AI, openly. StarCom's code is written with AI coding assistants (two instances of Claude by Anthropic) under the direction of Chaos, who designs the project and makes every decision. StarCom itself contains no AI: no AI features, no AI models, and nothing is ever sent to an AI service.",
        rc(cx + 14, sy + 24, cw - 28, 80), Font::Small, kMuted, Align::Left, true, false);
}

void StarUI::drawLines(Canvas& c, std::vector<ChatLine>& lines, float x, float w, float top, float bottom,
                       const std::function<std::string(const ChatLine&)>& whoOf, const std::function<Color(const ChatLine&)>& colourOf,
                       const std::string& emptyTitle, const std::string& emptyText){
    Rect area = rc(x, top, w, bottom - top);
    c.clip(area);
    float textW = w - 88;
    std::vector<float> heights(lines.size());
    std::vector<bool> heads(lines.size());
    float total = 0;
    for(size_t i = 0; i < lines.size(); i++){
        ChatLine& cl = lines[i];
        bool head = i == 0 || cl.system != lines[i - 1].system || lines[i - 1].system || whoOf(cl) != whoOf(lines[i - 1]);
        heads[i] = head;
        if(cl.cachedWidth != textW){ cl.cachedHeight = c.textHeight(cl.text, textW, cl.system ? Font::Small : Font::Body); cl.cachedWidth = textW; }
        heights[i] = cl.cachedHeight + (head ? 30 : 4);
        total += heights[i];
    }
    float maxScroll = total > area.h - 16 ? total - (area.h - 16) : 0;
    if(scroll_ > maxScroll) scroll_ = maxScroll;
    if(scroll_ < 0) scroll_ = 0;
    float y = bottom - 12 - total + scroll_;
    if(lines.empty()){
        c.text(emptyTitle, rc(x + 16, bottom - 150, w - 32, 36), Font::Big, kText, Align::Left, false, true);
        c.text(emptyText, rc(x + 16, bottom - 110, w - 32, 60), Font::Body, kMuted, Align::Left, true, false);
    }
    for(size_t i = 0; i < lines.size(); i++){
        const ChatLine& l = lines[i];
        bool head = heads[i];
        float lh = heights[i];
        if(y + lh >= top && y <= bottom){
            if(l.system){
                c.text(l.text, rc(x + 72, y + 26, textW, lh - 26), Font::Small, kMuted, Align::Left, true, false);
                c.text(l.time, rc(x + 72, y + 6, 80, 18), Font::Small, kMuted, Align::Left, false, true);
            } else {
                if(head){
                    std::string who = whoOf(l);
                    c.circle(x + 36, y + 24, 18, colourOf(l));
                    c.text(initial(who), rc(x + 18, y + 6, 36, 36), Font::Bold, kText, Align::Center, false, true);
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
}

void StarUI::drawInput(Canvas& c, float x, float w, float h, const std::string& placeholder, bool live){
    Rect bar = rc(x + 16, h - 64, w - 32, 48);
    c.round(bar, 8, kField);
    iconButton(c, rc(bar.x + 8, bar.y + 8, 32, 32), gAdd, kMuted, A_ATTACH, "Send a file (coming soon)");
    Rect fieldR = rc(bar.x + 44, bar.y, bar.w - 44 - 88, bar.h);
    c.clip(fieldR);
    if(msg_.empty()) c.text(placeholder, rc(fieldR.x + 4, fieldR.y, fieldR.w - 8, fieldR.h), Font::Body, kMuted, Align::Left, false, true);
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
    std::string me = hub_.myName().empty() ? std::string("You") : hub_.myName();
    std::string nick = f.nick;
    Color them = avatarColor(f.code);
    drawLines(c, f.lines, x, w, kHeadH, h - 76,
              [&](const ChatLine& l){ return l.mine ? me : nick; },
              [&](const ChatLine& l){ return l.mine ? kMagenta : them; },
              f.nick, "This is the beginning of your direct messages with " + f.nick + ". Messages are end-to-end encrypted and never stored anywhere; they disappear when you close StarCom.");
    std::string ph = live ? "Message @" + f.nick + "..." : f.state == LinkState::Connecting ? "Connecting to @" + f.nick + "..." : "Click Reconnect or wait for " + f.nick;
    drawInput(c, x, w, h, ph, live);
    if(f.state == LinkState::Ended || f.state == LinkState::Idle) button(c, rc(x + w - 124, h - 118, 108, 34), "Reconnect", kBlue, kText, A_RECONNECT, "Connect to your friend again");
    if(!flash_.empty() && !flashOk_ && selected_ >= 0) c.text(flash_, rc(x + 20, h - 88, w - 40, 20), Font::Small, kMagenta, Align::Left, false, true);
    size_t idx = (size_t)selected_;
    bool callShown = cv.state != CallState::Idle && !(cv.state == CallState::Ended && idx < callDismissed_.size() && callDismissed_[idx]);
    drawCallPanel(c, x, w, h - 76);
    drawVoicePanel(c, x, w, callShown ? h - 76 - 132 : h - 76);
}

void StarUI::drawHubChat(Canvas& c, float x, float w, float h, HubState& hs){
    const HubView& v = hs.view;
    const HubChannelView* ch = channelById(v, selChan_);
    std::string chName = ch ? ch->name : std::string();
    HubVoiceView vv = hub_.hubVoice();
    c.fill(rc(x, 0, w, kHeadH), kChat);
    c.fill(rc(x, kHeadH - 1, w, 1), kEdge);
    c.text("#", rc(x + 14, 0, 24, kHeadH), Font::Title, kMuted, Align::Center, false, true);
    float nw = c.textWidth(chName, Font::Bold);
    c.text(chName, rc(x + 42, 0, nw + 4, kHeadH), Font::Bold, kText, Align::Left, false, true);
    c.fill(rc(x + 52 + nw, 16, 1, 16), kHover);
    int online = onlineCount(v);
    c.text(std::to_string(online) + " Online", rc(x + nw + 62, 0, 140, kHeadH), Font::Body, kMuted, Align::Left, false, true);
    float icons = 4 * 40 + 8;
    float ix = x + w - icons;
    iconButton(c, rc(ix, 8, 32, 32), gMic, vv.active ? kText : kGrey, A_VMUTE, vv.active ? (vv.muted ? "Unmute your microphone" : "Mute your microphone") : "Join a voice channel first", false, vv.active && vv.muted);
    iconButton(c, rc(ix + 40, 8, 32, 32), gHeadphone, vv.active ? kText : kGrey, A_VDEAFEN, vv.active ? (vv.deafened ? "Hear everyone again" : "Deafen (hear nothing)") : "Join a voice channel first", false, vv.active && vv.deafened);
    iconButton(c, rc(ix + 80, 8, 32, 32), gBell, kMuted, A_BELL, "Notifications (coming soon)");
    iconButton(c, rc(ix + 120, 8, 32, 32), gPeople, kMuted, A_MEMBERS, "Members", showMembers_);
    std::string note = !flash_.empty() && !flashOk_ ? flash_ : hub_.hubNote();
    bool noteErr = !flash_.empty() && !flashOk_ ? true : hub_.hubNoteError();
    if(modal_ != MNone) note.clear();
    float bottom = note.empty() ? h - 76 : h - 100;
    HubChannelLines* cl = linesFor(hs, selChan_);
    if(cl){
        cl->unread = 0;
        hs.unread = 0;
        for(const HubChannelLines& o : hs.chans) hs.unread += o.unread;
        drawLines(c, cl->lines, x, w, kHeadH, bottom,
                  [](const ChatLine& l){ return l.who; },
                  [](const ChatLine& l){ return l.mine ? kMagenta : avatarColor(l.who); },
                  "Welcome to #" + chName, "This is the start of #" + chName + " in " + v.name + ". Messages are end-to-end encrypted, signed by their author, and never stored anywhere: they are only seen by members who are online.");
    } else {
        c.text(v.channels.empty() ? std::string("No channels yet") : std::string("Pick a channel on the left"), rc(x + 16, h - 190, w - 32, 36), Font::Big, kText, Align::Left, false, true);
    }
    drawInput(c, x, w, h, ch ? "Message #" + chName + "..." : std::string("Pick a channel first"), ch != nullptr);
    if(!note.empty()) c.text(note, rc(x + 20, h - 96, w - 40, 22), Font::Small, noteErr ? kMagenta : kBlue, Align::Left, false, true);
    drawVoicePanel(c, x, w, bottom);
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
        button(c, rc(box.x + pw - 110, box.y + 76, 96, 34), "OK", kDark, kText, A_CALLOK);
    }
}

void StarUI::drawVoicePanel(Canvas& c, float x, float w, float bottom){
    HubVoiceView vv = hub_.hubVoice();
    if(!vv.active) return;
    float pw = 300, ph = 124;
    Rect box = rc(x + w - pw - 16, bottom - ph - 8, pw, ph);
    c.round(box, 8, kPanel);
    c.outline(box, 8, 1.0f, kHover);
    hot(box, A_MODALBOX);
    std::string title = upper(vv.channelName.empty() ? std::string("Voice") : vv.channelName);
    float tw = c.textWidth(title, Font::Bold);
    c.text(title, rc(box.x + 14, box.y + 8, tw + 4, 22), Font::Bold, kText, Align::Left, false, true);
    c.fill(rc(box.x + 22 + tw, box.y + 13, 1, 14), kMuted);
    c.text(std::to_string(vv.names.size()) + " Active", rc(box.x + 30 + tw, box.y + 8, 120, 22), Font::Body, kMuted, Align::Left, false, true);
    c.text(glyph(gSpeaker), rc(box.x + pw - 34, box.y + 6, 24, 24), Font::Glyph, kMuted, Align::Center, false, true);
    std::string names;
    for(size_t i = 0; i < vv.names.size(); i++){ if(i) names += ", "; names += vv.names[i]; }
    c.text("(" + names + ")", rc(box.x + 14, box.y + 32, pw - 28, 18), Font::Small, kMuted, Align::Left, false, true);
    iconButton(c, rc(box.x + 14, box.y + 58, 40, 36), gMic, kText, A_VMUTE, vv.muted ? "Unmute" : "Mute", false, vv.muted);
    c.text("Mute", rc(box.x + 6, box.y + 94, 56, 16), Font::Tiny, kMuted, Align::Center, false, true);
    iconButton(c, rc(box.x + 66, box.y + 58, 40, 36), gHeadphone, kText, A_VDEAFEN, vv.deafened ? "Undeafen" : "Deafen", false, vv.deafened);
    c.text("Deafen", rc(box.x + 58, box.y + 94, 56, 16), Font::Tiny, kMuted, Align::Center, false, true);
    button(c, rc(box.x + 124, box.y + 58, pw - 138, 36), "DISCONNECT", kMagenta, kText, A_VLEAVE, "Leave the voice channel");
    c.text(vv.ping > 0 ? "latency: " + std::to_string(vv.ping) + " ms Ping" : std::string("Use headphones"), rc(box.x + 124, box.y + 96, pw - 138, 16), Font::Tiny, kMuted, Align::Center, false, true);
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

void StarUI::drawMembers(Canvas& c, float x, float h, HubState& hs){
    const HubView& v = hs.view;
    c.fill(rc(x, 0, 240, h), kPanel);
    c.clip(rc(x, 0, 240, h));
    float y = 12;
    for(int pass = 0; pass < 2; pass++){
        bool on = pass == 0;
        int n = 0;
        for(const HubMemberView& m : v.members) if(m.online == on) n++;
        if(!n) continue;
        c.text(std::string(on ? "ONLINE" : "OFFLINE") + " - " + std::to_string(n), rc(x + 16, y, 208, 24), Font::Small, kMuted, Align::Left, false, true);
        y += 28;
        for(const HubMemberView& m : v.members){
            if(m.online != on) continue;
            c.circle(x + 32, y + 18, 14, on ? avatarColor(m.name) : kGrey);
            c.text(initial(m.name), rc(x + 18, y + 4, 28, 28), Font::Bold, kText, Align::Center, false, true);
            c.text(m.name, rc(x + 54, y, 110, 36), Font::Body, on ? kText : kMuted, Align::Left, false, true);
            std::string r = roleName(m.role);
            if(!r.empty()) c.text(r, rc(x + 164, y, 64, 36), Font::Tiny, m.role == HubRole::Owner ? kMagenta : kBlue, Align::Right, false, true);
            y += 40;
        }
        y += 8;
    }
    c.unclip();
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
    button(c, rc(box.x + bw - 220, box.y + bh - 56, 96, 38), "Cancel", kDark, kText, A_ADDCANCEL);
    button(c, rc(box.x + bw - 116, box.y + bh - 56, 96, 38), found ? "Add" : "Find", kMagenta, kText, A_ADDOK, found ? "Add to your friends" : "Look for this @name");
}

void StarUI::drawSettings(Canvas& c, float w, float h){
    c.fill(rc(0, 0, w, h), rgb(0x000000, 0.7f));
    hot(rc(0, 0, w, h), A_MODALCLOSE);
    float bw = 520, bh = 610;
    if(bh > h - 20) bh = h - 20;
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
    button(c, rc(x + fw - 76, y + 22, 76, 32), "Copy", kDark, kText, A_COPYCODE, "Copy your friend code");
    y += 72;
    c.text("DATA FOLDER (YOUR IDENTITY, @NAME, FRIENDS LIST AND HUBS)", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text(hub_.dataFolder(), rc(x, y + 20, fw, 40), Font::Small, kText, Align::Left, true, false);
    y += 66;
    bool vol = hub_.volunteer();
    c.text("HELP THE NETWORK", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text("Carry other people's encrypted messages while StarCom is open, using up to 2 Mbps of your upload. You only pass along encrypted data you cannot read. Only the StarCom entry relays learn your IP address; other users never do.", rc(x, y + 20, fw - 100, 64), Font::Small, kText, Align::Left, true, false);
    button(c, rc(x + fw - 88, y + 22, 88, 36), vol ? "On" : "Off", vol ? kBlue : kDark, kText, A_VOLUNTEER, vol ? "Stop helping the network" : "Start helping the network");
    std::string vs;
    if(vol){
        BridgeStats st = hub_.bridgeStats();
        vs = st.relays == 0 ? std::string("Connecting to the StarCom relays...") : "Helping now: " + std::to_string(st.pairs) + (st.pairs == 1 ? " conversation" : " conversations") + " through " + std::to_string(st.relays) + (st.relays == 1 ? " relay" : " relays") + ", " + std::to_string(st.kbps) + " kbps.";
    } else vs = "Off. Nothing is carried for anyone.";
    c.text(vs, rc(x, y + 86, fw, 18), Font::Small, vol ? kBlue : kMuted, Align::Left, false, true);
    y += 114;
    c.text("ABOUT", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    c.text("StarCom: direct messages, voice calls and hubs with no accounts and no servers that read anything. Built with AI, openly: the code is written with AI coding assistants (two instances of Claude by Anthropic) under the direction of Chaos. StarCom itself contains no AI.", rc(x, y + 20, fw, 80), Font::Small, kText, Align::Left, true, false);
    button(c, rc(box.x + bw - 116, box.y + bh - 56, 96, 38), "Close", kDark, kText, A_MODALCLOSE);
}

void StarUI::drawHubs(Canvas& c, float w, float h){
    c.fill(rc(0, 0, w, h), rgb(0x000000, 0.7f));
    hot(rc(0, 0, w, h), A_MODALCLOSE);
    float bw = 480, bh = 420;
    Rect box = rc((w - bw) / 2, (h - bh) / 2, bw, bh);
    c.round(box, 8, kChat);
    hot(box, A_MODALBOX);
    float x = box.x + 20, y = box.y + 18, fw = bw - 40;
    c.text("Add a hub", rc(x, y, fw, 30), Font::Title, kText, Align::Left, false, true);
    c.text("A hub is a group space with text channels and voice channels. Only people you invite can join.", rc(x, y + 34, fw, 40), Font::Small, kMuted, Align::Left, true, false);
    y += 84;
    c.text("CREATE YOUR OWN", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    drawField(c, rc(x, y + 20, fw - 110, 40), hubName_, "hub name, for example: Night Owls", focus_ == FHubName, A_FHUBNAME);
    button(c, rc(x + fw - 98, y + 22, 98, 36), "Create", kMagenta, kText, A_CREATE, "Create the hub; you become its owner");
    y += 80;
    c.text("JOIN WITH AN INVITE", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    drawField(c, rc(x, y + 20, fw - 110, 40), invite_, "paste the invite code (it starts with SCH1-)", focus_ == FInvite, A_FINVITE);
    button(c, rc(x + fw - 98, y + 22, 98, 36), "Join", kBlue, kText, A_JOIN, "Join the hub");
    c.text("Ask a member for an invite code. The person who made it has to be online while you join.", rc(x, y + 66, fw, 40), Font::Small, kMuted, Align::Left, true, false);
    y += 112;
    if(!hub_.hubNote().empty()) c.text(hub_.hubNote(), rc(x, y, fw, 40), Font::Small, hub_.hubNoteError() ? kMagenta : kBlue, Align::Left, true, false);
    button(c, rc(box.x + bw - 116, box.y + bh - 56, 96, 38), "Close", kDark, kText, A_MODALCLOSE);
}

void StarUI::drawHubMenu(Canvas& c){
    HubState* hs = curHub();
    if(!hs) return;
    bool mod = hs->view.myRole != HubRole::Member;
    bool owner = hs->view.myRole == HubRole::Owner;
    float rows = (mod ? 2.0f : 1.0f) + (owner ? 0.0f : 1.0f);
    Rect box = rc(kRailW + kColW - 200, 44, 196, rows * 40 + 4);
    c.round(box, 6, kPanel);
    c.outline(box, 6, 1.0f, kHover);
    hot(box, A_MODALBOX);
    float y = box.y + 4;
    if(mod){
        Rect a = rc(box.x + 4, y, box.w - 8, 36);
        if(hovered(A_HUBINVITE)) c.round(a, 4, kHover);
        c.text("Invite people", rc(a.x + 12, a.y, a.w - 12, a.h), Font::Body, kBlue, Align::Left, false, true);
        hot(a, A_HUBINVITE);
        y += 40;
    }
    Rect b = rc(box.x + 4, y, box.w - 8, 36);
    if(hovered(A_HUBSET)) c.round(b, 4, kHover);
    c.text(mod ? "Hub settings" : "Members", rc(b.x + 12, b.y, b.w - 12, b.h), Font::Body, kText, Align::Left, false, true);
    hot(b, A_HUBSET);
    if(!owner){
        Rect d = rc(box.x + 4, y + 40, box.w - 8, 36);
        if(hovered(A_HUBLEAVE)) c.round(d, 4, kHover);
        c.text("Leave hub", rc(d.x + 12, d.y, d.w - 12, d.h), Font::Body, kMagenta, Align::Left, false, true);
        hot(d, A_HUBLEAVE);
    }
}

void StarUI::drawInvite(Canvas& c, float w, float h){
    HubState* hs = curHub();
    if(!hs) return;
    c.fill(rc(0, 0, w, h), rgb(0x000000, 0.7f));
    hot(rc(0, 0, w, h), A_MODALCLOSE);
    float bw = 500, bh = 300;
    Rect box = rc((w - bw) / 2, (h - bh) / 2, bw, bh);
    c.round(box, 8, kChat);
    hot(box, A_MODALBOX);
    float x = box.x + 20, y = box.y + 18, fw = bw - 40;
    c.text("Invite people to " + hs->view.name, rc(x, y, fw, 30), Font::Title, kText, Align::Left, false, true);
    c.text("Send this code privately to the people you want to invite, for example in a direct message. It works for 24 hours and for up to 10 people, while you are online.", rc(x, y + 36, fw, 60), Font::Small, kMuted, Align::Left, true, false);
    y += 104;
    c.text("INVITE CODE", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    Rect codeR = rc(x, y + 20, fw, 56);
    c.round(codeR, 6, kRail);
    const std::string& code = hub_.lastInvite();
    if(code.empty()) c.text(hub_.hubNote().empty() ? std::string("Making an invite...") : hub_.hubNote(), rc(x + 12, y + 20, fw - 24, 56), Font::Small, hub_.hubNote().empty() ? kMuted : kMagenta, Align::Left, true, true);
    else c.text(code, rc(x + 12, y + 20, fw - 24, 56), Font::Small, kText, Align::Left, true, true);
    if(!code.empty()) button(c, rc(box.x + bw - 220, box.y + bh - 56, 96, 38), "Copy", kMagenta, kText, A_COPYINV, "Copy the invite code");
    button(c, rc(box.x + bw - 116, box.y + bh - 56, 96, 38), "Done", kDark, kText, A_MODALCLOSE);
}

void StarUI::drawHubSettings(Canvas& c, float w, float h){
    HubState* hs = curHub();
    if(!hs) return;
    const HubView& v = hs->view;
    bool owner = v.myRole == HubRole::Owner;
    bool mod = v.myRole != HubRole::Member;
    c.fill(rc(0, 0, w, h), rgb(0x000000, 0.7f));
    hot(rc(0, 0, w, h), A_MODALCLOSE);
    float bw = 560, bh = h - 40 > 640 ? 640 : h - 40;
    Rect box = rc((w - bw) / 2, (h - bh) / 2, bw, bh);
    c.round(box, 8, kChat);
    hot(box, A_MODALBOX);
    float x = box.x + 20, fw = bw - 40;
    c.text(mod ? "Hub settings" : "Members", rc(x, box.y + 14, fw, 30), Font::Title, kText, Align::Left, false, true);
    Rect body = rc(box.x, box.y + 52, bw, bh - 52 - 64);
    c.clip(body);
    float y = body.y + 4 - setScroll_;
    if(owner){
        c.text("HUB NAME", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
        drawField(c, rc(x, y + 20, fw - 110, 40), rename_, v.name, focus_ == FRename, A_FRENAME);
        button(c, rc(x + fw - 98, y + 22, 98, 36), "Rename", kMagenta, kText, A_RENAME, "Rename the hub");
        y += 76;
        c.text("CHANNELS", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
        y += 24;
        for(size_t i = 0; i < v.channels.size() && i < 100; i++){
            const HubChannelView& ch = v.channels[i];
            Rect row = rc(x, y, fw, 34);
            c.round(row, 4, kPanel);
            c.text(ch.kind == ChannelKind::Voice ? glyph(gSpeaker) : std::string("#"), rc(x + 8, y, 24, 34), ch.kind == ChannelKind::Voice ? Font::Glyph : Font::Bold, kMuted, Align::Center, false, true);
            c.text(ch.name, rc(x + 40, y, fw - 150, 34), Font::Body, kText, Align::Left, false, true);
            int act = A_RMCHAN + (int)i;
            bool armed = confirm_ == act && confirmChan_ == ch.id;
            button(c, rc(x + fw - 100, y + 3, 92, 28), armed ? "Sure?" : "Remove", kDark, armed ? kMagenta : kText, act, "Remove this channel");
            y += 38;
        }
        drawField(c, rc(x, y + 4, fw - 230, 40), chanName_, "new channel name", focus_ == FChanName, A_FCHAN);
        button(c, rc(x + fw - 222, y + 6, 116, 36), chanVoice_ ? "Voice" : "Text", kDark, kText, A_CHANKIND, "Click to switch between a text and a voice channel");
        button(c, rc(x + fw - 98, y + 6, 98, 36), "Add", kBlue, kText, A_ADDCHAN, "Add the channel");
        y += 60;
    }
    c.text("MEMBERS (" + std::to_string(v.members.size()) + ")", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
    y += 24;
    for(size_t i = 0; i < v.members.size() && i < 100; i++){
        const HubMemberView& m = v.members[i];
        Rect row = rc(x, y, fw, 40);
        c.round(row, 4, kPanel);
        c.circle(x + 22, y + 20, 14, m.online ? avatarColor(m.name) : kGrey);
        c.text(initial(m.name), rc(x + 8, y + 6, 28, 28), Font::Bold, kText, Align::Center, false, true);
        std::string r = roleName(m.role);
        c.text(m.name + (r.empty() ? std::string() : "  (" + r + ")"), rc(x + 44, y, fw - 330, 40), Font::Body, m.online ? kText : kMuted, Align::Left, false, true);
        bool self = friendCode(m.key) == hub_.myCode();
        bool canMod = !self && m.role != HubRole::Owner && (owner || (mod && m.role == HubRole::Member));
        float bx = x + fw - 8;
        if(canMod){
            int ban = A_BAN + (int)i, kick = A_KICK + (int)i;
            bx -= 72;
            bool armedMember = std::memcmp(confirmKey_, m.key, 32) == 0;
            bool banArmed = armedMember && confirm_ == ban, kickArmed = armedMember && confirm_ == kick;
            button(c, rc(bx, y + 6, 72, 28), banArmed ? "Sure?" : "Ban", kDark, kMagenta, ban, "Remove them and stop them from coming back");
            bx -= 78;
            button(c, rc(bx, y + 6, 72, 28), kickArmed ? "Sure?" : "Kick", kDark, kickArmed ? kMagenta : kText, kick, "Remove them (they can come back with a new invite)");
        }
        if(owner && !self && m.role != HubRole::Owner){
            int role = A_ROLE + (int)i;
            bx -= 124;
            button(c, rc(bx, y + 6, 118, 28), m.role == HubRole::Admin ? "Remove admin" : "Make admin", kDark, kBlue, role, m.role == HubRole::Admin ? std::string("They stop being an admin") : std::string("Admins can invite, kick and ban members"));
        }
        y += 44;
    }
    if(owner && !v.banned.empty()){
        y += 8;
        c.text("BANNED (" + std::to_string(v.banned.size()) + ")", rc(x, y, fw, 18), Font::Small, kMuted, Align::Left, false, true);
        y += 24;
        for(size_t i = 0; i < v.banned.size() && i < 100; i++){
            std::string code = friendCode(v.banned[i].key);
            std::string label = code.substr(0, 23) + "...";
            for(const Friend& f : hub_.friends()) if(f.code == code) label = f.nick + "  (" + code.substr(0, 17) + "...)";
            Rect row = rc(x, y, fw, 40);
            c.round(row, 4, kPanel);
            c.text(label, rc(x + 12, y, fw - 120, 40), Font::Body, kMuted, Align::Left, false, true);
            hot(rc(x, y, fw - 110, 40), A_MODALBOX, "Friend code: " + code);
            button(c, rc(x + fw - 100, y + 6, 92, 28), "Unban", kDark, kBlue, A_UNBAN + (int)i, "Let them join again with a new invite");
            y += 44;
        }
    }
    float content = y + setScroll_ - (body.y + 4);
    float maxScroll = content > body.h ? content - body.h : 0;
    if(setScroll_ > maxScroll) setScroll_ = maxScroll;
    c.unclip();
    std::string note = !flash_.empty() ? flash_ : hub_.hubNote();
    if(!note.empty()) c.text(note, rc(x, box.y + bh - 58, fw - 240, 40), Font::Small, (flash_.empty() ? hub_.hubNoteError() : !flashOk_) ? kMagenta : kBlue, Align::Left, true, true);
    if(!owner) button(c, rc(box.x + bw - 220, box.y + bh - 56, 96, 38), "Leave hub", kMagenta, kText, A_HUBLEAVE, "Leave this hub");
    button(c, rc(box.x + bw - 116, box.y + bh - 56, 96, 38), "Close", kDark, kText, A_MODALCLOSE);
}

void StarUI::drawLeave(Canvas& c, float w, float h){
    HubState* hs = curHub();
    if(!hs) return;
    c.fill(rc(0, 0, w, h), rgb(0x000000, 0.7f));
    hot(rc(0, 0, w, h), A_MODALCLOSE);
    float bw = 440, bh = 220;
    Rect box = rc((w - bw) / 2, (h - bh) / 2, bw, bh);
    c.round(box, 8, kChat);
    hot(box, A_MODALBOX);
    float x = box.x + 20, fw = bw - 40;
    c.text("Leave " + hs->view.name + "?", rc(x, box.y + 18, fw, 30), Font::Title, kText, Align::Left, false, true);
    std::string why = "To come back you need a new invite from the owner or an admin.";
    c.text(why, rc(x, box.y + 58, fw, 70), Font::Body, kMuted, Align::Left, true, false);
    button(c, rc(box.x + bw - 220, box.y + bh - 56, 96, 38), "Cancel", kDark, kText, A_MODALCLOSE);
    button(c, rc(box.x + bw - 116, box.y + bh - 56, 96, 38), "Leave", kMagenta, kText, A_LEAVEOK, "Leave the hub");
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
    if(modal_ != MNone && a == A_MODALCLOSE){ modal_ = MNone; focus_ = FNone; confirm_ = 0; host_.redraw(); return; }
    act(a);
    host_.redraw();
}

void StarUI::wheel(float x, float y, float delta){
    if(modal_ == MHubSettings){ setScroll_ -= delta; host_.redraw(); return; }
    if(x < kRailW){ railScroll_ -= delta; host_.redraw(); return; }
    if(x < kRailW + kColW && !selHub_.empty()){ colScroll_ -= delta; host_.redraw(); return; }
    if((selected_ >= 0 || !selHub_.empty()) && x >= chatX_ && x < chatX_ + chatW_ && y > kHeadH){ scroll_ += delta; host_.redraw(); }
}

std::string* StarUI::focused(){
    switch(focus_){
    case FMessage: return &msg_;
    case FAddCode: return &addCode_;
    case FAddNick: return &addNick_;
    case FMyName: return &myName_;
    case FHubName: return &hubName_;
    case FInvite: return &invite_;
    case FRename: return &rename_;
    case FChanName: return &chanName_;
    default: return nullptr;
    }
}

size_t StarUI::capFor(Field f) const {
    if(f == FMessage) return selHub_.empty() ? 1100 : kMaxHubText;
    if(f == FInvite) return 160;
    return 32;
}

void StarUI::charInput(uint32_t cp){
    std::string* s = focused();
    if(!s || cp < 0x20 || cp == 0x7F) return;
    std::string encoded;
    appendUtf8(encoded, cp);
    if(s->size() + encoded.size() > capFor(focus_)) return;
    s->append(encoded);
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
        else if(focus_ == FHubName) act(A_CREATE);
        else if(focus_ == FInvite) act(A_JOIN);
        else if(focus_ == FRename) act(A_RENAME);
        else if(focus_ == FChanName) act(A_ADDCHAN);
        break;
    case Key::Escape: if(modal_ != MNone){ modal_ = MNone; focus_ = FNone; confirm_ = 0; } break;
    case Key::Tab:
        if(modal_ == MAdd && !hub_.foundCode().empty()) focus_ = focus_ == FAddCode ? FAddNick : FAddCode;
        else if(modal_ == MHubs) focus_ = focus_ == FHubName ? FInvite : FHubName;
        break;
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

bool StarUI::actHub(int a){
    HubState* hs = curHub();
    if(a >= A_HUB && a < A_CHAN){
        size_t i = (size_t)(a - A_HUB);
        if(i < hub_.hubs().size()){ modal_ = MNone; selectHub(idOf(hub_.hubs()[i].view.id)); }
        return true;
    }
    if(!hs) return a >= A_HUB && a < A_HUBEND;
    const HubView& v = hs->view;
    uint8_t id[32];
    std::memcpy(id, v.id, 32);
    if(a >= A_CHAN && a < A_KICK){
        size_t i = (size_t)(a - A_CHAN);
        if(i >= v.channels.size()) return true;
        const HubChannelView& ch = v.channels[i];
        if(ch.kind == ChannelKind::Text){ selectChannel(*hs, ch.id); focus_ = FMessage; return true; }
        HubVoiceView vv = hub_.hubVoice();
        if(vv.active && std::memcmp(vv.hub, v.id, 32) == 0 && vv.channel == ch.id) return true;
        if(anyDmCall()){ flash_ = "Hang up your call first, then join the voice channel."; return true; }
        hub_.clearHubNote();
        hub_.voiceJoin(id, ch.id);
        return true;
    }
    if(a >= A_KICK && a < A_RMCHAN){
        int kind = (a - A_KICK) / 100;
        size_t i = (size_t)((a - A_KICK) % 100);
        if(i >= v.members.size()) return true;
        if(kind < 2 && (confirm_ != a || std::memcmp(confirmKey_, v.members[i].key, 32) != 0)){
            confirm_ = a;
            std::memcpy(confirmKey_, v.members[i].key, 32);
            return true;
        }
        confirm_ = 0;
        ManageArgs m;
        std::memcpy(m.member, v.members[i].key, 32);
        hub_.clearHubNote();
        if(kind == 0) hub_.manageHub(id, ManageOp::Kick, m);
        else if(kind == 1) hub_.manageHub(id, ManageOp::Ban, m);
        else {
            m.role = v.members[i].role == HubRole::Admin ? HubRole::Member : HubRole::Admin;
            hub_.manageHub(id, ManageOp::SetRole, m);
        }
        return true;
    }
    if(a >= A_UNBAN && a < A_HUBEND){
        size_t i = (size_t)(a - A_UNBAN);
        if(i >= v.banned.size() || v.myRole != HubRole::Owner) return true;
        ManageArgs m;
        std::memcpy(m.member, v.banned[i].key, 32);
        hub_.clearHubNote();
        hub_.manageHub(id, ManageOp::Unban, m);
        return true;
    }
    if(a >= A_RMCHAN && a < A_UNBAN){
        size_t i = (size_t)(a - A_RMCHAN);
        if(i >= v.channels.size()) return true;
        if(confirm_ != a || confirmChan_ != v.channels[i].id){ confirm_ = a; confirmChan_ = v.channels[i].id; return true; }
        confirm_ = 0;
        ManageArgs m;
        m.channel = v.channels[i].id;
        hub_.clearHubNote();
        hub_.manageHub(id, ManageOp::RemoveChannel, m);
        return true;
    }
    switch(a){
    case A_HUBMENU: modal_ = modal_ == MHubMenu ? MNone : MHubMenu; return true;
    case A_HUBINVITE: if(v.myRole == HubRole::Member) return true; hub_.clearHubNote(); hub_.makeInvite(id); modal_ = MInvite; focus_ = FNone; return true;
    case A_HUBSET: hub_.clearHubNote(); modal_ = MHubSettings; rename_.clear(); chanName_.clear(); setScroll_ = 0; focus_ = FNone; return true;
    case A_HUBLEAVE: if(v.myRole != HubRole::Owner){ modal_ = MLeave; focus_ = FNone; } return true;
    case A_LEAVEOK: hub_.leaveHub(id); modal_ = MNone; selHub_.clear(); selChan_ = 0; focus_ = FNone; return true;
    case A_COPYINV: if(!hub_.lastInvite().empty()){ host_.copyText(hub_.lastInvite()); flash_ = "Copied"; flashOk_ = true; } return true;
    case A_FRENAME: focus_ = FRename; return true;
    case A_FCHAN: focus_ = FChanName; return true;
    case A_CHANKIND: chanVoice_ = !chanVoice_; return true;
    case A_RENAME: {
        if(rename_.empty()) return true;
        ManageArgs m;
        m.name = rename_;
        hub_.clearHubNote();
        hub_.manageHub(id, ManageOp::Rename, m);
        rename_.clear();
        focus_ = FNone;
        return true; }
    case A_ADDCHAN: {
        if(chanName_.empty()) return true;
        if(v.channels.size() >= 24){ flash_ = "A hub can have up to 24 channels."; return true; }
        ManageArgs m;
        m.name = chanName_;
        m.kind = chanVoice_ ? ChannelKind::Voice : ChannelKind::Text;
        hub_.clearHubNote();
        hub_.manageHub(id, ManageOp::AddChannel, m);
        chanName_.clear();
        return true; }
    case A_MEMBERS: showMembers_ = !showMembers_; return true;
    default: return false;
    }
}

void StarUI::act(int a){
    flash_.clear();
    flashOk_ = false;
    if(a != 0 && a != confirm_ && !(a >= A_KICK && a < A_HUBEND)) confirm_ = 0;
    if(a == 0){ focus_ = FNone; confirm_ = 0; if(modal_ == MEmoji || modal_ == MMenu || modal_ == MHubMenu) modal_ = MNone; return; }
    if(a >= A_HUB && a < A_HUBEND){ actHub(a); return; }
    if(a >= A_REQIGNORE && a < A_REQIGNORE + 100){ hub_.ignoreRequest((size_t)(a - A_REQIGNORE)); return; }
    if(a >= A_REQACCEPT && a < A_REQACCEPT + 100){ hub_.acceptRequest((size_t)(a - A_REQACCEPT)); return; }
    if(a >= A_EMOJIPICK && a < A_EMOJIPICK + 100){
        int i = a - A_EMOJIPICK;
        if(i < kEmojiCount && msg_.size() + 8 < capFor(FMessage)) msg_ += kEmoji[i];
        modal_ = MNone;
        focus_ = FMessage;
        return;
    }
    if(a >= A_FRIEND && a < A_EMOJIPICK){
        size_t i = (size_t)(a - A_FRIEND);
        if(i >= hub_.friends().size()) return;
        selected_ = (int)i;
        selHub_.clear();
        modal_ = MNone;
        Friend& f = hub_.friends()[i];
        f.unread = 0;
        scroll_ = 0;
        if(f.state == LinkState::Idle || f.state == LinkState::Ended) hub_.open(i);
        focus_ = FMessage;
        return;
    }
    if(actHub(a)) return;
    size_t idx = (size_t)selected_;
    switch(a){
    case A_HOME: selected_ = -1; selHub_.clear(); modal_ = MNone; focus_ = FNone; break;
    case A_ADDOPEN: case A_MENUADD: modal_ = MAdd; addErr_.clear(); addCode_.clear(); addNick_.clear(); hub_.clearFound(); focus_ = FAddCode; break;
    case A_ADDCANCEL: modal_ = MNone; focus_ = FNone; break;
    case A_ADDOK: submitAdd(); break;
    case A_FCODE: focus_ = FAddCode; break;
    case A_FNICK: focus_ = FAddNick; break;
    case A_FMSG: focus_ = FMessage; break;
    case A_FMYNAME: focus_ = FMyName; break;
    case A_FHUBNAME: focus_ = FHubName; break;
    case A_FINVITE: focus_ = FInvite; break;
    case A_SAVENAME: saveName(); break;
    case A_COPY: case A_MENUCOPY:
        host_.copyText(hub_.myName().empty() ? hub_.myCode() : "@" + hub_.myName());
        flash_ = "Copied"; flashOk_ = true;
        modal_ = modal_ == MMenu ? MNone : modal_;
        break;
    case A_COPYCODE: host_.copyText(hub_.myCode()); flash_ = "Copied"; flashOk_ = true; break;
    case A_SETTINGS: modal_ = MSettings; myName_.clear(); nameErr_.clear(); focus_ = FNone; break;
    case A_HUBS: modal_ = MHubs; hub_.clearHubNote(); hubName_.clear(); invite_.clear(); focus_ = FHubName; break;
    case A_CREATE: {
        std::string n = hubName_;
        while(!n.empty() && n.back() == ' ') n.pop_back();
        if(n.empty()){ focus_ = FHubName; break; }
        hub_.clearHubNote();
        knownHubs_.clear();
        for(HubState& hs : hub_.hubs()) knownHubs_.push_back(idOf(hs.view.id));
        wantNewHub_ = true;
        hub_.createHub(n);
        hubName_.clear();
        break; }
    case A_JOIN: {
        if(invite_.empty()){ focus_ = FInvite; break; }
        hub_.clearHubNote();
        knownHubs_.clear();
        for(HubState& hs : hub_.hubs()) knownHubs_.push_back(idOf(hs.view.id));
        wantNewHub_ = true;
        hub_.joinHub(invite_);
        break; }
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
            if(hub_.hubVoice().active) flash_ = "Leave the voice channel first, then start the call.";
            else if(hub_.friends()[idx].state == LinkState::Connected) hub_.callAction(idx, CallAction::Call);
            else flash_ = "Connect to your friend first: open the chat and wait until it says Connected.";
        }
        break;
    case A_MUTE: if(selected_ >= 0) hub_.callAction(idx, CallAction::ToggleMute); break;
    case A_DEAFEN: if(selected_ >= 0) hub_.callAction(idx, CallAction::ToggleDeafen); break;
    case A_HANGUP: if(selected_ >= 0) hub_.callAction(idx, CallAction::Hangup); break;
    case A_ACCEPT:
        if(selected_ >= 0){
            if(hub_.hubVoice().active) flash_ = "Leave the voice channel first, then accept the call.";
            else hub_.callAction(idx, CallAction::Accept);
        }
        break;
    case A_DECLINE: if(selected_ >= 0) hub_.callAction(idx, CallAction::Hangup); break;
    case A_CALLOK: if(selected_ >= 0 && idx < callDismissed_.size()) callDismissed_[idx] = true; break;
    case A_VLEAVE: hub_.voiceLeave(); break;
    case A_VOLUNTEER: {
        std::string err;
        if(!hub_.setVolunteer(!hub_.volunteer(), err)) flash_ = err;
        break; }
    case A_VMUTE: if(hub_.hubVoice().active) hub_.hubVoiceMute(); break;
    case A_VDEAFEN: if(hub_.hubVoice().active) hub_.hubVoiceDeafen(); break;
    default: break;
    }
}

void StarUI::saveName(){
    std::string err;
    if(hub_.setMyName(myName_, err)){ myName_.clear(); nameErr_.clear(); focus_ = FNone; flash_ = "Saved"; flashOk_ = true; }
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
        selHub_.clear();
        scroll_ = 0;
        hub_.open((size_t)selected_);
        focus_ = FMessage;
    } else addErr_ = err;
}

void StarUI::sendMessage(){
    std::string t = msg_;
    while(!t.empty() && t.back() == ' ') t.pop_back();
    if(t.empty()) return;
    HubState* hs = curHub();
    if(hs){
        if(!channelById(hs->view, selChan_)) return;
        uint8_t id[32];
        std::memcpy(id, hs->view.id, 32);
        hub_.clearHubNote();
        hub_.postHub(id, selChan_, t);
        msg_.clear();
        scroll_ = 0;
        return;
    }
    if(selected_ < 0) return;
    Friend& f = hub_.friends()[(size_t)selected_];
    if(f.state != LinkState::Connected) return;
    hub_.send((size_t)selected_, t);
    msg_.clear();
    scroll_ = 0;
}

}

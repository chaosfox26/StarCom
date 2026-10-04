#include "hubcli.h"
#include "console.h"
#include "dht.h"
#include "identity.h"
#include "nethub.h"
#include "textsafe.h"
#include <chrono>
#include <cstring>
#include <map>
#include <thread>

namespace sc {

namespace {

std::string idOf(const uint8_t id[32]){
    return std::string((const char*)id, 32);
}

HubState* findHub(NetHub& net, const std::string& id){
    for(HubState& h : net.hubs()) if(idOf(h.view.id) == id) return &h;
    return nullptr;
}

const HubMemberView* memberByName(const HubState& h, const std::string& at){
    std::string n;
    std::string s = at;
    if(!s.empty() && s[0] == '@') s.erase(0, 1);
    if(!normalizeName(s, n)) return nullptr;
    for(const HubMemberView& m : h.view.members) if(m.name == n) return &m;
    return nullptr;
}

std::string roleName(HubRole r){
    return r == HubRole::Owner ? "owner" : r == HubRole::Admin ? "admin" : "member";
}

std::string channelName(const HubState& h, uint64_t id){
    for(const HubChannelView& c : h.view.channels) if(c.id == id) return safeText(c.name);
    return "?";
}

void help(){
    consolePrint(
        "Hub commands:\n"
        "  /create <name>        create a hub (you become its owner)\n"
        "  /join <invite code>   join a hub with an invite code\n"
        "  /hubs                 list your hubs and their channels\n"
        "  /use <number>         switch to hub <number> from /hubs\n"
        "  /ch <name>            switch to a text channel in this hub\n"
        "  /who                  who is in this hub, and who is online\n"
        "  /invite               make an invite code (owner and admins)\n"
        "  /vc [name]            join a voice channel (use headphones)\n"
        "  /novc                 leave the voice channel\n"
        "  /mute  /deafen        voice channel controls\n"
        "  /kick @name  /ban @name       remove someone (owner and admins)\n"
        "  /admin @name  /unadmin @name  change roles (owner)\n"
        "  /bans  /unban <number>        list and lift bans (owner)\n"
        "  /addch <name> [voice] add a channel (owner)\n"
        "  /leave                leave this hub\n"
        "  /quit                 close StarCom\n"
        "Anything else you type is sent to the current channel.\n");
}

}

int runHubConsole(){
    consoleInit();
    NetHub net;
    std::string err;
    if(!net.start(err)){ consolePrint(err + "\n"); return 1; }
    if(net.myName().empty()){
        consolePrint("Choose your @name first: starcom name <name>\n");
        net.shutdown();
        return 1;
    }
    consolePrint("StarCom hubs, as @" + net.myName() + ". Nobody in a hub ever sees your IP address. Type /help for commands.\n");
    LineInput in;
    in.start();
    std::map<std::string, size_t> printed;
    std::string lastNet;
    std::string curHub;
    uint64_t curCh = 0;
    bool quit = false;
    auto usable = [&]() -> HubState* {
        HubState* h = findHub(net, curHub);
        if(!h) consolePrint("* no hub selected: /create one, /join one, or /use <number>\n");
        return h;
    };
    auto selectHub = [&](HubState& h){
        curHub = idOf(h.view.id);
        curCh = 0;
        for(const HubChannelView& c : h.view.channels) if(c.kind == ChannelKind::Text){ curCh = c.id; break; }
        consolePrint("* now in " + safeText(h.view.name) + (curCh ? " #" + channelName(h, curCh) : std::string()) + "\n");
    };
    while(!quit){
        net.pump();
        std::string nn = net.networkNote();
        if(nn != lastNet){
            lastNet = nn;
            if(!nn.empty()) consolePrint("* " + nn + "\n");
        }
        std::string note = net.hubNote(), inv = net.lastInvite();
        if(!note.empty() || !inv.empty()){
            if(!note.empty()) consolePrint("* " + safeText(note) + "\n");
            if(!inv.empty()) consolePrint("* invite code (share it privately; works for 24 hours and 10 people):\n  " + inv + "\n");
            net.clearHubNote();
        }
        if(!curHub.empty() && !findHub(net, curHub)){
            curHub.clear();
            curCh = 0;
        }
        if(curHub.empty() && !net.hubs().empty()) selectHub(net.hubs().front());
        for(HubState& h : net.hubs()){
            for(HubChannelLines& cl : h.chans){
                std::string k = idOf(h.view.id) + std::string((const char*)&cl.channel, 8);
                size_t& done = printed[k];
                if(done > cl.trimmed + cl.lines.size() || done < cl.trimmed) done = cl.trimmed;
                for(; done < cl.trimmed + cl.lines.size(); done++){
                    const ChatLine& l = cl.lines[done - cl.trimmed];
                    std::string where = idOf(h.view.id) == curHub && cl.channel == curCh ? std::string() : "[" + safeText(h.view.name) + " #" + channelName(h, cl.channel) + "] ";
                    consolePrint(where + (l.mine ? std::string("you") : "@" + l.who) + ": " + l.text + "\n");
                }
                if(idOf(h.view.id) == curHub && cl.channel == curCh){ cl.unread = 0; h.unread = 0; }
            }
        }
        std::string line;
        while(!quit && in.next(line)){
            while(!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if(line.empty()) continue;
            std::string cmd = line, arg;
            size_t sp = line.find(' ');
            if(line[0] == '/' && sp != std::string::npos){ cmd = line.substr(0, sp); arg = line.substr(sp + 1); }
            else if(line[0] != '/') cmd.clear();
            if(cmd == "/quit"){ quit = true; break; }
            if(cmd == "/help"){ help(); continue; }
            if(cmd == "/create"){ net.createHub(arg); continue; }
            if(cmd == "/join"){ net.joinHub(arg); continue; }
            if(cmd == "/hubs"){
                auto& hs = net.hubs();
                if(hs.empty()) consolePrint("* you are not in any hub yet\n");
                for(size_t i = 0; i < hs.size(); i++){
                    const HubView& v = hs[i].view;
                    size_t on = 0;
                    for(const HubMemberView& m : v.members) if(m.online) on++;
                    std::string s = std::to_string(i + 1) + ". " + safeText(v.name) + " (" + std::to_string(v.members.size()) + " members, " + std::to_string(on) + " online, you are " + roleName(v.myRole) + ")" + (idOf(v.id) == curHub ? " <- current" : "") + "\n";
                    for(const HubChannelView& c : v.channels){
                        if(c.kind == ChannelKind::Text) s += "     #" + safeText(c.name) + "\n";
                        else {
                            s += "     (voice) " + safeText(c.name);
                            std::string inVc;
                            for(const HubMemberView& m : v.members) if(m.voiceChannel == c.id){ inVc += (inVc.empty() ? ": " : ", ") + std::string("@") + m.name; }
                            s += inVc + "\n";
                        }
                    }
                    consolePrint(s);
                }
                continue;
            }
            if(cmd == "/use"){
                size_t n = (size_t)std::atoi(arg.c_str());
                if(n < 1 || n > net.hubs().size()){ consolePrint("* no such hub number (see /hubs)\n"); continue; }
                selectHub(net.hubs()[n - 1]);
                continue;
            }
            if(cmd == "/novc"){ net.voiceLeave(); consolePrint("* left the voice channel\n"); continue; }
            if(cmd == "/mute"){ net.hubVoiceMute(); consolePrint(net.hubVoice().muted ? "* muted\n" : "* unmuted\n"); continue; }
            if(cmd == "/deafen"){ net.hubVoiceDeafen(); consolePrint(net.hubVoice().deafened ? "* deafened\n" : "* undeafened\n"); continue; }
            HubState* h = nullptr;
            if(!cmd.empty() && cmd != "/ch" && cmd != "/who" && cmd != "/invite" && cmd != "/vc" && cmd != "/kick" && cmd != "/ban" && cmd != "/admin" && cmd != "/unadmin" && cmd != "/addch" && cmd != "/leave" && cmd != "/bans" && cmd != "/unban"){
                consolePrint("* unknown command; /help lists them\n");
                continue;
            }
            if(!(h = usable())) continue;
            if(cmd.empty()){
                if(!curCh){ consolePrint("* this hub has no text channel\n"); continue; }
                net.postHub(h->view.id, curCh, line);
                continue;
            }
            if(cmd == "/ch"){
                std::string want = arg;
                if(!want.empty() && want[0] == '#') want.erase(0, 1);
                bool found = false;
                for(const HubChannelView& c : h->view.channels) if(c.kind == ChannelKind::Text && c.name == want){ curCh = c.id; found = true; }
                consolePrint(found ? "* now in #" + channelName(*h, curCh) + "\n" : std::string("* no text channel by that name (see /hubs)\n"));
                continue;
            }
            if(cmd == "/who"){
                std::string s;
                for(const HubMemberView& m : h->view.members) s += "  @" + m.name + " (" + roleName(m.role) + (m.online ? ", online" : "") + (m.voiceChannel ? ", in voice" : "") + ")\n";
                consolePrint(s);
                continue;
            }
            if(cmd == "/invite"){ net.makeInvite(h->view.id); continue; }
            if(cmd == "/vc"){
                uint64_t vc = 0;
                for(const HubChannelView& c : h->view.channels) if(c.kind == ChannelKind::Voice && (arg.empty() || c.name == arg)){ vc = c.id; break; }
                if(!vc){ consolePrint("* no voice channel by that name (see /hubs)\n"); continue; }
                net.voiceJoin(h->view.id, vc);
                consolePrint("* joining the voice channel " + channelName(*h, vc) + " (use headphones; /mute, /deafen, /novc)\n");
                continue;
            }
            if(cmd == "/leave"){ net.leaveHub(h->view.id); continue; }
            if(cmd == "/bans"){
                if(h->view.myRole != HubRole::Owner){ consolePrint("* only the hub owner can see and lift bans\n"); continue; }
                if(h->view.banned.empty()){ consolePrint("* nobody is banned from this hub\n"); continue; }
                std::string s;
                for(size_t i = 0; i < h->view.banned.size(); i++) s += "  " + std::to_string(i + 1) + ". " + friendCode(h->view.banned[i].key) + "\n";
                consolePrint(s);
                continue;
            }
            if(cmd == "/unban"){
                size_t n = (size_t)std::atoi(arg.c_str());
                if(h->view.myRole != HubRole::Owner || n < 1 || n > h->view.banned.size()){ consolePrint("* use /bans, then /unban <number> (owner only)\n"); continue; }
                ManageArgs a;
                std::memcpy(a.member, h->view.banned[n - 1].key, 32);
                net.manageHub(h->view.id, ManageOp::Unban, a);
                consolePrint("* ban lifted\n");
                continue;
            }
            if(cmd == "/addch"){
                ManageArgs a;
                std::string name = arg;
                if(name.size() > 6 && name.compare(name.size() - 6, 6, " voice") == 0){ a.kind = ChannelKind::Voice; name.resize(name.size() - 6); }
                a.name = name;
                net.manageHub(h->view.id, ManageOp::AddChannel, a);
                continue;
            }
            const HubMemberView* m = memberByName(*h, arg);
            if(!m){ consolePrint("* nobody called " + safeText(arg) + " is in this hub (see /who)\n"); continue; }
            ManageArgs a;
            std::memcpy(a.member, m->key, 32);
            if(cmd == "/kick") net.manageHub(h->view.id, ManageOp::Kick, a);
            else if(cmd == "/ban") net.manageHub(h->view.id, ManageOp::Ban, a);
            else {
                a.role = cmd == "/admin" ? HubRole::Admin : HubRole::Member;
                net.manageHub(h->view.id, ManageOp::SetRole, a);
            }
        }
        if(in.closed() && !quit){ quit = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    net.voiceLeave();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    net.shutdown();
    consolePrint("* StarCom closed\n");
    return 0;
}

}

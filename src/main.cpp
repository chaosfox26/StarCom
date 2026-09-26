#include "chat.h"
#include "console.h"
#include "handshake.h"
#include "hwprobe.h"
#include "identity.h"
#include "p2p.h"
#include "rendezvous.h"
#include "platform_net.h"
#include "seed.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace sc;

static const char* ID_FILE = "identity.key";
static const char* CONF_FILE = "starcom.conf";

static int usage(){
    std::printf(
        "StarCom sprint 3\n"
        "  starcom id                          create or show your identity and friend code\n"
        "  starcom probe                       show hardware video encoders and the capability JSON\n"
        "  starcom seed [name]                 look up the seed name (default: seed= in starcom.conf)\n"
        "  starcom listen <port>               wait for one peer and complete a handshake\n"
        "  starcom chat <friend code>          find a friend through the meeting point and chat\n"
        "                                      (needs rendezvous=<host>:<port> in starcom.conf)\n"
        "  starcom rendezvous <port>           run a meeting point + relay (keeps nothing on disk, no logs)\n"
        "  starcom connect <host|seed> <port> <friend code>\n"
        "                                      handshake with a listening peer\n");
    return 2;
}

static bool loadId(Identity& id){
    bool created = false;
    std::string err;
    if(!identityLoadOrCreate(ID_FILE, id, created, err)){ std::fprintf(stderr, "%s\n", err.c_str()); return false; }
    if(created) std::printf("Created a new identity in %s. Keep this file private; losing it means losing this identity.\n", ID_FILE);
    return true;
}

static int cmdId(){
    Identity id;
    if(!loadId(id)) return 1;
    std::printf("Your friend code:\n  %s\n", friendCode(id.pub).c_str());
    identityWipe(id);
    return 0;
}

static int cmdProbe(){
    HwCaps c = probeHardware();
    auto line = [](const char* n, const CodecCaps& k){
        std::printf("  %-6s hardware: %-3s software: %-3s", n, k.hw ? "yes" : "no", k.sw ? "yes" : "no");
        for(const std::string& e : k.engines) std::printf("  [%s]", e.c_str());
        std::printf("\n");
    };
    std::printf("Encoder probe via %s\n", c.api.c_str());
    if(!c.note.empty()) std::printf("  note: %s\n", c.note.c_str());
    line("AV1", c.av1);
    line("HEVC", c.hevc);
    line("H.264", c.h264);
    std::printf("Best local tier: %s\nCapability JSON: %s\n", bestTier(c).c_str(), capsJson(c).c_str());
    return 0;
}

static int cmdSeed(const std::string& arg){
    std::string name = arg.empty() ? configValue(CONF_FILE, "seed") : arg;
    std::vector<Addr> addrs;
    std::string err;
    if(!seedLookup(name, 0, addrs, err)){ std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::printf("Seed %s resolves to %zu entry point(s):\n", name.c_str(), addrs.size());
    for(const Addr& a : addrs){
        std::string s = a.str();
        std::printf("  %s\n", s.substr(0, s.rfind(':')).c_str());
    }
    return 0;
}

static std::string safeText(const std::string& in){
    std::string o;
    for(size_t i = 0; i < in.size(); i++){
        unsigned char c = (unsigned char)in[i];
        if(c == 0x7F || (c < 0x20 && c != '\t')){ o += '?'; continue; }
        if(c == 0xC2 && i + 1 < in.size() && (unsigned char)in[i + 1] >= 0x80 && (unsigned char)in[i + 1] <= 0x9F){ o += '?'; i++; continue; }
        o += (char)c;
    }
    return o;
}

static int chatWith(UdpSocket& s, Session& ses){
    std::string tag = friendCode(ses.peerPub).substr(0, 9);
    consolePrint("\nChat open with " + tag + ". Type a message and press Enter. /quit to leave.\n");
    LineInput input;
    input.start();
    ChatIO io;
    io.nextOutgoing = [&input](std::string& line){
        if(input.next(line)) return true;
        if(input.closed()){ line = "/quit"; return true; }
        return false;
    };
    io.onIncoming = [&tag](const std::string& text){ consolePrint("[" + tag + "] " + safeText(text) + "\n"); };
    io.onStatus = [](const std::string& m){ consolePrint("* " + m + "\n"); };
    std::string err;
    bool ok = runChat(s, ses, io, err);
    if(!ok) consolePrint("* chat ended: " + err + "\n");
    return ok ? 0 : 1;
}

static void report(const Session& s, const std::string& myCaps, const std::string& peerCaps){
    std::printf("Handshake complete with %s\n  peer friend code: %s\n  session id: local %016llx / peer %016llx\n  peer capabilities: %s\n  negotiated video codec: %s\n",
        s.peer.str().c_str(), friendCode(s.peerPub).c_str(), (unsigned long long)s.id, (unsigned long long)s.peerId, peerCaps.c_str(), negotiate(myCaps, peerCaps).c_str());
}

static int cmdListen(const std::string& portArg){
    int port = std::atoi(portArg.c_str());
    if(port <= 0 || port > 65535) return usage();
    Identity id;
    if(!loadId(id)) return 1;
    std::string myCaps = capsJson(probeHardware()), err;
    UdpSocket s;
    if(!s.open(familyV6(), (uint16_t)port, err) && !s.open(familyV4(), (uint16_t)port, err)){ std::fprintf(stderr, "%s\n", err.c_str()); identityWipe(id); return 1; }
    std::printf("Listening on UDP port %u as %s\n", (unsigned)s.localPort(), friendCode(id.pub).c_str());
    Session ses;
    std::string peerCaps;
    bool ok = hsRespond(s, id, myCaps, ses, peerCaps, err, 120000);
    identityWipe(id);
    if(!ok){ std::fprintf(stderr, "handshake failed: %s\n", err.c_str()); return 1; }
    report(ses, myCaps, peerCaps);
    int rc = chatWith(s, ses);
    sessionWipe(ses);
    return rc;
}

static int cmdConnect(const std::string& host, const std::string& portArg, const std::string& code){
    int port = std::atoi(portArg.c_str());
    uint8_t peerPub[32];
    if(port <= 0 || port > 65535) return usage();
    if(!parseFriendCode(code, peerPub)){ std::fprintf(stderr, "that is not a valid friend code\n"); return 1; }
    std::string name = host == "seed" ? configValue(CONF_FILE, "seed") : host;
    std::vector<Addr> addrs;
    std::string err;
    if(!seedLookup(name, (uint16_t)port, addrs, err)){ std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    Identity id;
    if(!loadId(id)) return 1;
    std::string myCaps = capsJson(probeHardware());
    for(const Addr& a : addrs){
        UdpSocket s;
        if(!s.open(a.family(), 0, err)) continue;
        std::printf("Handshaking with %s ...\n", a.str().c_str());
        Session ses;
        std::string peerCaps;
        if(hsInitiate(s, a, id, peerPub, myCaps, ses, peerCaps, err, 10000)){
            identityWipe(id);
            report(ses, myCaps, peerCaps);
            int rc = chatWith(s, ses);
            sessionWipe(ses);
            return rc;
        }
        std::fprintf(stderr, "  failed: %s\n", err.c_str());
    }
    identityWipe(id);
    return 1;
}

static int cmdChat(const std::string& code){
    uint8_t friendPub[32];
    if(!parseFriendCode(code, friendPub)){ std::fprintf(stderr, "that is not a valid friend code\n"); return 1; }
    std::string rv = configValue(CONF_FILE, "rendezvous");
    size_t colon = rv.rfind(':');
    if(rv.empty() || colon == std::string::npos){ std::fprintf(stderr, "set rendezvous=<host>:<port> in %s (the address comes from whoever runs your meeting point)\n", CONF_FILE); return 1; }
    int port = std::atoi(rv.substr(colon + 1).c_str());
    std::vector<Addr> addrs;
    std::string err;
    if(port <= 0 || port > 65535 || !resolve(rv.substr(0, colon), (uint16_t)port, addrs, err)){ std::fprintf(stderr, "cannot resolve the meeting point: %s\n", err.c_str()); return 1; }
    Addr server = addrs[0];
    for(const Addr& a : addrs) if(a.family() == familyV4()){ server = a; break; }
    Identity id;
    if(!loadId(id)) return 1;
    if(std::memcmp(id.pub, friendPub, 32) == 0){ identityWipe(id); std::fprintf(stderr, "that is your own friend code\n"); return 1; }
    std::string myCaps = capsJson(probeHardware());
    UdpSocket s;
    if(!s.open(server.family(), 0, err)){ identityWipe(id); std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::printf("You are %s\n", friendCode(id.pub).c_str());
    Session ses;
    std::string peerCaps;
    bool hideIp = configValue(CONF_FILE, "hideip") == "1";
    bool ok = connectFriend(s, id, friendPub, server, myCaps, hideIp, ses, peerCaps, err, [](const std::string& m){ consolePrint("* " + m + "\n"); });
    if(!ok){ announceOffline(s, id, friendPub, server); identityWipe(id); std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    report(ses, myCaps, peerCaps);
    int rc = chatWith(s, ses);
    sessionWipe(ses);
    announceOffline(s, id, friendPub, server);
    identityWipe(id);
    return rc;
}

int main(int argc, char** argv){
    if(argc < 2) return usage();
    consoleInit();
    if(!netInit()){ std::fprintf(stderr, "network init failed\n"); return 1; }
    std::string cmd = argv[1];
    int rc;
    if(cmd == "id") rc = cmdId();
    else if(cmd == "probe") rc = cmdProbe();
    else if(cmd == "seed") rc = cmdSeed(argc > 2 ? argv[2] : "");
    else if(cmd == "listen" && argc > 2) rc = cmdListen(argv[2]);
    else if(cmd == "connect" && argc > 4) rc = cmdConnect(argv[2], argv[3], argv[4]);
    else if(cmd == "chat" && argc > 2) rc = cmdChat(argv[2]);
    else if(cmd == "rendezvous" && argc > 2) rc = runRendezvous((uint16_t)std::atoi(argv[2]));
    else rc = usage();
    netShutdown();
    return rc;
}

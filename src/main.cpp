#include "chat.h"
#include "dht.h"
#include "console.h"
#ifdef _WIN32
#include "gui/gui.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "bridge.h"
#include "hubcli.h"
#include "hwprobe.h"
#include "engine.h"
#include "identity.h"
#include "link.h"
#include "paths.h"
#include "textsafe.h"
#include "voicecall.h"
#include "rendezvous.h"
#include "platform_net.h"
#include "scrub.h"
#include "seed.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace sc;

static const char* ID_FILE = "identity.key";
static const char* NAME_FILE = "name.txt";
static const char* FRIENDS_FILE = "friends.txt";
static std::string g_dataWhere;

static int usage(){
    std::printf(
        "StarCom\n"
        "  starcom                    open the StarCom window (Windows)\n"
        "  starcom name <name>        choose your @name (friends find you by it)\n"
        "  starcom chat @name         chat with a friend; neither of you can see the other's IP address\n"
        "  starcom call @name         the same, and ring your friend straight away (use headphones)\n"
        "  starcom hub                your hubs in the console: create, join, talk, voice channels\n"
        "  starcom volunteer [on|off|run]  help carry other people's encrypted messages (off by default)\n"
        "  starcom id                 show your friend code\n"
        "  starcom probe              show your hardware video encoders\n"
        "  starcom seed               look up this copy's network entry points\n"
        "  starcom scrub <file> <out> remove EXIF, GPS, text and other hidden data from a JPEG, PNG, GIF or WebP picture\n"
        "  starcom relay <port>       run a StarCom relay + entry point (keeps nothing about users, no logs)\n");
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
    if(!g_dataWhere.empty()) std::printf("StarCom data folder: %s\n", g_dataWhere.c_str());
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

static int cmdSeed(){
    std::vector<Addr> addrs;
    std::string err;
    if(!seedNodes(addrs, err) || addrs.empty()){ std::fprintf(stderr, "%s\n", err.empty() ? "this copy of StarCom has no network entry points" : err.c_str()); return 1; }
    std::printf("%zu network entry point(s) found for this copy of StarCom.\n", addrs.size());
    return 0;
}

static int cmdScrub(const std::string& inPath, const std::string& outPath){
    std::vector<uint8_t> data;
    {
        std::ifstream f(inPath, std::ios::binary);
        if(!f){ std::fprintf(stderr, "cannot open %s\n", inPath.c_str()); return 1; }
        f.seekg(0, std::ios::end);
        std::streamoff size = f.tellg();
        f.seekg(0, std::ios::beg);
        if(size < 0 || (uint64_t)size > kMaxScrubBytes){ std::fprintf(stderr, "the file is larger than 64 MB\n"); return 1; }
        data.resize((size_t)size);
        f.read((char*)data.data(), (std::streamsize)data.size());
        if((size_t)f.gcount() != data.size()){ std::fprintf(stderr, "cannot read %s\n", inPath.c_str()); return 1; }
    }
    std::vector<uint8_t> clean;
    ScrubReport rep;
    std::string err;
    if(!scrubFile(data, clean, rep, err)){ std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    FILE* o = std::fopen(outPath.c_str(), "wbx");
    if(!o){ std::fprintf(stderr, "cannot create %s (it must not exist yet)\n", outPath.c_str()); return 1; }
    bool ok = std::fwrite(clean.data(), 1, clean.size(), o) == clean.size();
    ok = (std::fclose(o) == 0) && ok;
    if(!ok){ std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 1; }
    std::printf("%s picture cleaned: %zu bytes became %zu bytes\n", kindName(rep.kind), rep.inBytes, rep.outBytes);
    if(rep.removed.empty()) std::printf("  nothing hidden was found\n");
    else {
        std::string list;
        for(const std::string& r : rep.removed) list += (list.empty() ? "" : ", ") + r;
        std::printf("  removed: %s\n", list.c_str());
    }
    if(rep.orientationKept) std::printf("  kept: which way up the picture is (rotation %d), nothing else from EXIF\n", rep.orientationKept);
    return 0;
}

static int chatWithCall(UdpSocket& s, Session& ses, const std::string& tag, bool lower, bool startCall, LineInput& input){
    Engine e(s, ses);
    VoiceCall vc(e, lower);
    std::atomic<int> shown((int)CallState::Idle);
    vc.setOnChange([&vc, &shown](){
        CallState st = vc.state();
        if((int)st == shown.load()) return;
        shown = (int)st;
        if(st == CallState::Ringing) consolePrint("* incoming call: type /accept to answer or /hangup to decline (use headphones)\n");
        else if(st == CallState::Calling) consolePrint("* calling... (/hangup to cancel)\n");
        else if(st == CallState::InCall) consolePrint("* in call: /mute, /deafen, /stats, /hangup (use headphones: there is no echo cancellation yet)\n");
        else if(st == CallState::Ended) consolePrint("* " + vc.note() + "\n");
    });
    consolePrint("\nChat open with " + tag + ". Type a message and press Enter. /call to start a voice call, /quit to leave.\n");
    if(startCall) vc.call();
    ChatIO io;
    io.nextOutgoing = [&input, &vc](std::string& line){
        for(;;){
            if(!input.next(line)){
                if(input.closed()){ line = "/quit"; return true; }
                return false;
            }
            if(line == "/call"){ vc.call(); continue; }
            if(line == "/accept"){ vc.accept(); continue; }
            if(line == "/hangup"){ vc.hangup(); continue; }
            if(line == "/mute"){ vc.setMuted(!vc.muted()); consolePrint(vc.muted() ? "* muted\n" : "* unmuted\n"); continue; }
            if(line == "/deafen"){ vc.setDeafened(!vc.deafened()); consolePrint(vc.deafened() ? "* deafened\n" : "* undeafened\n"); continue; }
            if(line == "/stats"){
                VoiceStats v = vc.stats();
                char buf[200];
                std::snprintf(buf, sizeof buf, "* voice: %llu packets in, %llu played, %llu filled in, %llu rebuilt by FEC, %llu late; ping %d ms\n",
                    (unsigned long long)v.received, (unsigned long long)v.played, (unsigned long long)v.concealed, (unsigned long long)v.recoveredFec, (unsigned long long)v.late, vc.pingMs());
                consolePrint(buf);
                continue;
            }
            return true;
        }
    };
    io.onIncoming = [&tag](const std::string& text){ consolePrint("[" + tag + "] " + safeText(text) + "\n"); };
    io.onStatus = [](const std::string& m){ consolePrint("* " + m + "\n"); };
    std::string err;
    bool ok = runChatWith(e, io, err);
    if(!ok) consolePrint("* chat ended: " + err + "\n");
    return ok ? 0 : 1;
}

static std::string readName(){
    std::ifstream f(NAME_FILE);
    std::string line, n;
    if(std::getline(f, line) && normalizeName(line, n)) return n;
    return std::string();
}

static int cmdName(const std::string& arg){
    std::string n;
    if(!normalizeName(arg, n)){ std::fprintf(stderr, "an @name is 3 to 32 characters: letters a-z, digits, '_' and '-'\n"); return 1; }
    std::ofstream f(NAME_FILE, std::ios::trunc);
    f << n << '\n';
    if(!f){ std::fprintf(stderr, "could not save %s\n", NAME_FILE); return 1; }
    std::printf("Your @name is now @%s. Friends find you by it while StarCom is running.\n", n.c_str());
    return 0;
}

static bool findPin(const std::string& name, uint8_t pub[32], bool& met){
    std::ifstream f(FRIENDS_FILE);
    std::string line;
    while(std::getline(f, line)){
        while(!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        met = line.size() > 5 && line.compare(line.size() - 5, 5, " +met") == 0;
        if(met) line.resize(line.size() - 5);
        size_t a = line.find(' ');
        if(a == std::string::npos) continue;
        size_t b = line.rfind(' ');
        if(b == a) continue;
        if(line.substr(b + 1) == name && parseFriendCode(line.substr(0, a), pub)) return true;
    }
    return false;
}

static void savePin(const std::string& code, const std::string& name){
    std::ofstream f(FRIENDS_FILE, std::ios::app);
    f << code << ' ' << name << ' ' << name << '\n';
}

static void markMet(const uint8_t pub[32]){
    std::ifstream in(FRIENDS_FILE);
    std::string line, all;
    bool changed = false;
    while(std::getline(in, line)){
        while(!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if(line.empty()) continue;
        size_t a = line.find(' ');
        uint8_t k[32];
        bool met = line.size() > 5 && line.compare(line.size() - 5, 5, " +met") == 0;
        if(!met && parseFriendCode(a == std::string::npos ? line : line.substr(0, a), k) && std::memcmp(k, pub, 32) == 0){ line += " +met"; changed = true; }
        all += line + "\n";
    }
    in.close();
    if(!changed) return;
    std::string tmp = std::string(FRIENDS_FILE) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << all;
        if(!out) return;
    }
    replaceFile(tmp, FRIENDS_FILE);
}

static const char* VOLUNTEER_FILE = "volunteer.txt";

static int cmdVolunteer(const std::string& arg){
    const char* what = "Volunteering means carrying other people's encrypted messages while StarCom is open, using up to 2 Mbps of your upload.\n"
                       "You only pass along encrypted data you cannot read. Only the StarCom entry relays learn your IP address; other users never do.\n";
    if(arg == "on" || arg == "off"){
        std::string tmp = std::string(VOLUNTEER_FILE) + ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            f << arg << '\n';
            if(!f){ std::fprintf(stderr, "could not save the setting\n"); return 1; }
        }
        if(!replaceFile(tmp, VOLUNTEER_FILE)){ std::fprintf(stderr, "could not save the setting\n"); return 1; }
        std::printf("Volunteering is now %s. It applies whenever the StarCom window or 'starcom hub' is open.\n%s", arg.c_str(), arg == "on" ? what : "");
        return 0;
    }
    if(arg == "run"){
        consoleInit();
        consolePrint(std::string(what) + "Helping the StarCom network now. Type /quit to stop.\n");
        BridgeHost bridge;
        bridge.start(2000);
        LineInput input;
        input.start();
        auto last = std::chrono::steady_clock::now();
        for(;;){
            std::string line;
            bool stop = false;
            while(input.next(line)) if(line == "/quit" || line == "q") stop = true;
            if(stop || input.closed()) break;
            if(std::chrono::steady_clock::now() - last >= std::chrono::seconds(10)){
                last = std::chrono::steady_clock::now();
                BridgeStats st = bridge.stats();
                consolePrint("* relays " + std::to_string(st.relays) + ", conversations " + std::to_string(st.pairs) + ", " + std::to_string(st.kbps) + " kbps, " + std::to_string(st.bytes / 1024) + " KB carried\n");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        bridge.stop();
        consolePrint("* stopped\n");
        return 0;
    }
    if(!arg.empty()){ std::fprintf(stderr, "use: starcom volunteer [on|off|run]\n"); return 1; }
    std::ifstream f(VOLUNTEER_FILE);
    std::string v;
    bool on = std::getline(f, v) && v.compare(0, 2, "on") == 0;
    std::printf("Volunteering is %s.\n%s'starcom volunteer on' or 'off' changes it; 'starcom volunteer run' helps right now in this window.\n", on ? "on" : "off", what);
    return 0;
}

static int cmdChatName(const std::string& arg, bool voice){
    std::string fname;
    if(!normalizeName(arg, fname)){ std::fprintf(stderr, "that is not a valid @name\n"); return 1; }
    std::string mine = readName();
    if(mine.empty()){ std::fprintf(stderr, "choose your own @name first: starcom name <name> (your friends find you by it)\n"); return 1; }
    if(mine == fname){ std::fprintf(stderr, "that is your own @name\n"); return 1; }
    Identity id;
    if(!loadId(id)) return 1;
    std::string myCaps = capsJson(probeHardware(), mine);
    std::mutex im;
    std::deque<uint64_t> incoming;
    RelayLink link;
    link.setIncoming([&im, &incoming](uint64_t c){ std::lock_guard<std::mutex> g(im); if(incoming.size() < 16) incoming.push_back(c); });
    link.start(id, mine);
    std::printf("You are @%s (%s)\n", mine.c_str(), friendCode(id.pub).c_str());
    std::string err;
    uint8_t pin[32];
    bool met = false;
    if(!findPin(fname, pin, met)){
        consolePrint("* connecting to the StarCom network...\n");
        if(!link.waitReady(30000, nullptr)){ identityWipe(id); std::fprintf(stderr, "%s\n", link.note().c_str()); return 1; }
        PointerRecord rec;
        bool got = false, said = false;
        for(int i = 0; i < 60 && !got; i++){
            got = link.lookup(fname, nullptr, rec, err);
            if(got) break;
            if(!said){ consolePrint("* @" + fname + " is not on the StarCom network yet; still looking (up to 2 minutes)...\n"); said = true; }
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        if(!got){ identityWipe(id); std::fprintf(stderr, "@%s was not found on the StarCom network\n", fname.c_str()); return 1; }
        std::memcpy(pin, rec.owner, 32);
        std::string code = friendCode(pin);
        std::printf("@%s has the friend code\n  %s\nCheck it with your friend (in person, by phone, ...). It is now saved in your friends list.\n", fname.c_str(), code.c_str());
        savePin(code, fname);
    }
    if(std::memcmp(pin, id.pub, 32) == 0){ identityWipe(id); std::fprintf(stderr, "that @name belongs to you\n"); return 1; }
    const bool lower = std::memcmp(id.pub, pin, 32) < 0;
    std::atomic<bool> running(true), adopted(false);
    std::mutex pm;
    std::unique_ptr<UdpSocket> ch(new UdpSocket());
    std::unique_ptr<UdpSocket> pendCh;
    Session ses, pendSes;
    std::string peerCaps, pendCaps;
    bool havePend = false;
    UdpSocket* current = nullptr;
    std::thread watcher([&]{
        while(running){
            uint64_t c = 0;
            {
                std::lock_guard<std::mutex> g(im);
                if(!incoming.empty()){ c = incoming.front(); incoming.pop_front(); }
            }
            if(!c){ std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
            std::unique_ptr<UdpSocket> sx(new UdpSocket());
            Session s;
            std::string pc, e;
            if(!answerMasked(link, c, *sx, id, myCaps, s, pc, e)) continue;
            if(lower || std::memcmp(s.peerPub, pin, 32) != 0){ sessionWipe(s); link.close(*sx); continue; }
            {
                std::lock_guard<std::mutex> g(pm);
                if(havePend){ link.close(*pendCh); sessionWipe(pendSes); }
                pendCh = std::move(sx);
                pendSes = s;
                pendCaps = pc;
                havePend = true;
                if(current) link.wake(*current);
            }
            sessionWipe(s);
            adopted = true;
        }
    });
    if(!lower) for(int i = 0; i < 50 && !adopted; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    bool ok = false;
    if(!adopted) ok = connectMasked(link, *ch, id, fname, pin, myCaps, ses, peerCaps, err, [](const std::string& m){ consolePrint("* " + m + "\n"); }, &adopted, !met);
    int rc = 1;
    bool first = true;
    LineInput input;
    input.start();
    for(;;){
        {
            std::lock_guard<std::mutex> g(pm);
            if(havePend){
                if(ok) sessionWipe(ses);
                link.close(*ch);
                ch = std::move(pendCh);
                ses = pendSes;
                sessionWipe(pendSes);
                peerCaps = pendCaps;
                havePend = false;
                ok = true;
            }
            current = ok ? ch.get() : nullptr;
        }
        if(!ok) break;
        if(first) std::printf("Connected to @%s through the relays (neither of you can see the other's IP address)\n  friend code: %s\n  negotiated video codec: %s\n", fname.c_str(), friendCode(ses.peerPub).c_str(), negotiate(myCaps, peerCaps).c_str());
        if(!met){ markMet(pin); met = true; }
        else consolePrint("* switched to @" + fname + "'s connection (you both connected at once)\n");
        bool startCall = voice && first;
        first = false;
        rc = chatWithCall(*ch, ses, "@" + fname, lower, startCall, input);
        {
            std::lock_guard<std::mutex> g(pm);
            current = nullptr;
        }
        sessionWipe(ses);
        link.close(*ch);
        bool pend;
        {
            std::lock_guard<std::mutex> g(pm);
            pend = havePend;
        }
        if(rc == 0) break;
        for(int i = 0; i < 100 && !pend && !lower; i++){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::lock_guard<std::mutex> g(pm);
            pend = havePend;
        }
        ok = false;
        ch.reset(new UdpSocket());
        if(!pend){
            consolePrint("* the conversation was closed; trying to reach @" + fname + " again (up to 2 minutes)...\n");
            adopted = false;
            ok = connectMasked(link, *ch, id, fname, pin, myCaps, ses, peerCaps, err, [](const std::string& m){ consolePrint("* " + m + "\n"); }, &adopted, !met);
            std::lock_guard<std::mutex> g(pm);
            if(!ok && !havePend) break;
        }
    }
    running = false;
    watcher.join();
    {
        std::lock_guard<std::mutex> g(pm);
        if(havePend){ link.close(*pendCh); sessionWipe(pendSes); }
    }
    identityWipe(id);
    link.setIncoming(nullptr);
    if(!ok && first){ std::fprintf(stderr, "%s\n", err.c_str()); link.stop(); return 1; }
    link.stop();
    return rc;
}

int main(int argc, char** argv){
    std::string dataErr;
    bool isServer = argc > 1 && std::string(argv[1]) == "relay";
    bool keepCwd = isServer || (argc > 1 && std::string(argv[1]) == "scrub");
    bool dataOk = keepCwd ? true : enterDataDir(g_dataWhere, dataErr);
#ifdef _WIN32
    if(argc < 2 || std::string(argv[1]) == "gui"){
        DWORD procs[4];
        if(GetConsoleProcessList(procs, 4) <= 1){
            HWND con = GetConsoleWindow();
            if(con) ShowWindow(con, SW_HIDE);
            FreeConsole();
        }
        if(!dataOk){ MessageBoxA(nullptr, dataErr.c_str(), "StarCom", MB_ICONERROR); return 1; }
        return runGui();
    }
#endif
    if(argc < 2) return usage();
    if(!dataOk){ std::fprintf(stderr, "%s\n", dataErr.c_str()); return 1; }
    consoleInit();
    if(!netInit()){ std::fprintf(stderr, "network init failed\n"); return 1; }
    std::string cmd = argv[1];
    int rc;
    if(cmd == "id") rc = cmdId();
    else if(cmd == "probe") rc = cmdProbe();
    else if(cmd == "seed") rc = cmdSeed();
    else if(cmd == "scrub" && argc > 3) rc = cmdScrub(argv[2], argv[3]);
    else if(cmd == "name" && argc > 2) rc = cmdName(argv[2]);
    else if(cmd == "chat" && argc > 2) rc = cmdChatName(argv[2], false);
    else if(cmd == "call" && argc > 2) rc = cmdChatName(argv[2], true);
    else if(cmd == "hub") rc = runHubConsole();
    else if(cmd == "volunteer") rc = cmdVolunteer(argc > 2 ? std::string(argv[2]) : std::string());
    else if(cmd == "relay" && argc > 2) rc = runRelay((uint16_t)std::atoi(argv[2]));
    else rc = usage();
    netShutdown();
    return rc;
}

#include "rendezvous.h"
#include "dht.h"
#include "identity.h"
#include "platform_net.h"
#include "relay.h"
#include "seed.h"
#include "wire.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace sc {

int runRelay(uint16_t port){
    UdpSocket s;
    std::string err;
    int fam = familyV6();
    if(!s.open(familyV6(), port, err)){
        fam = familyV4();
        if(!s.open(familyV4(), port, err)){
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
    }
    Identity relayId;
    bool created = false;
    if(!identityLoadOrCreate("relay.key", relayId, created, err)){
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::vector<Addr> dhtSeeds;
    std::string seedErr;
    if(!seedNodes(dhtSeeds, seedErr)) resolve("127.0.0.1", s.localPort(), dhtSeeds, seedErr);
    RelayServer relay(s, fam, relayId, dhtSeeds);
    identityWipe(relayId);
    DhtNode seedNode(s, false);
    std::printf("StarCom relay and Star DNS seed on UDP port %u. Only this relay's own key (relay.key) is kept on disk; nothing about users is written or logged.\n", (unsigned)s.localPort());
    std::fflush(stdout);
    uint8_t in[kMaxPacket + 64];
    auto lastRelayTick = std::chrono::steady_clock::now();
    auto lastSeedTick = lastRelayTick;
    for(;;){
        Addr from;
        int r = s.recvFrom(in, sizeof in, from, 200);
        auto now = std::chrono::steady_clock::now();
        if(now - lastRelayTick >= std::chrono::milliseconds(200)){
            lastRelayTick = now;
            relay.tick();
        }
        if(now - lastSeedTick >= std::chrono::seconds(5)){
            lastSeedTick = now;
            seedNode.tick();
        }
        if(r <= 0) continue;
        Header h;
        if(!readHeader(in, (size_t)r, h)) continue;
        if(h.type == PacketType::Dht){
            seedNode.handle(in, (size_t)r, from);
            continue;
        }
        relay.handle(in, (size_t)r, from);
    }
}

}

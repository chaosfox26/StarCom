#include "wire.h"

namespace sc {

const uint8_t kPrologue[] = {'S', 't', 'a', 'r', 'C', 'o', 'm', '/', '1'};
const size_t kPrologueLen = sizeof kPrologue;

void writeHeader(uint8_t* p, PacketType t, Lane l){
    p[0] = 'S';
    p[1] = 'C';
    p[2] = kWireVersion;
    p[3] = (uint8_t)t;
    p[4] = (uint8_t)l;
    p[5] = 0;
    p[6] = 0;
    p[7] = 0;
}

bool readHeader(const uint8_t* p, size_t n, Header& h){
    if(n < kHeaderLen || p[0] != 'S' || p[1] != 'C' || p[2] != kWireVersion) return false;
    if(p[3] < (uint8_t)PacketType::Hello || p[3] > (uint8_t)PacketType::Relay) return false;
    if(p[4] > (uint8_t)Lane::Background || p[5] || p[6] || p[7]) return false;
    h.type = (PacketType)p[3];
    h.lane = (Lane)p[4];
    return true;
}

void putU64(uint8_t* p, uint64_t v){
    for(int i = 0; i < 8; i++){ p[i] = (uint8_t)v; v >>= 8; }
}

uint64_t getU64(const uint8_t* p){
    uint64_t v = 0;
    for(int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

}

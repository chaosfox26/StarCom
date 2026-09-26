#pragma once
#include <cstddef>
#include <cstdint>
#include "platform_net.h"

namespace sc {

enum class Lane : uint8_t { Voice = 0, Signal = 1, Video = 2, Background = 3 };
enum class PacketType : uint8_t { Hello = 1, Welcome = 2, Data = 3, Rendezvous = 4, Relay = 5, Dht = 6, Link = 7 };

constexpr uint8_t kWireVersion = 1;
constexpr size_t kHeaderLen = 8;
constexpr size_t kDataHeaderLen = kHeaderLen + 16;
constexpr size_t kMaxPacket = 1400;
extern const uint8_t kPrologue[];
extern const size_t kPrologueLen;

struct Header {
    PacketType type = PacketType::Data;
    Lane lane = Lane::Signal;
};

void writeHeader(uint8_t* p, PacketType t, Lane l);
bool readHeader(const uint8_t* p, size_t n, Header& h);
void putU64(uint8_t* p, uint64_t v);
uint64_t getU64(const uint8_t* p);

size_t encodeAddr(const Addr& a, uint8_t out[19]);
bool decodeAddr(const uint8_t in[19], Addr& a);
Addr mapForSocket(const Addr& a, int socketFamily);

}

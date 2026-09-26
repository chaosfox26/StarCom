#pragma once
#include "identity.h"
#include "platform_net.h"
#include "wire.h"
#include <cstdint>
#include <vector>

namespace sc {

constexpr size_t kMailboxLen = 32;
constexpr size_t kMaxNote = 512;
constexpr size_t kGetPadded = 600;
constexpr size_t kBindPadded = 64;
constexpr size_t kMaxCandidates = 8;
constexpr uint16_t kNoteMaxAgeSec = 900;

enum RvOp : uint8_t { kRvPut = 1, kRvGet = 2, kRvPutOk = 3, kRvFound = 4, kRvMissing = 5 };
enum RelayOp : uint8_t { kRelayBind = 1, kRelayBound = 2 };

uint64_t currentHour();
bool friendSecret(const Identity& me, const uint8_t friendEdPub[32], uint8_t shared[32]);
void mailboxId(const uint8_t shared[32], uint64_t hour, const uint8_t ownerEdPub[32], uint8_t out[32]);
void relayToken(const uint8_t shared[32], uint64_t hour, uint8_t out[32]);
size_t sealNote(const uint8_t shared[32], const uint8_t ownerEdPub[32], const uint8_t mailbox[32], const std::vector<Addr>& cands, uint8_t* out, size_t cap);
bool openNote(const uint8_t shared[32], const uint8_t ownerEdPub[32], const uint8_t mailbox[32], const uint8_t* in, size_t n, uint64_t& stampMs, std::vector<Addr>& cands);
bool noteFresh(uint64_t stampMs, uint64_t lastSeenMs);

size_t buildPut(const uint8_t mailbox[32], const uint8_t* blob, size_t n, uint8_t* out, size_t cap);
size_t buildGet(const uint8_t mailbox[32], uint8_t* out, size_t cap);
size_t buildBind(const uint8_t token[32], uint8_t* out, size_t cap);
bool parseRendezvous(const uint8_t* pkt, size_t n, uint8_t& op, Addr& observed, const uint8_t*& blob, size_t& blobLen);
bool parseRelayReply(const uint8_t* pkt, size_t n, uint8_t& count);

size_t encodeAddr(const Addr& a, uint8_t out[19]);
bool decodeAddr(const uint8_t in[19], Addr& a);
Addr mapForSocket(const Addr& a, int socketFamily);

int runRendezvous(uint16_t port);

}

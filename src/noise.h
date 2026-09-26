#pragma once
#include <cstddef>
#include <cstdint>

namespace sc {

constexpr size_t kKeyLen = 32;
constexpr size_t kTagLen = 16;
constexpr size_t kStampLen = 8;
constexpr size_t kMaxHsPayload = 1024;
constexpr size_t kFirstOverhead = 32 + 48 + kStampLen + kTagLen;
constexpr size_t kSecondOverhead = 32 + kTagLen;

struct StaticKey {
    uint8_t sec[32];
    uint8_t pub[32];
};

bool staticFromSeed(const uint8_t seed[32], StaticKey& out);
void x25519FromEdPub(const uint8_t edPub[32], uint8_t xPub[32]);
uint64_t stampNow();

class NoiseChannel {
public:
    bool peekNonce(uint64_t& n) const { if(sendN_ == UINT64_MAX) return false; n = sendN_; return true; }
    bool seal(uint8_t* out, size_t cap, size_t& outLen, uint64_t& nonce,
              const uint8_t* ad, size_t adLen, const uint8_t* plain, size_t plainLen);
    bool open(uint64_t nonce, const uint8_t* ad, size_t adLen,
              const uint8_t* cipher, size_t cipherLen, uint8_t* plain, size_t& plainLen, bool& newest);
    ~NoiseChannel();

private:
    friend class NoiseHandshake;
    static constexpr uint64_t kWindow = 1024;
    uint8_t sendK_[32] = {};
    uint8_t recvK_[32] = {};
    uint64_t sendN_ = 0;
    uint64_t top_ = 0;
    uint64_t bits_[kWindow / 64] = {};
    bool seen(uint64_t n) const;
    void mark(uint64_t n);
};

class NoiseHandshake {
public:
    void startInitiator(const StaticKey& s, const uint8_t remoteStatic[32],
                         const uint8_t* prologue, size_t prologueLen);
    void startResponder(const StaticKey& s, const uint8_t* prologue, size_t prologueLen);

    bool writeFirst(uint8_t* out, size_t cap, size_t& outLen,
                     const uint8_t* payload, size_t payloadLen);
    bool readFirst(const uint8_t* in, size_t inLen, uint64_t& stamp,
                    uint8_t* payload, size_t cap, size_t& payloadLen);
    bool writeSecond(uint8_t* out, size_t cap, size_t& outLen,
                      const uint8_t* payload, size_t payloadLen);
    bool readSecond(const uint8_t* in, size_t inLen,
                     uint8_t* payload, size_t cap, size_t& payloadLen);

    const uint8_t* remoteStatic() const { return rs_; }
    const uint8_t* hash() const { return h_; }
    bool split(NoiseChannel& out) const;
    ~NoiseHandshake();

private:
    enum Step : uint8_t { kIdle, kInitSend1, kRespRecv1, kRespSend2, kInitRecv2, kDone, kFailed };
    uint8_t h_[64] = {};
    uint8_t ck_[64] = {};
    uint8_t k_[32] = {};
    bool hasK_ = false;
    uint64_t n_ = 0;
    StaticKey s_ = {};
    uint8_t eSec_[32] = {};
    uint8_t ePub_[32] = {};
    uint8_t rs_[32] = {};
    uint8_t re_[32] = {};
    bool initiator_ = false;
    Step step_ = kIdle;

    void init(const uint8_t* prologue, size_t prologueLen);
    void mixHash(const uint8_t* d, size_t n);
    void mixKey(const uint8_t* ikm, size_t n);
    bool mixDh(const uint8_t sec[32], const uint8_t pub[32]);
    bool encryptAndHash(uint8_t* out, const uint8_t* plain, size_t n);
    bool decryptAndHash(uint8_t* out, const uint8_t* cipher, size_t n);
    bool fail();
};

}

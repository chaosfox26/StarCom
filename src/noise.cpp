#include "noise.h"
#include "platform_net.h"
#include "../third_party/monocypher/monocypher.h"
#include "../third_party/monocypher/monocypher-ed25519.h"
#include <chrono>
#include <cstring>

namespace sc {

namespace {

const char kProtocol[] = "Noise_IK_25519_ChaChaPoly_BLAKE2b";

void hmac(uint8_t out[64], const uint8_t* key, size_t keyLen, const uint8_t* a, size_t an,
          const uint8_t* b = nullptr, size_t bn = 0){
    uint8_t pad[128] = {};
    std::memcpy(pad, key, keyLen);
    for(auto& c : pad) c ^= 0x36;
    uint8_t inner[64];
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 64);
    crypto_blake2b_update(&ctx, pad, 128);
    crypto_blake2b_update(&ctx, a, an);
    if(bn) crypto_blake2b_update(&ctx, b, bn);
    crypto_blake2b_final(&ctx, inner);
    for(auto& c : pad) c ^= 0x36 ^ 0x5c;
    crypto_blake2b_init(&ctx, 64);
    crypto_blake2b_update(&ctx, pad, 128);
    crypto_blake2b_update(&ctx, inner, 64);
    crypto_blake2b_final(&ctx, out);
    crypto_wipe(pad, sizeof pad);
    crypto_wipe(inner, sizeof inner);
}

void hkdf2(const uint8_t ck[64], const uint8_t* ikm, size_t n, uint8_t o1[64], uint8_t o2[64]){
    uint8_t temp[64];
    hmac(temp, ck, 64, ikm, n);
    const uint8_t one = 1, two = 2;
    hmac(o1, temp, 64, &one, 1);
    hmac(o2, temp, 64, o1, 64, &two, 1);
    crypto_wipe(temp, sizeof temp);
}

void makeNonce(uint8_t nonce[12], uint64_t n){
    std::memset(nonce, 0, 4);
    for(int i = 0; i < 8; i++) nonce[4 + i] = (uint8_t)(n >> (8 * i));
}

void aeadSeal(const uint8_t key[32], uint64_t n, const uint8_t* ad, size_t adLen,
               const uint8_t* plain, size_t len, uint8_t* out){
    uint8_t nonce[12];
    makeNonce(nonce, n);
    crypto_aead_ctx ctx;
    crypto_aead_init_ietf(&ctx, key, nonce);
    crypto_aead_write(&ctx, out, out + len, ad, adLen, plain, len);
    crypto_wipe(&ctx, sizeof ctx);
}

bool aeadOpen(const uint8_t key[32], uint64_t n, const uint8_t* ad, size_t adLen,
               const uint8_t* cipher, size_t len, uint8_t* out){
    uint8_t nonce[12];
    makeNonce(nonce, n);
    crypto_aead_ctx ctx;
    crypto_aead_init_ietf(&ctx, key, nonce);
    int bad = crypto_aead_read(&ctx, out, cipher + len, ad, adLen, cipher, len);
    crypto_wipe(&ctx, sizeof ctx);
    return bad == 0;
}

bool allZero(const uint8_t* p, size_t n){
    uint8_t acc = 0;
    for(size_t i = 0; i < n; i++) acc |= p[i];
    return acc == 0;
}

void putBe64(uint8_t* p, uint64_t v){
    for(int i = 7; i >= 0; i--){ p[i] = (uint8_t)v; v >>= 8; }
}

uint64_t getBe64(const uint8_t* p){
    uint64_t v = 0;
    for(int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

}

bool staticFromSeed(const uint8_t seed[32], StaticKey& out){
    uint8_t hash[64];
    crypto_sha512(hash, seed, 32);
    std::memcpy(out.sec, hash, 32);
    crypto_wipe(hash, sizeof hash);
    crypto_x25519_public_key(out.pub, out.sec);
    return !allZero(out.pub, 32);
}

void x25519FromEdPub(const uint8_t edPub[32], uint8_t xPub[32]){
    crypto_eddsa_to_x25519(xPub, edPub);
}

uint64_t stampNow(){
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool NoiseChannel::seen(uint64_t n) const {
    if(n >= top_) return false;
    if(top_ - n > kWindow) return true;
    return (bits_[(n % kWindow) / 64] >> (n % 64)) & 1;
}

void NoiseChannel::mark(uint64_t n){
    if(n >= top_){
        if(n - top_ >= kWindow){
            std::memset(bits_, 0, sizeof bits_);
        } else {
            for(uint64_t i = top_; i < n; i++) bits_[(i % kWindow) / 64] &= ~(1ull << (i % 64));
        }
        top_ = n + 1;
    }
    bits_[(n % kWindow) / 64] |= 1ull << (n % 64);
}

bool NoiseChannel::seal(uint8_t* out, size_t cap, size_t& outLen, uint64_t& nonce,
                   const uint8_t* ad, size_t adLen, const uint8_t* plain, size_t plainLen){
    if(sendN_ == UINT64_MAX || cap < plainLen + kTagLen) return false;
    nonce = sendN_++;
    aeadSeal(sendK_, nonce, ad, adLen, plain, plainLen, out);
    outLen = plainLen + kTagLen;
    return true;
}

bool NoiseChannel::open(uint64_t nonce, const uint8_t* ad, size_t adLen,
                   const uint8_t* cipher, size_t cipherLen, uint8_t* plain, size_t& plainLen, bool& newest){
    if(cipherLen < kTagLen || nonce == UINT64_MAX || seen(nonce)) return false;
    size_t len = cipherLen - kTagLen;
    if(!aeadOpen(recvK_, nonce, ad, adLen, cipher, len, plain)) return false;
    newest = nonce >= top_;
    mark(nonce);
    plainLen = len;
    return true;
}

NoiseChannel::~NoiseChannel(){
    crypto_wipe(sendK_, sizeof sendK_);
    crypto_wipe(recvK_, sizeof recvK_);
}

void NoiseHandshake::init(const uint8_t* prologue, size_t prologueLen){
    std::memset(h_, 0, sizeof h_);
    std::memcpy(h_, kProtocol, sizeof kProtocol - 1);
    std::memcpy(ck_, h_, 64);
    hasK_ = false;
    n_ = 0;
    mixHash(prologue, prologueLen);
}

void NoiseHandshake::startInitiator(const StaticKey& s, const uint8_t remoteStatic[32],
                                const uint8_t* prologue, size_t prologueLen){
    s_ = s;
    std::memcpy(rs_, remoteStatic, 32);
    initiator_ = true;
    init(prologue, prologueLen);
    mixHash(rs_, 32);
    step_ = kInitSend1;
}

void NoiseHandshake::startResponder(const StaticKey& s, const uint8_t* prologue, size_t prologueLen){
    s_ = s;
    initiator_ = false;
    init(prologue, prologueLen);
    mixHash(s_.pub, 32);
    step_ = kRespRecv1;
}

void NoiseHandshake::mixHash(const uint8_t* d, size_t n){
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 64);
    crypto_blake2b_update(&ctx, h_, 64);
    crypto_blake2b_update(&ctx, d, n);
    crypto_blake2b_final(&ctx, h_);
}

void NoiseHandshake::mixKey(const uint8_t* ikm, size_t n){
    uint8_t o2[64];
    hkdf2(ck_, ikm, n, ck_, o2);
    std::memcpy(k_, o2, 32);
    crypto_wipe(o2, sizeof o2);
    hasK_ = true;
    n_ = 0;
}

bool NoiseHandshake::mixDh(const uint8_t sec[32], const uint8_t pub[32]){
    uint8_t shared[32];
    crypto_x25519(shared, sec, pub);
    bool ok = !allZero(shared, 32);
    if(ok) mixKey(shared, 32);
    crypto_wipe(shared, sizeof shared);
    return ok;
}

bool NoiseHandshake::encryptAndHash(uint8_t* out, const uint8_t* plain, size_t n){
    if(!hasK_){
        std::memmove(out, plain, n);
        mixHash(out, n);
        return true;
    }
    if(n_ == UINT64_MAX) return false;
    aeadSeal(k_, n_++, h_, 64, plain, n, out);
    mixHash(out, n + kTagLen);
    return true;
}

bool NoiseHandshake::decryptAndHash(uint8_t* out, const uint8_t* cipher, size_t n){
    if(!hasK_){
        mixHash(cipher, n);
        std::memmove(out, cipher, n);
        return true;
    }
    if(n < kTagLen || n_ == UINT64_MAX) return false;
    uint8_t hBefore[64];
    std::memcpy(hBefore, h_, 64);
    mixHash(cipher, n);
    bool ok = aeadOpen(k_, n_, hBefore, 64, cipher, n - kTagLen, out);
    if(ok) n_++;
    return ok;
}

bool NoiseHandshake::fail(){
    step_ = kFailed;
    crypto_wipe(eSec_, sizeof eSec_);
    crypto_wipe(k_, sizeof k_);
    crypto_wipe(ck_, sizeof ck_);
    return false;
}

bool NoiseHandshake::writeFirst(uint8_t* out, size_t cap, size_t& outLen,
                            const uint8_t* payload, size_t payloadLen){
    if(step_ != kInitSend1 || payloadLen > kMaxHsPayload || cap < kFirstOverhead + payloadLen) return fail();
    if(!sysRandom(eSec_, 32)) return fail();
    crypto_x25519_public_key(ePub_, eSec_);
    std::memcpy(out, ePub_, 32);
    mixHash(ePub_, 32);
    if(!mixDh(eSec_, rs_)) return fail();
    if(!encryptAndHash(out + 32, s_.pub, 32)) return fail();
    if(!mixDh(s_.sec, rs_)) return fail();
    uint8_t body[kStampLen + kMaxHsPayload];
    putBe64(body, stampNow());
    if(payloadLen) std::memcpy(body + kStampLen, payload, payloadLen);
    bool ok = encryptAndHash(out + 80, body, kStampLen + payloadLen);
    crypto_wipe(body, kStampLen + payloadLen);
    if(!ok) return fail();
    outLen = kFirstOverhead + payloadLen;
    step_ = kInitRecv2;
    return true;
}

bool NoiseHandshake::readFirst(const uint8_t* in, size_t inLen, uint64_t& stamp,
                           uint8_t* payload, size_t cap, size_t& payloadLen){
    if(step_ != kRespRecv1 || inLen < kFirstOverhead || inLen - kFirstOverhead > kMaxHsPayload ||
        inLen - kFirstOverhead > cap) return fail();
    std::memcpy(re_, in, 32);
    mixHash(re_, 32);
    if(!mixDh(s_.sec, re_)) return fail();
    if(!decryptAndHash(rs_, in + 32, 48)) return fail();
    if(!mixDh(s_.sec, rs_)) return fail();
    size_t bodyLen = inLen - 80 - kTagLen;
    uint8_t body[kStampLen + kMaxHsPayload];
    if(!decryptAndHash(body, in + 80, inLen - 80)){ crypto_wipe(body, bodyLen); return fail(); }
    stamp = getBe64(body);
    payloadLen = bodyLen - kStampLen;
    if(payloadLen) std::memcpy(payload, body + kStampLen, payloadLen);
    crypto_wipe(body, bodyLen);
    step_ = kRespSend2;
    return true;
}

bool NoiseHandshake::writeSecond(uint8_t* out, size_t cap, size_t& outLen,
                             const uint8_t* payload, size_t payloadLen){
    if(step_ != kRespSend2 || payloadLen > kMaxHsPayload || cap < kSecondOverhead + payloadLen) return fail();
    if(!sysRandom(eSec_, 32)) return fail();
    crypto_x25519_public_key(ePub_, eSec_);
    std::memcpy(out, ePub_, 32);
    mixHash(ePub_, 32);
    if(!mixDh(eSec_, re_)) return fail();
    if(!mixDh(eSec_, rs_)) return fail();
    if(!encryptAndHash(out + 32, payload, payloadLen)) return fail();
    outLen = kSecondOverhead + payloadLen;
    crypto_wipe(eSec_, sizeof eSec_);
    step_ = kDone;
    return true;
}

bool NoiseHandshake::readSecond(const uint8_t* in, size_t inLen,
                            uint8_t* payload, size_t cap, size_t& payloadLen){
    if(step_ != kInitRecv2 || inLen < kSecondOverhead || inLen - kSecondOverhead > cap) return fail();
    std::memcpy(re_, in, 32);
    mixHash(re_, 32);
    if(!mixDh(eSec_, re_)) return fail();
    if(!mixDh(s_.sec, re_)) return fail();
    if(!decryptAndHash(payload, in + 32, inLen - 32)) return fail();
    payloadLen = inLen - kSecondOverhead;
    crypto_wipe(eSec_, sizeof eSec_);
    step_ = kDone;
    return true;
}

bool NoiseHandshake::split(NoiseChannel& out) const {
    if(step_ != kDone) return false;
    uint8_t o1[64], o2[64];
    hkdf2(ck_, nullptr, 0, o1, o2);
    std::memcpy(initiator_ ? out.sendK_ : out.recvK_, o1, 32);
    std::memcpy(initiator_ ? out.recvK_ : out.sendK_, o2, 32);
    out.sendN_ = 0;
    out.top_ = 0;
    std::memset(out.bits_, 0, sizeof out.bits_);
    crypto_wipe(o1, sizeof o1);
    crypto_wipe(o2, sizeof o2);
    return true;
}

NoiseHandshake::~NoiseHandshake(){
    crypto_wipe(eSec_, sizeof eSec_);
    crypto_wipe(k_, sizeof k_);
    crypto_wipe(ck_, sizeof ck_);
    crypto_wipe(&s_, sizeof s_);
}

}

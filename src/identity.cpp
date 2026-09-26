#include "identity.h"
#include "platform_net.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern "C" {
#include "../third_party/monocypher/monocypher.h"
#include "../third_party/monocypher/monocypher-ed25519.h"
}

namespace sc {

static const char B32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
static const uint8_t FILE_MAGIC[8] = {'S', 'C', 'I', 'D', 'v', '1', 0, 0};

static void derive(Identity& id){
    uint8_t seedCopy[32];
    std::memcpy(seedCopy, id.seed, 32);
    crypto_ed25519_key_pair(id.secret, id.pub, seedCopy);
    crypto_wipe(seedCopy, sizeof seedCopy);
}

static bool readFile(const std::string& path, std::vector<uint8_t>& out){
    FILE* f = std::fopen(path.c_str(), "rb");
    if(!f) return false;
    uint8_t buf[256];
    size_t n = std::fread(buf, 1, sizeof buf, f);
    std::fclose(f);
    out.assign(buf, buf + n);
    crypto_wipe(buf, sizeof buf);
    return true;
}

static FILE* createExclusive(const std::string& path){
#ifdef _WIN32
    return std::fopen(path.c_str(), "wbx");
#else
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if(fd < 0) return nullptr;
    FILE* f = ::fdopen(fd, "wb");
    if(!f) ::close(fd);
    return f;
#endif
}

bool identityLoadOrCreate(const std::string& path, Identity& id, bool& created, std::string& err){
    created = false;
    std::vector<uint8_t> data;
    if(readFile(path, data)){
        if(data.size() != 8 + 32 + 32 || std::memcmp(data.data(), FILE_MAGIC, 8) != 0){
            crypto_wipe(data.data(), data.size());
            err = "the identity file is damaged or not a StarCom identity: " + path;
            return false;
        }
        std::memcpy(id.seed, data.data() + 8, 32);
        derive(id);
        bool match = std::memcmp(id.pub, data.data() + 40, 32) == 0;
        crypto_wipe(data.data(), data.size());
        if(!match){ identityWipe(id); err = "the identity file failed its self-check: " + path; return false; }
        return true;
    }
    if(!sysRandom(id.seed, 32)){ err = "the operating system random generator failed"; return false; }
    derive(id);
    uint8_t out[8 + 32 + 32];
    std::memcpy(out, FILE_MAGIC, 8);
    std::memcpy(out + 8, id.seed, 32);
    std::memcpy(out + 40, id.pub, 32);
    FILE* f = createExclusive(path);
    if(!f){
        bool exists = errno == EEXIST;
        crypto_wipe(out, sizeof out); identityWipe(id);
        err = exists ? "the identity file exists but could not be read (permissions or another program holding it); it was NOT replaced: " + path : "cannot create the identity file: " + path;
        return false; }
    bool ok = std::fwrite(out, 1, sizeof out, f) == sizeof out;
    ok = (std::fclose(f) == 0) && ok;
    crypto_wipe(out, sizeof out);
    if(!ok){ identityWipe(id); err = "cannot write the identity file: " + path; return false; }
    created = true;
    return true;
}

static std::string base32(const uint8_t* p, size_t n){
    std::string s;
    uint32_t buf = 0;
    int bits = 0;
    for(size_t i = 0; i < n; i++){
        buf = (buf << 8) | p[i];
        bits += 8;
        while(bits >= 5){ s += B32[(buf >> (bits - 5)) & 31]; bits -= 5; }
    }
    if(bits > 0) s += B32[(buf << (5 - bits)) & 31];
    return s;
}

static bool unbase32(const std::string& s, std::vector<uint8_t>& out){
    uint32_t buf = 0;
    int bits = 0;
    for(char c : s){
        if(c == '-') continue;
        if(c >= 'a' && c <= 'z') c = (char)(c - 32);
        const char* q = std::strchr(B32, c);
        if(!q || c == 0) return false;
        buf = (buf << 5) | (uint32_t)(q - B32);
        bits += 5;
        if(bits >= 8){ out.push_back((uint8_t)(buf >> (bits - 8))); bits -= 8; }
    }
    return (buf & ((1u << bits) - 1)) == 0;
}

std::string friendCode(const uint8_t pub[32]){
    uint8_t body[34];
    std::memcpy(body, pub, 32);
    uint8_t h[32];
    crypto_blake2b(h, sizeof h, pub, 32);
    body[32] = h[0];
    body[33] = h[1];
    std::string b = base32(body, sizeof body);
    std::string out = "SC1";
    for(size_t i = 0; i < b.size(); i += 5) out += "-" + b.substr(i, 5);
    return out;
}

bool parseFriendCode(const std::string& code, uint8_t pub[32]){
    if(code.size() < 4 || (code.compare(0, 4, "SC1-") != 0 && code.compare(0, 4, "sc1-") != 0)) return false;
    std::vector<uint8_t> raw;
    if(!unbase32(code.substr(4), raw) || raw.size() != 34) return false;
    uint8_t h[32];
    crypto_blake2b(h, sizeof h, raw.data(), 32);
    if(h[0] != raw[32] || h[1] != raw[33]) return false;
    std::memcpy(pub, raw.data(), 32);
    return true;
}

void identityWipe(Identity& id){
    crypto_wipe(id.seed, sizeof id.seed);
    crypto_wipe(id.secret, sizeof id.secret);
}

}

#pragma once
#include <cstdint>
#include <string>

namespace sc {

struct Identity {
    uint8_t seed[32] = {0};
    uint8_t secret[64] = {0};
    uint8_t pub[32] = {0};
};

bool identityLoadOrCreate(const std::string& path, Identity& id, bool& created, std::string& err);
std::string friendCode(const uint8_t pub[32]);
bool parseFriendCode(const std::string& code, uint8_t pub[32]);
void identityWipe(Identity& id);

}

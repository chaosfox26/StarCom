#pragma once
#include <cstdint>

namespace sc {

int runRelay(uint16_t port, const char* keyFile = "relay.key", bool announce = false);

}

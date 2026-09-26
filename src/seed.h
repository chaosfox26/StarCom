#pragma once
#include "platform_net.h"
#include <string>
#include <vector>

namespace sc {

std::string configValue(const std::string& path, const std::string& key);
bool seedLookup(const std::string& name, uint16_t port, std::vector<Addr>& out, std::string& err);

}

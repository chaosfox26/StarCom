#pragma once
#include <string>

namespace sc {

bool enterDataDir(std::string& where, std::string& err);
bool replaceFile(const std::string& tmp, const std::string& dest);

}

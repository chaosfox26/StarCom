#pragma once
#include <string>

namespace sc {

bool enterDataDir(std::string& where, std::string& err);
std::string defaultRendezvous();
std::string meetingPoint(const std::string& confFile);

}

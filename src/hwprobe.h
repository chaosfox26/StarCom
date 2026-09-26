#pragma once
#include <string>
#include <vector>

namespace sc {

struct CodecCaps {
    bool hw = false;
    bool sw = false;
    std::vector<std::string> engines;
};

struct HwCaps {
    CodecCaps av1;
    CodecCaps hevc;
    CodecCaps h264;
    std::string api;
    std::string note;
};

HwCaps probeHardware();
std::string capsJson(const HwCaps& c);
std::string capsJson(const HwCaps& c, const std::string& name);
std::string capsName(const std::string& caps);
std::string bestTier(const HwCaps& c);
std::string negotiate(const std::string& mine, const std::string& peer);

}

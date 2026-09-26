#include "hwprobe.h"

namespace sc {

static std::string jsonEsc(const std::string& s){
    std::string o;
    for(char c : s){
        if(c == '"' || c == '\\'){ o += '\\'; o += c; }
        else if((unsigned char)c < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

static std::string codecJson(const char* name, const CodecCaps& c){
    std::string s = "\"" + std::string(name) + "\":{\"hw\":" + (c.hw ? "true" : "false") + ",\"sw\":" + (c.sw ? "true" : "false") + ",\"engines\":[";
    for(size_t i = 0; i < c.engines.size(); i++) s += (i ? "," : "") + std::string("\"") + jsonEsc(c.engines[i]) + "\"";
    return s + "]}";
}

std::string capsJson(const HwCaps& c, const std::string& name){
    std::string j = capsJson(c);
    if(name.empty()) return j;
    return "{\"name\":\"" + jsonEsc(name) + "\"," + j.substr(1);
}

std::string capsName(const std::string& caps){
    const std::string key = "\"name\":\"";
    size_t i = caps.find(key);
    if(i == std::string::npos) return std::string();
    i += key.size();
    size_t j = caps.find('"', i);
    if(j == std::string::npos || j - i > 32) return std::string();
    std::string n = caps.substr(i, j - i);
    for(char ch : n) if(!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return std::string();
    return n.size() >= 3 ? n : std::string();
}

std::string capsJson(const HwCaps& c){
    return "{\"v\":1,\"api\":\"" + jsonEsc(c.api) + "\"," + codecJson("av1", c.av1) + "," + codecJson("hevc", c.hevc) + "," + codecJson("h264", c.h264) + "}";
}

static bool hasHw(const std::string& json, const char* codec){
    std::string key = "\"" + std::string(codec) + "\":{\"hw\":true";
    return json.find(key) != std::string::npos;
}

std::string negotiate(const std::string& mine, const std::string& peer){
    if(hasHw(mine, "av1") && hasHw(peer, "av1")) return "AV1 (hardware, both sides)";
    if(hasHw(mine, "hevc") && hasHw(peer, "hevc")) return "HEVC/H.265 (hardware, both sides)";
    if(hasHw(mine, "h264") && hasHw(peer, "h264")) return "H.264 (hardware, both sides)";
    return "H.264 (software fallback)";
}

std::string bestTier(const HwCaps& c){
    if(c.av1.hw) return "1 AV1 (hardware)";
    if(c.hevc.hw) return "2 HEVC/H.265 (hardware)";
    if(c.h264.hw) return "3 H.264 (hardware)";
    return "4 H.264 (software fallback)";
}

}

#include "seed.h"
#include <fstream>

namespace sc {

std::string configValue(const std::string& path, const std::string& key){
    std::ifstream f(path);
    std::string line;
    while(std::getline(f, line)){
        if(!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if(eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while(!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
        while(!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(0, 1);
        if(k == key) return v;
    }
    return std::string();
}

bool seedLookup(const std::string& name, uint16_t port, std::vector<Addr>& out, std::string& err){
    if(name.empty()){ err = "no seed name given (pass one, or set seed= in starcom.conf)"; return false; }
    return resolve(name, port, out, err);
}

}

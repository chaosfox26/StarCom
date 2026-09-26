#include "seed.h"

namespace sc {

bool seedNodes(std::vector<Addr>& out, std::string& err){
#ifdef STARCOM_SEEDS
    std::string list = STARCOM_SEEDS;
#else
    std::string list;
#endif
    out.clear();
    if(list.empty()){ err = "this copy of StarCom has no seed nodes built in"; return false; }
    size_t pos = 0;
    while(pos <= list.size()){
        size_t comma = list.find(',', pos);
        std::string item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = comma == std::string::npos ? list.size() + 1 : comma + 1;
        size_t colon = item.rfind(':');
        if(colon == std::string::npos || colon == 0) continue;
        int port = 0;
        for(size_t i = colon + 1; i < item.size(); i++){
            if(item[i] < '0' || item[i] > '9'){ port = 0; break; }
            port = port * 10 + (item[i] - '0');
            if(port > 65535){ port = 0; break; }
        }
        if(port <= 0) continue;
        std::vector<Addr> addrs;
        std::string e;
        if(!resolve(item.substr(0, colon), (uint16_t)port, addrs, e)){ err = e; continue; }
        for(const Addr& a : addrs){
            bool dup = false;
            for(const Addr& b : out) if(b == a) dup = true;
            if(!dup) out.push_back(a);
        }
    }
    if(out.empty() && err.empty()) err = "none of the built-in seed names could be resolved";
    return !out.empty();
}

}

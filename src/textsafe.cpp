#include "textsafe.h"
#include <cstdint>

namespace sc {

static bool blocked(uint32_t cp){
    if(cp < 0x20 && cp != '\t') return true;
    if(cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) return true;
    if(cp == 0x061C || cp == 0x200E || cp == 0x200F || cp == 0x2028 || cp == 0x2029) return true;
    if(cp >= 0x202A && cp <= 0x202E) return true;
    if(cp >= 0x2066 && cp <= 0x2069) return true;
    if(cp == 0xFEFF || (cp >= 0xFFF9 && cp <= 0xFFFB)) return true;
    return false;
}

std::string safeText(const std::string& in){
    std::string o;
    o.reserve(in.size());
    size_t i = 0;
    while(i < in.size()){
        unsigned char c = (unsigned char)in[i];
        uint32_t cp = 0;
        size_t len = 0;
        if(c < 0x80){ cp = c; len = 1; }
        else if((c >> 5) == 6){ cp = c & 0x1F; len = 2; }
        else if((c >> 4) == 14){ cp = c & 0x0F; len = 3; }
        else if((c >> 3) == 30){ cp = c & 0x07; len = 4; }
        else { o += '?'; i++; continue; }
        if(i + len > in.size()){ o += '?'; break; }
        bool ok = true;
        for(size_t k = 1; k < len; k++){
            unsigned char d = (unsigned char)in[i + k];
            if((d & 0xC0) != 0x80){ ok = false; break; }
            cp = (cp << 6) | (d & 0x3F);
        }
        bool overlong = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
        if(!ok || overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)){ o += '?'; i++; continue; }
        if(blocked(cp)) o += '?';
        else o.append(in, i, len);
        i += len;
    }
    return o;
}

}

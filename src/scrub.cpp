#include "scrub.h"
#include <algorithm>
#include <cstring>

namespace sc {

static uint32_t rd32be(const uint8_t* p){
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static uint32_t rd32le(const uint8_t* p){
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | (uint32_t)p[0];
}

static void put32le(std::vector<uint8_t>& o, uint32_t v){
    for(int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> (8 * i)));
}

static uint32_t crc32Of(const uint8_t* p, size_t n){
    static const std::vector<uint32_t> table = []{
        std::vector<uint32_t> t(256);
        for(uint32_t i = 0; i < 256; i++){
            uint32_t c = i;
            for(int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for(size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void note(ScrubReport& rep, const std::string& what){
    if(std::find(rep.removed.begin(), rep.removed.end(), what) == rep.removed.end()) rep.removed.push_back(what);
}

static bool bad(std::string& err, const char* what){
    err = what;
    return false;
}

static bool exifInfo(const uint8_t* seg, size_t n, int& orientation, bool& gps){
    if(n < 6 || std::memcmp(seg, "Exif\0\0", 6) != 0) return false;
    const uint8_t* t = seg + 6;
    uint64_t tn = n - 6;
    if(tn < 8) return true;
    bool le;
    if(t[0] == 'I' && t[1] == 'I') le = true;
    else if(t[0] == 'M' && t[1] == 'M') le = false;
    else return true;
    auto r16 = [&](uint64_t o) -> uint32_t { return le ? (uint32_t)t[o] | (uint32_t)t[o + 1] << 8 : (uint32_t)t[o] << 8 | (uint32_t)t[o + 1]; };
    auto r32 = [&](uint64_t o) -> uint32_t { return le ? rd32le(t + o) : rd32be(t + o); };
    if(r16(2) != 42) return true;
    uint64_t ifd = r32(4);
    if(ifd + 2 > tn) return true;
    uint64_t count = r16(ifd);
    for(uint64_t k = 0; k < count; k++){
        uint64_t e = ifd + 2 + k * 12;
        if(e + 12 > tn) break;
        uint32_t tag = r16(e);
        if(tag == 0x0112 && r16(e + 2) == 3 && r32(e + 4) == 1) orientation = (int)r16(e + 8);
        if(tag == 0x8825) gps = true;
    }
    return true;
}

static void orientationSegment(std::vector<uint8_t>& out, size_t at, int orientation){
    const uint8_t seg[] = {
        0xFF, 0xE1, 0x00, 0x22,
        'E', 'x', 'i', 'f', 0, 0,
        'M', 'M', 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x01,
        0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, (uint8_t)orientation, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00};
    out.insert(out.begin() + (std::ptrdiff_t)at, seg, seg + sizeof seg);
}

static bool scrubJpeg(const uint8_t* p, size_t n, std::vector<uint8_t>& out, ScrubReport& rep, std::string& err){
    if(n < 4 || p[0] != 0xFF || p[1] != 0xD8) return bad(err, "not a JPEG file");
    out.clear();
    out.reserve(n);
    out.push_back(0xFF);
    out.push_back(0xD8);
    size_t insertAt = 2;
    size_t i = 2;
    bool sof = false, sos = false, eoi = false;
    int orientation = 0;
    while(i < n){
        if(p[i] != 0xFF) return bad(err, "damaged JPEG: a marker was expected");
        while(i < n && p[i] == 0xFF) i++;
        if(i >= n) break;
        uint8_t m = p[i++];
        if(m == 0xD9){ eoi = true; break; }
        if(m == 0x01) continue;
        if(m < 0xC0 || (m >= 0xD0 && m <= 0xD8)) return bad(err, "damaged JPEG: unexpected marker");
        if(i + 2 > n) return bad(err, "damaged JPEG: cut short");
        size_t len = (size_t)p[i] << 8 | p[i + 1];
        if(len < 2 || i + len > n) return bad(err, "damaged JPEG: a segment is cut short");
        const uint8_t* seg = p + i + 2;
        size_t sl = len - 2;
        if(m == 0xE0 && sl >= 14 && std::memcmp(seg, "JFIF\0", 5) == 0){
            out.push_back(0xFF);
            out.push_back(0xE0);
            out.push_back(0x00);
            out.push_back(0x10);
            out.insert(out.end(), seg, seg + 12);
            out.push_back(0x00);
            out.push_back(0x00);
            if(out.size() == 20) insertAt = out.size();
            if(sl > 14 || seg[12] != 0 || seg[13] != 0) note(rep, "thumbnail");
            i += len;
            continue;
        }
        if(m == 0xEE && sl == 12 && std::memcmp(seg, "Adobe", 5) == 0){
            out.insert(out.end(), p + i - 2, p + i + len);
            i += len;
            continue;
        }
        if(m >= 0xE0){
            if(m == 0xE1){
                int o = 0;
                bool g = false;
                if(exifInfo(seg, sl, o, g)){
                    bool ours = false;
                    if(o >= 2 && o <= 8 && !g && len == 34){
                        std::vector<uint8_t> t;
                        orientationSegment(t, 0, o);
                        ours = std::memcmp(t.data(), p + i - 2, t.size()) == 0;
                    }
                    if(!ours) note(rep, "EXIF");
                    if(o >= 1 && o <= 8) orientation = o;
                    if(g){ rep.gps = true; note(rep, "GPS location"); }
                }
                else if(sl >= 28 && std::memcmp(seg, "http://ns.adobe.com/xap/1.0/", 28) == 0) note(rep, "XMP");
                else note(rep, "other metadata");
            }
            else if(m == 0xE2 && sl >= 12 && std::memcmp(seg, "ICC_PROFILE\0", 12) == 0) note(rep, "colour profile");
            else if(m == 0xFE) note(rep, "comment");
            else note(rep, "other metadata");
            i += len;
            continue;
        }
        if(m == 0xDA){
            out.insert(out.end(), p + i - 2, p + i + len);
            size_t j = i + len;
            size_t start = j;
            while(j < n){
                if(p[j] == 0xFF){
                    if(j + 1 >= n) return bad(err, "damaged JPEG: cut short");
                    uint8_t nx = p[j + 1];
                    if(nx == 0x00 || (nx >= 0xD0 && nx <= 0xD7)){ j += 2; continue; }
                    break;
                }
                j++;
            }
            out.insert(out.end(), p + start, p + j);
            i = j;
            sos = true;
            continue;
        }
        if((m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) sof = true;
        out.insert(out.end(), p + i - 2, p + i + len);
        i += len;
    }
    if(!eoi || !sof || !sos) return bad(err, "damaged JPEG: the picture data is incomplete");
    if(i < n) note(rep, "data after the end of the picture");
    out.push_back(0xFF);
    out.push_back(0xD9);
    if(orientation >= 2){
        orientationSegment(out, insertAt, orientation);
        rep.orientationKept = orientation;
    }
    return true;
}

static const char* pngKeep[] = {"IHDR", "PLTE", "IDAT", "IEND", "tRNS", "gAMA", "cHRM", "sRGB", "sBIT", "cICP", "acTL", "fcTL", "fdAT"};

static std::string pngName(const uint8_t* t){
    if(!std::memcmp(t, "eXIf", 4)) return "EXIF";
    if(!std::memcmp(t, "tEXt", 4) || !std::memcmp(t, "zTXt", 4) || !std::memcmp(t, "iTXt", 4)) return "text";
    if(!std::memcmp(t, "tIME", 4)) return "timestamp";
    if(!std::memcmp(t, "iCCP", 4)) return "colour profile";
    if(!std::memcmp(t, "pHYs", 4)) return "physical size";
    return "other metadata";
}

static bool scrubPng(const uint8_t* p, size_t n, std::vector<uint8_t>& out, ScrubReport& rep, std::string& err){
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if(n < 8 + 12 || std::memcmp(p, sig, 8) != 0) return bad(err, "not a PNG file");
    out.assign(p, p + 8);
    out.reserve(n);
    size_t i = 8;
    bool ihdr = false, idat = false, iend = false;
    while(i + 12 <= n){
        uint32_t len = rd32be(p + i);
        if(len > 0x7FFFFFFFu || (uint64_t)i + 12 + len > n) return bad(err, "damaged PNG: a chunk is cut short");
        const uint8_t* type = p + i + 4;
        for(int k = 0; k < 4; k++){
            bool letter = (type[k] >= 'A' && type[k] <= 'Z') || (type[k] >= 'a' && type[k] <= 'z');
            if(!letter) return bad(err, "damaged PNG: bad chunk name");
        }
        if(!ihdr && std::memcmp(type, "IHDR", 4) != 0) return bad(err, "damaged PNG: the first chunk must be IHDR");
        bool keep = false;
        for(const char* k : pngKeep) if(std::memcmp(type, k, 4) == 0) keep = true;
        if(keep){
            if(crc32Of(type, (size_t)len + 4) != rd32be(p + i + 8 + len)) return bad(err, "damaged PNG: a chunk failed its check");
            out.insert(out.end(), p + i, p + i + 12 + len);
        }
        else note(rep, pngName(type));
        if(std::memcmp(type, "IHDR", 4) == 0){
            if(len != 13 || ihdr) return bad(err, "damaged PNG: bad header chunk");
            ihdr = true;
        }
        if(std::memcmp(type, "IDAT", 4) == 0) idat = true;
        if(std::memcmp(type, "IEND", 4) == 0 && len != 0) return bad(err, "damaged PNG: bad end chunk");
        i += 12 + len;
        if(std::memcmp(type, "IEND", 4) == 0){ iend = true; break; }
    }
    if(!iend || !idat) return bad(err, "damaged PNG: the picture data is incomplete");
    if(i < n) note(rep, "data after the end of the picture");
    return true;
}

static bool skipSubBlocks(const uint8_t* p, size_t n, size_t& i){
    for(;;){
        if(i >= n) return false;
        size_t sz = p[i++];
        if(sz == 0) return true;
        if(i + sz > n) return false;
        i += sz;
    }
}

static bool scrubGif(const uint8_t* p, size_t n, std::vector<uint8_t>& out, ScrubReport& rep, std::string& err){
    if(n < 14 || (std::memcmp(p, "GIF87a", 6) != 0 && std::memcmp(p, "GIF89a", 6) != 0)) return bad(err, "not a GIF file");
    uint8_t flags = p[10];
    size_t i = 13 + ((flags & 0x80) ? ((size_t)3 << ((flags & 7) + 1)) : 0);
    if(i > n) return bad(err, "damaged GIF: cut short");
    out.assign(p, p + i);
    out.reserve(n);
    bool trailer = false;
    int frames = 0;
    while(i < n){
        uint8_t b = p[i];
        if(b == 0x3B){ out.push_back(0x3B); i++; trailer = true; break; }
        if(b == 0x21){
            if(i + 2 > n) return bad(err, "damaged GIF: cut short");
            uint8_t label = p[i + 1];
            size_t j = i + 2;
            if(!skipSubBlocks(p, n, j)) return bad(err, "damaged GIF: a block is cut short");
            bool keep = label == 0xF9;
            if(label == 0xFF && j - i == 19 && p[i + 2] == 11 && std::memcmp(p + i + 3, "NETSCAPE2.0", 11) == 0 && p[i + 14] == 3 && p[i + 15] == 1) keep = true;
            if(keep) out.insert(out.end(), p + i, p + j);
            else if(label == 0xFE) note(rep, "comment");
            else if(label == 0x01) note(rep, "text overlay");
            else note(rep, "other metadata");
            i = j;
            continue;
        }
        if(b == 0x2C){
            if(i + 10 > n) return bad(err, "damaged GIF: cut short");
            uint8_t f = p[i + 9];
            size_t j = i + 10 + ((f & 0x80) ? ((size_t)3 << ((f & 7) + 1)) : 0);
            if(j + 1 > n) return bad(err, "damaged GIF: cut short");
            j++;
            if(!skipSubBlocks(p, n, j)) return bad(err, "damaged GIF: picture data is cut short");
            out.insert(out.end(), p + i, p + j);
            frames++;
            i = j;
            continue;
        }
        return bad(err, "damaged GIF: unexpected data");
    }
    if(!trailer || frames == 0) return bad(err, "damaged GIF: the picture data is incomplete");
    if(i < n) note(rep, "data after the end of the picture");
    return true;
}

static bool webpFrameKeep(const uint8_t* t){
    return !std::memcmp(t, "VP8 ", 4) || !std::memcmp(t, "VP8L", 4) || !std::memcmp(t, "ALPH", 4);
}

static bool webpChunks(const uint8_t* p, size_t start, size_t end, bool inFrame, std::vector<uint8_t>& out, ScrubReport& rep, bool& image, std::string& err){
    size_t i = start;
    while(i < end){
        if(i + 8 > end) return bad(err, "damaged WebP: a chunk is cut short");
        const uint8_t* t = p + i;
        uint32_t sz = rd32le(t + 4);
        uint64_t padded = (uint64_t)sz + (sz & 1);
        if((uint64_t)i + 8 + padded > end) return bad(err, "damaged WebP: a chunk is cut short");
        const uint8_t* d = t + 8;
        bool isFrame = !std::memcmp(t, "ANMF", 4);
        bool keep = webpFrameKeep(t) || (!inFrame && (!std::memcmp(t, "VP8X", 4) || !std::memcmp(t, "ANIM", 4) || isFrame));
        if(!keep){
            if(!std::memcmp(t, "EXIF", 4)) note(rep, "EXIF");
            else if(!std::memcmp(t, "XMP ", 4)) note(rep, "XMP");
            else if(!std::memcmp(t, "ICCP", 4)) note(rep, "colour profile");
            else note(rep, "other metadata");
        }
        else if(isFrame){
            if(sz < 16) return bad(err, "damaged WebP: bad animation frame");
            std::vector<uint8_t> inner(d, d + 16);
            if(!webpChunks(p, (size_t)(d - p) + 16, (size_t)(d - p) + sz, true, inner, rep, image, err)) return false;
            out.insert(out.end(), t, t + 4);
            put32le(out, (uint32_t)inner.size());
            out.insert(out.end(), inner.begin(), inner.end());
            if(inner.size() & 1) out.push_back(0);
        }
        else {
            out.insert(out.end(), t, t + 4);
            put32le(out, sz);
            size_t at = out.size();
            out.insert(out.end(), d, d + sz);
            if(!std::memcmp(t, "VP8X", 4)){
                if(sz < 10) return bad(err, "damaged WebP: bad header chunk");
                out[at] = (uint8_t)(out[at] & ~(0x20 | 0x08 | 0x04));
            }
            if(sz & 1) out.push_back(0);
            if(webpFrameKeep(t) && std::memcmp(t, "ALPH", 4) != 0) image = true;
        }
        i += 8 + (size_t)padded;
    }
    return true;
}

static bool scrubWebp(const uint8_t* p, size_t n, std::vector<uint8_t>& out, ScrubReport& rep, std::string& err){
    if(n < 20 || std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WEBP", 4) != 0) return bad(err, "not a WebP file");
    uint32_t riff = rd32le(p + 4);
    if(riff < 12 || (uint64_t)riff + 8 > n) return bad(err, "damaged WebP: the file is cut short");
    size_t end = (size_t)riff + 8;
    std::vector<uint8_t> body;
    bool image = false;
    if(!webpChunks(p, 12, end, false, body, rep, image, err)) return false;
    if(!image) return bad(err, "damaged WebP: no picture data");
    if(end < n) note(rep, "data after the end of the picture");
    out.clear();
    out.reserve(body.size() + 12);
    out.insert(out.end(), p, p + 4);
    put32le(out, (uint32_t)(4 + body.size()));
    out.insert(out.end(), p + 8, p + 12);
    out.insert(out.end(), body.begin(), body.end());
    return true;
}

FileKind sniffKind(const uint8_t* p, size_t n){
    if(n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return FileKind::Jpeg;
    if(n >= 8 && std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0) return FileKind::Png;
    if(n >= 6 && (std::memcmp(p, "GIF87a", 6) == 0 || std::memcmp(p, "GIF89a", 6) == 0)) return FileKind::Gif;
    if(n >= 12 && std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "WEBP", 4) == 0) return FileKind::Webp;
    return FileKind::Unknown;
}

const char* kindName(FileKind k){
    switch(k){
        case FileKind::Jpeg: return "JPEG";
        case FileKind::Png: return "PNG";
        case FileKind::Gif: return "GIF";
        case FileKind::Webp: return "WebP";
        default: return "unknown";
    }
}

bool scrubFile(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, ScrubReport& rep, std::string& err){
    rep = ScrubReport();
    out.clear();
    rep.inBytes = in.size();
    if(in.size() > kMaxScrubBytes){ err = "the file is larger than 64 MB"; return false; }
    rep.kind = sniffKind(in.data(), in.size());
    bool ok = false;
    switch(rep.kind){
        case FileKind::Jpeg: ok = scrubJpeg(in.data(), in.size(), out, rep, err); break;
        case FileKind::Png: ok = scrubPng(in.data(), in.size(), out, rep, err); break;
        case FileKind::Gif: ok = scrubGif(in.data(), in.size(), out, rep, err); break;
        case FileKind::Webp: ok = scrubWebp(in.data(), in.size(), out, rep, err); break;
        default: err = "StarCom cannot scrub this kind of file yet, so it will not be sent (it handles JPEG, PNG, GIF and WebP pictures)"; return false;
    }
    if(!ok){ out.clear(); return false; }
    rep.outBytes = out.size();
    return true;
}

}

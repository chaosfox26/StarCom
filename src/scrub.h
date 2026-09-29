#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sc {

constexpr size_t kMaxScrubBytes = 64u * 1024u * 1024u;

enum class FileKind { Unknown, Jpeg, Png, Gif, Webp };

struct ScrubReport {
    FileKind kind = FileKind::Unknown;
    size_t inBytes = 0;
    size_t outBytes = 0;
    bool gps = false;
    int orientationKept = 0;
    std::vector<std::string> removed;
};

FileKind sniffKind(const uint8_t* p, size_t n);
const char* kindName(FileKind k);
bool scrubFile(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, ScrubReport& rep, std::string& err);

}

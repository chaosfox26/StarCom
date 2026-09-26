#pragma once
#include <functional>
#include <string>

namespace sc {

using CaptureFn = std::function<void(const float* frame)>;
using RenderFn = std::function<void(float* frame)>;

class AudioIO {
public:
    AudioIO();
    ~AudioIO();
    AudioIO(const AudioIO&) = delete;
    AudioIO& operator=(const AudioIO&) = delete;
    bool start(CaptureFn onFrame, RenderFn fillFrame, std::string& err);
    void stop();
    void setMuted(bool m);
    bool muted() const;
    struct Impl;
private:
    Impl* p_;
};

}

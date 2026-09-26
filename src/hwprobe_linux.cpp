#ifndef _WIN32
#include "hwprobe.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace sc {

namespace {

typedef void* VaDisplay;
typedef int VaStatus;
typedef VaDisplay (*GetDisplayDrmFn)(int);
typedef VaStatus (*InitializeFn)(VaDisplay, int*, int*);
typedef VaStatus (*TerminateFn)(VaDisplay);
typedef int (*MaxNumFn)(VaDisplay);
typedef VaStatus (*QueryProfilesFn)(VaDisplay, int*, int*);
typedef VaStatus (*QueryEntrypointsFn)(VaDisplay, int, int*, int*);
typedef const char* (*VendorFn)(VaDisplay);

const int kH264Main = 6, kH264High = 7, kH264Baseline = 13;
const int kHevcMain = 17, kHevcMain10 = 18;
const int kAv1Profile0 = 32, kAv1Profile1 = 33;
const int kEncSlice = 6, kEncSliceLp = 8;

struct Va {
    void* lib = nullptr;
    void* drm = nullptr;
    GetDisplayDrmFn getDisplay = nullptr;
    InitializeFn initialize = nullptr;
    TerminateFn terminate = nullptr;
    MaxNumFn maxProfiles = nullptr;
    MaxNumFn maxEntrypoints = nullptr;
    QueryProfilesFn profiles = nullptr;
    QueryEntrypointsFn entrypoints = nullptr;
    VendorFn vendor = nullptr;
    bool load(){
        lib = dlopen("libva.so.2", RTLD_NOW | RTLD_LOCAL);
        drm = dlopen("libva-drm.so.2", RTLD_NOW | RTLD_LOCAL);
        if(!lib || !drm) return false;
        getDisplay = (GetDisplayDrmFn)dlsym(drm, "vaGetDisplayDRM");
        initialize = (InitializeFn)dlsym(lib, "vaInitialize");
        terminate = (TerminateFn)dlsym(lib, "vaTerminate");
        maxProfiles = (MaxNumFn)dlsym(lib, "vaMaxNumProfiles");
        maxEntrypoints = (MaxNumFn)dlsym(lib, "vaMaxNumEntrypoints");
        profiles = (QueryProfilesFn)dlsym(lib, "vaQueryConfigProfiles");
        entrypoints = (QueryEntrypointsFn)dlsym(lib, "vaQueryConfigEntrypoints");
        vendor = (VendorFn)dlsym(lib, "vaQueryVendorString");
        return getDisplay && initialize && terminate && maxProfiles && maxEntrypoints && profiles && entrypoints;
    }
    ~Va(){
        if(drm) dlclose(drm);
        if(lib) dlclose(lib);
    }
};

bool canEncode(Va& va, VaDisplay d, int profile, int maxEp){
    std::vector<int> eps((size_t)(maxEp > 0 ? maxEp : 1));
    int n = 0;
    if(va.entrypoints(d, profile, eps.data(), &n) != 0) return false;
    for(int i = 0; i < n && i < (int)eps.size(); i++) if(eps[i] == kEncSlice || eps[i] == kEncSliceLp) return true;
    return false;
}

void addEngine(CodecCaps& c, const std::string& name){
    c.hw = true;
    for(const std::string& e : c.engines) if(e == name) return;
    c.engines.push_back(name);
}

}

HwCaps probeHardware(){
    HwCaps caps;
    caps.api = "VA-API";
    Va va;
    if(!va.load()){
        caps.note = "libva not installed; no hardware encoders detected";
        return caps;
    }
    int devices = 0;
    for(int i = 128; i < 136; i++){
        std::string path = "/dev/dri/renderD" + std::to_string(i);
        int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
        if(fd < 0) continue;
        devices++;
        VaDisplay d = va.getDisplay(fd);
        int major = 0, minor = 0;
        if(d && va.initialize(d, &major, &minor) == 0){
            std::string name = va.vendor ? va.vendor(d) : "";
            if(name.empty()) name = path;
            int maxP = va.maxProfiles(d), maxE = va.maxEntrypoints(d);
            std::vector<int> profs((size_t)(maxP > 0 ? maxP : 1));
            int n = 0;
            if(va.profiles(d, profs.data(), &n) == 0){
                for(int k = 0; k < n && k < (int)profs.size(); k++){
                    int p = profs[k];
                    if(!canEncode(va, d, p, maxE)) continue;
                    if(p == kAv1Profile0 || p == kAv1Profile1) addEngine(caps.av1, name);
                    else if(p == kHevcMain || p == kHevcMain10) addEngine(caps.hevc, name);
                    else if(p == kH264Main || p == kH264High || p == kH264Baseline) addEngine(caps.h264, name);
                }
            }
            va.terminate(d);
        }
        close(fd);
    }
    if(!devices) caps.note = "no GPU render device found; no hardware encoders detected";
    else if(!caps.av1.hw && !caps.hevc.hw && !caps.h264.hw) caps.note = "GPU found but it offers no AV1/HEVC/H.264 hardware encoding";
    return caps;
}

}
#endif

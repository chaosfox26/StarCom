#ifdef _WIN32
#include "hwprobe.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>

namespace sc {

static const GUID SC_MFVideoFormat_AV1 = { 0x31305641, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

static std::string narrow(const wchar_t* w){
    if(!w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if(n <= 1) return std::string();
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

static void enumEncoders(const GUID& subtype, UINT32 flags, CodecCaps& out, bool hardware){
    MFT_REGISTER_TYPE_INFO outType = { MFMediaType_Video, subtype };
    IMFActivate** acts = nullptr;
    UINT32 count = 0;
    if(FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, nullptr, &outType, &acts, &count))) return;
    for(UINT32 i = 0; i < count; i++){
        WCHAR* name = nullptr;
        UINT32 len = 0;
        std::string label = hardware ? "hardware" : "software";
        if(SUCCEEDED(acts[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &len))){
            label = narrow(name);
            CoTaskMemFree(name);
        }
        if(hardware){
            out.hw = true;
            bool seen = false;
            for(const std::string& e : out.engines) if(e == label) seen = true;
            if(!seen) out.engines.push_back(label);
        }
        else { out.sw = true; }
        acts[i]->Release();
    }
    CoTaskMemFree(acts);
}

HwCaps probeHardware(){
    HwCaps c;
    c.api = "Windows Media Foundation";
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if(FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))){
        c.note = "Media Foundation is not available on this system";
        if(SUCCEEDED(co)) CoUninitialize();
        return c;
    }
    const UINT32 hwFlags = MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER;
    const UINT32 swFlags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER;
    enumEncoders(SC_MFVideoFormat_AV1, hwFlags, c.av1, true);
    enumEncoders(MFVideoFormat_HEVC, hwFlags, c.hevc, true);
    enumEncoders(MFVideoFormat_H264, hwFlags, c.h264, true);
    enumEncoders(SC_MFVideoFormat_AV1, swFlags, c.av1, false);
    enumEncoders(MFVideoFormat_HEVC, swFlags, c.hevc, false);
    enumEncoders(MFVideoFormat_H264, swFlags, c.h264, false);
    MFShutdown();
    if(SUCCEEDED(co)) CoUninitialize();
    return c;
}

}
#endif

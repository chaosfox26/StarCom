#include "paths.h"
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#else
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sc {

#ifdef _WIN32
static std::string narrow(const std::wstring& w){
    if(w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool enterDataDir(std::string& where, std::string& err){
    std::wstring dir;
    wchar_t buf[32768];
    DWORD n = GetEnvironmentVariableW(L"STARCOM_HOME", buf, 32768);
    if(n > 0 && n < 32768) dir.assign(buf, n);
    else {
        PWSTR app = nullptr;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &app))){ err = "cannot find the AppData folder"; return false; }
        dir = std::wstring(app) + L"\\StarCom";
        CoTaskMemFree(app);
    }
    int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if(rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS){ err = "cannot create the StarCom data folder"; return false; }
    if(!SetCurrentDirectoryW(dir.c_str())){ err = "cannot open the StarCom data folder"; return false; }
    where = narrow(dir);
    return true;
}
#else
bool enterDataDir(std::string& where, std::string& err){
    std::string dir;
    const char* h = std::getenv("STARCOM_HOME");
    if(h && *h) dir = h;
    else {
        const char* x = std::getenv("XDG_DATA_HOME");
        const char* home = std::getenv("HOME");
        if(x && *x) dir = std::string(x) + "/starcom";
        else if(home && *home) dir = std::string(home) + "/.local/share/starcom";
        else { err = "cannot find a home folder"; return false; }
    }
    std::string part;
    for(size_t i = 0; i <= dir.size(); i++){
        if(i == dir.size() || (dir[i] == '/' && i > 0)){
            part = dir.substr(0, i);
            if(::mkdir(part.c_str(), 0700) != 0 && errno != EEXIST){ err = "cannot create " + part; return false; }
        }
    }
    if(::chdir(dir.c_str()) != 0){ err = "cannot open " + dir; return false; }
    where = dir;
    return true;
}
#endif

}

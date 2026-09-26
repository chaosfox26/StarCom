#include "console.h"
#include <cstdio>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace sc {

void consoleInit(){
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void consolePrint(const std::string& utf8){
    std::fwrite(utf8.data(), 1, utf8.size(), stdout);
    std::fflush(stdout);
}

#ifdef _WIN32
static bool readConsoleLine(std::string& out){
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if(!GetConsoleMode(h, &mode)){
        if(!std::getline(std::cin, out)) return false;
        if(!out.empty() && out.back() == '\r') out.pop_back();
        return true;
    }
    std::wstring w;
    wchar_t buf[512];
    for(;;){
        DWORD got = 0;
        if(!ReadConsoleW(h, buf, 512, &got, nullptr) || got == 0) return false;
        w.append(buf, got);
        if(!w.empty() && w.back() == L'\n') break;
    }
    while(!w.empty() && (w.back() == L'\n' || w.back() == L'\r')) w.pop_back();
    out.clear();
    if(w.empty()) return true;
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    out.resize((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], n, nullptr, nullptr);
    return true;
}
#else
static bool readConsoleLine(std::string& out){
    if(!std::getline(std::cin, out)) return false;
    if(!out.empty() && out.back() == '\r') out.pop_back();
    return true;
}
#endif

LineInput::LineInput() : eof_(false) {}

LineInput::~LineInput(){
    if(t_.joinable()) t_.detach();
}

void LineInput::start(){
    t_ = std::thread([this]{ run(); });
}

void LineInput::run(){
    std::string line;
    while(readConsoleLine(line)){
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(line);
    }
    eof_ = true;
}

bool LineInput::next(std::string& line){
    std::lock_guard<std::mutex> lk(mu_);
    if(q_.empty()) return false;
    line = q_.front();
    q_.pop_front();
    return true;
}

bool LineInput::closed() const {
    return eof_;
}

}

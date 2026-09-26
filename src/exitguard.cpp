#include "exitguard.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <pthread.h>
#include <thread>
#include <unistd.h>
#endif

namespace sc {

static std::function<void()>* g_exitFn = nullptr;

#ifdef _WIN32
static BOOL WINAPI onConsoleEvent(DWORD type){
    if(type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT){
        if(g_exitFn) (*g_exitFn)();
    }
    return FALSE;
}

void installExitHook(std::function<void()> fn){
    static std::function<void()> keep;
    keep = std::move(fn);
    g_exitFn = &keep;
    SetConsoleCtrlHandler(nullptr, FALSE);
    SetConsoleCtrlHandler(onConsoleEvent, TRUE);
}
#else
void installExitHook(std::function<void()> fn){
    static std::function<void()> keep;
    keep = std::move(fn);
    g_exitFn = &keep;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
    std::thread([set]{
        int sig = 0;
        if(sigwait(&set, &sig) == 0){
            if(g_exitFn) (*g_exitFn)();
            _exit(128 + sig);
        }
    }).detach();
}
#endif

}

#pragma once
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace sc {

void consoleInit();
void consolePrint(const std::string& utf8);

class LineInput {
public:
    LineInput();
    ~LineInput();
    LineInput(const LineInput&) = delete;
    LineInput& operator=(const LineInput&) = delete;
    void start();
    bool next(std::string& line);
    bool closed() const;
private:
    void run();
    std::thread t_;
    std::mutex mu_;
    std::deque<std::string> q_;
    std::atomic<bool> eof_;
};

}

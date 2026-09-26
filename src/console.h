#pragma once
#include <atomic>
#include <deque>
#include <memory>
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
    struct State {
        std::mutex mu;
        std::deque<std::string> q;
        std::atomic<bool> eof{false};
    };
    std::shared_ptr<State> st_;
    std::thread t_;
};

}

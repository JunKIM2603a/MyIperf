#pragma once

#include "nlohmann/json.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

class ResultEventSink {
public:
    ResultEventSink();
    ~ResultEventSink();

    void start(const std::string& pipeName);
    void publish(const nlohmann::json& event);
    void stop();
    bool enabled() const;

private:
    void workerLoop();
    bool writeLine(const std::string& line);

#ifdef _WIN32
    bool ensurePipeConnected();
    void closePipe();
    HANDLE pipeHandle;
    bool pipeConnected;
#endif

    std::string pipeName;
    std::atomic<bool> running;
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> queue;
    bool warningLogged;
};

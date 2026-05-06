#include "ResultEventSink.h"

#include "myiperf/Logger.h"

#include <chrono>
#include <filesystem>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
std::string normalizePipeName(const std::string& pipeName) {
    if (pipeName.rfind("\\\\.\\pipe\\", 0) == 0) {
        return pipeName;
    }
    return "\\\\.\\pipe\\" + pipeName;
}
#endif

} // namespace

ResultEventSink::ResultEventSink()
#ifdef _WIN32
    : pipeHandle(INVALID_HANDLE_VALUE), pipeConnected(false),
      running(false), warningLogged(false) {}
#else
    : running(false), warningLogged(false) {}
#endif

ResultEventSink::~ResultEventSink() {
    stop();
}

void ResultEventSink::start(const std::string& name) {
    stop();
    if (name.empty()) {
        return;
    }

#ifdef _WIN32
    pipeName = normalizePipeName(name);
#else
    pipeName = name;
#endif

    warningLogged = false;
    running.store(true, std::memory_order_release);
    worker = std::thread(&ResultEventSink::workerLoop, this);
}

void ResultEventSink::publish(const nlohmann::json& event) {
    if (!running.load(std::memory_order_acquire)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push_back(event.dump() + "\n");
    }
    cv.notify_one();
}

void ResultEventSink::stop() {
    if (!running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    cv.notify_all();
    if (worker.joinable()) {
        worker.join();
    }

#ifdef _WIN32
    closePipe();
#endif

    std::lock_guard<std::mutex> lock(mutex);
    queue.clear();
}

bool ResultEventSink::enabled() const {
    return running.load(std::memory_order_acquire);
}

void ResultEventSink::workerLoop() {
    while (true) {
        std::string line;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] {
                return !queue.empty() || !running.load(std::memory_order_acquire);
            });
            if (queue.empty()) {
                if (!running.load(std::memory_order_acquire)) {
                    break;
                }
                continue;
            }
            line = std::move(queue.front());
            queue.pop_front();
        }

        if (!writeLine(line)) {
            if (!warningLogged) {
                warningLogged = true;
                Logger::log("Warning: Result pipe is not connected. Result events will be best-effort only.");
            }
            if (running.load(std::memory_order_acquire)) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    queue.push_front(std::move(line));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

#ifdef _WIN32
bool ResultEventSink::ensurePipeConnected() {
    if (pipeConnected) {
        return true;
    }

    if (pipeHandle == INVALID_HANDLE_VALUE) {
        pipeHandle = CreateNamedPipeA(
            pipeName.c_str(),
            PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
            1,
            64 * 1024,
            64 * 1024,
            0,
            NULL);
        if (pipeHandle == INVALID_HANDLE_VALUE) {
            return false;
        }
    }

    BOOL connected = ConnectNamedPipe(pipeHandle, NULL);
    if (connected || GetLastError() == ERROR_PIPE_CONNECTED) {
        pipeConnected = true;
        DWORD mode = PIPE_WAIT;
        SetNamedPipeHandleState(pipeHandle, &mode, NULL, NULL);
        return true;
    }

    DWORD error = GetLastError();
    if (error == ERROR_PIPE_LISTENING || error == ERROR_NO_DATA) {
        return false;
    }

    closePipe();
    return false;
}

void ResultEventSink::closePipe() {
    if (pipeHandle != INVALID_HANDLE_VALUE) {
        if (pipeConnected) {
            DisconnectNamedPipe(pipeHandle);
        }
        CloseHandle(pipeHandle);
        pipeHandle = INVALID_HANDLE_VALUE;
    }
    pipeConnected = false;
}

bool ResultEventSink::writeLine(const std::string& line) {
    if (!ensurePipeConnected()) {
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(pipeHandle, line.data(), static_cast<DWORD>(line.size()), &written, NULL);
    if (!ok || written != line.size()) {
        closePipe();
        return false;
    }
    FlushFileBuffers(pipeHandle);
    return true;
}
#else
bool ResultEventSink::writeLine(const std::string& line) {
    if (!std::filesystem::exists(pipeName)) {
        if (mkfifo(pipeName.c_str(), 0666) == -1 && errno != EEXIST) {
            return false;
        }
    }

    int fd = open(pipeName.c_str(), O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        return false;
    }

    ssize_t written = write(fd, line.data(), line.size());
    close(fd);
    return written == static_cast<ssize_t>(line.size());
}
#endif

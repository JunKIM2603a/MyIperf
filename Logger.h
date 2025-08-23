#pragma once

#include <string>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <atomic>
#include <fstream>
#include <cstdarg>
#include <vector>
#include <filesystem>
#include "Protocol.h"
#include "Config.h"

#ifdef _WIN32
// Define HANDLE to be void* to avoid including <windows.h> in a header file.
typedef void *HANDLE;
#endif

#include <cstdlib>  // std::abort
#include <sstream>  // std::ostringstream

// wait function for debug step
void DebugPause(const std::string& message = "Press Enter to continue...");

// Helper function to format strings like printf
inline std::string string_format(const char* fmt, ...) {
    int size = 1024;
    std::vector<char> buffer(size);
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(buffer.data(), size, fmt, args);
    va_end(args);
    if (needed >= 0 && needed < size) {
        return std::string(buffer.data());
    }
    
    size = needed > 0 ? needed + 1 : size * 2;
    buffer.resize(size);
    va_start(args, fmt);
    vsnprintf(buffer.data(), size, fmt, args);
    va_end(args);
    return std::string(buffer.data());
}

// function for Log
inline void logError(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
}

// custom Assert function
inline void assertLog(bool condition, const char* conditionStr, const char* file, int line, const char* func, const std::string& msg) {
    if (!condition) {
        std::ostringstream oss;
        oss << "Assertion failed!\n"
            << "  Condition: " << conditionStr << "\n"
            << "  Message: "   << msg << "\n"
            << "  File: "      << file << "\n"
            << "  Line: "      << line << "\n"
            << "  Function: "  << func << "\n";
        std::cerr << oss.str(); // Print to stderr for debugging
        logError(oss.str());
        std::abort();
    }
}

// New ASSERT_LOG macro that calls the assertLog function
#define ASSERT_LOG(cond, fmt, ...) \
    assertLog(cond, #cond, __FILE__, __LINE__, __func__, string_format(fmt, ##__VA_ARGS__))


/**
 * @class Logger
 * @brief A thread-safe, asynchronous logging utility.
 *
 * This static class provides a simple logging framework that queues messages
 * from multiple threads and writes them to the console, a file, and a named pipe.
 */
class Logger {
public:
    static void start(const Config& config);
    static void stop();
    static void log(const std::string& message);
    static void writeStats(double throughputMbps, double duration, long long totalBytes);
    static void writeFinalReport(const std::string& role,
                                 const TestStats& localStats,
                                 const TestStats& remoteStats);

private:
    static void logWorker();
    static void manageLogRotation(const std::string& mode);

    // Logging framework members
    static std::mutex queueMutex;
    static std::condition_variable cv;
    static std::deque<std::string> messageQueue;
    static std::thread workerThread;
    static std::atomic<bool> running;
    static const std::string getTimeNow();

    // File logging members
    static std::ofstream logStream;
    static std::atomic<bool> saveToFile;
    static const std::string logDirectory;

#ifdef _WIN32
    // Named pipe logging members
    static void pipeWorker();
    static std::thread pipeThread;
    static HANDLE hPipe;
    static std::atomic<bool> pipeConnected;
    static std::string pipeName;
#endif
};

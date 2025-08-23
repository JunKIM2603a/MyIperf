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

// 로그용 함수
inline void logError(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
}

// 커스텀 Assert 함수
inline void assertLog(bool condition, const char* conditionStr, const char* file, int line, const char* func, const std::string& msg) {
    if (!condition) {
        std::ostringstream oss;
        oss << "Assertion failed!\n"
            << "  Condition: " << conditionStr << "\n"
            << "  Message: "   << msg << "\n"
            << "  File: "      << file << "\n"
            << "  Line: "      << line << "\n"
            << "  Function: "  << func << "\n";
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
 * from multiple threads and writes them to the console via a dedicated worker thread.
 * This prevents log messages from different threads from interleaving and avoids
 * blocking application threads for I/O operations.
 */
class Logger {
public:
    /**
     * @brief Starts the logger service.
     * This must be called before any logging is done.
     * @param enableFileLogging If true, logs will be saved to a file.
     * @param mode The mode of operation (e.g., "CLIENT" or "SERVER") to be included in the log file name.
     */
    static void start(bool enableFileLogging = false, const std::string& mode = "");

    /**
     * @brief Stops the logger service and cleans up resources.
     */
    static void stop();

    /**
     * @brief Queues a message to be logged.
     * @param message The message string to log.
     */
    static void log(const std::string& message);

    /**
     * @brief Logs formatted test statistics.
     * @param throughputMbps The calculated throughput in Mbps.
     * @param duration The test duration in seconds.
     * @param totalBytes The total bytes transferred.
     */
    static void writeStats(double throughputMbps, double duration, long long totalBytes);

    static void writeFinalReport(const std::string& role,
                                 const TestStats& localStats,
                                 const TestStats& remoteStats);

private:
    /**
     * @brief The function executed by the worker thread to process the log queue.
     */
    static void logWorker();

    /**
     * @brief Manages log file rotation, keeping only the most recent 10 logs for the current mode.
     * @param mode The current operation mode (e.g., "CLIENT" or "SERVER").
     */
    static void manageLogRotation(const std::string& mode);

    // Static member variables for the logging framework
    static std::mutex queueMutex;               // Mutex to protect the message queue.
    static std::condition_variable cv;          // Condition variable to signal new messages.
    static std::deque<std::string> messageQueue; // Queue to hold pending log messages.
    static std::thread workerThread;            // The dedicated thread for writing logs.
    static std::atomic<bool> running;           // Flag to control the logger's running state.
    static const std::string getTimeNow();      // Get System time

    // Optional file logging
    static std::ofstream logStream;             // File stream for persistent logs.
    static std::atomic<bool> saveToFile;        // Controls whether logs are saved to a file.
    static const std::string logDirectory;      // Directory to store log files.
};
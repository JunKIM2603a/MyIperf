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


#include <cstdlib>  // std::abort
#include <sstream>  // std::ostringstream

// wait function for debug step
void DebugPause(const std::string& message = "Press Enter to continue...") {
    std::cout << message;
    std::cout.flush();  // flush output buffer to show message
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // flush prior input buffer
    std::cin.get();     // wait to input enter
}

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

// 로그용 매크로 (여기서는 콘솔에 출력)
#define LOG_ERROR(msg) \
    do { \
        std::cerr << "[ERROR] " << msg << std::endl; \
    } while (0)

// 커스텀 Assert 매크로
#define ASSERT_LOG(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            std::ostringstream oss; \
            oss << "Assertion failed!\n" \
                << "  Condition: " << #cond << "\n" \
                << "  Message: "   << string_format(fmt, ##__VA_ARGS__) << "\n" \
                << "  File: "      << __FILE__ << "\n" \
                << "  Line: "      << __LINE__ << "\n" \
                << "  Function: "  << __func__ << "\n"; \
            LOG_ERROR(oss.str()); \
            std::abort(); \
        } \
    } while (0)



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
     */
    static void start();

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
                                 long long totalBytes,
                                 long long totalPackets,
                                 long long checksumErrors,
                                 long long sequenceErrors,
                                 double durationSeconds,
                                 double throughputMbps);

private:
    /**
     * @brief The function executed by the worker thread to process the log queue.
     */
    static void logWorker();

    // Static member variables for the logging framework
    static std::mutex queueMutex;               // Mutex to protect the message queue.
    static std::condition_variable cv;          // Condition variable to signal new messages.
    static std::deque<std::string> messageQueue; // Queue to hold pending log messages.
    static std::thread workerThread;            // The dedicated thread for writing logs.
    static std::atomic<bool> running;           // Flag to control the logger's running state.

    // Optional file logging
    static std::ofstream logStream;             // File stream for persistent logs.
};
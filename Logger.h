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

/**
 * @brief Pauses execution for debugging purposes, waiting for user input.
 * @param message The message to display to the user.
 */
void DebugPause(const std::string& message = "Press Enter to continue...");

/**
 * @brief Formats a string using printf-style syntax.
 * @param fmt The format string.
 * @param ... The variable arguments.
 * @return The formatted string.
 */
inline std::string string_format(const char* fmt, ...);

/**
 * @brief Logs an error message to the standard error stream.
 * @param msg The error message to log.
 */
inline void logError(const std::string& msg);

/**
 * @brief A custom assertion function that logs detailed information before aborting.
 * @param condition The condition to check.
 * @param conditionStr The string representation of the condition.
 * @param file The file where the assertion occurred.
 * @param line The line number of the assertion.
 * @param func The function name where the assertion occurred.
 * @param msg A custom message to log with the assertion.
 */
inline void assertLog(bool condition, const char* conditionStr, const char* file, int line, const char* func, const std::string& msg);

/**
 * @def ASSERT_LOG(cond, fmt, ...)
 * @brief A macro for asserting a condition and logging a formatted message if it fails.
 *
 * This macro calls the `assertLog` function with the current file, line, and function context.
 * It allows for a printf-style formatted message to be included in the assertion log.
 */
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
    /**
     * @brief Starts the logger with the given configuration.
     * @param config The configuration for the logger.
     */
    static void start(const Config& config);

    /**
     * @brief Stops the logger and waits for the worker threads to finish.
     */
    static void stop();

    /**
     * @brief Logs a message.
     * @param message The message to log.
     */
    static void log(const std::string& message);

    /**
     * @brief Writes performance statistics to the log.
     * @param throughputMbps The throughput in Mbps.
     * @param duration The duration of the test in seconds.
     * @param totalBytes The total bytes transferred.
     */
    static void writeStats(double throughputMbps, double duration, long long totalBytes);

    /**
     * @brief Writes the final report of the test to the log.
     * @param role The role of the current instance (Client or Server).
     * @param localStats The statistics of the local instance.
     * @param remoteStats The statistics of the remote instance.
     */
    static void writeFinalReport(const std::string& role,
                                 const TestStats& localStats,
                                 const TestStats& remoteStats);

private:
    /**
     * @brief The main function for the logger worker thread.
     */
    static void logWorker();

    /**
     * @brief Manages log file rotation.
     * @param mode The mode of the logger (e.g., "CLIENT" or "SERVER").
     */
    static void manageLogRotation(const std::string& mode);

    /**
     * @brief Gets the current time as a formatted string.
     * @return The current time string.
     */
    static const std::string getTimeNow();

    // Logging framework members
    /**< Mutex to protect the message queue. */
    static std::mutex queueMutex;
    /**< Condition variable to signal the logger worker thread. */
    static std::condition_variable cv;
    /**< The queue of messages to be logged. */
    static std::deque<std::string> messageQueue;
    /**< The logger worker thread. */
    static std::thread workerThread;
    /**< Flag to control the running state of the logger. */
    static std::atomic<bool> running;

    // File logging members
    /**< The output file stream for the log file. */
    static std::ofstream logStream;
    /**< Flag to control whether to save the log to a file. */
    static std::atomic<bool> saveToFile;
    /**< The directory where log files are stored. */
    static const std::string logDirectory;

#ifdef _WIN32
    // Named pipe logging members
    /**
     * @brief The main function for the named pipe worker thread.
     */
    static void pipeWorker();
    /**< The named pipe worker thread. */
    static std::thread pipeThread;
    /**< The handle to the named pipe. */
    static HANDLE hPipe;
    /**< Flag to indicate if a client is connected to the named pipe. */
    static std::atomic<bool> pipeConnected;
    /**< The name of the named pipe. */
    static std::string pipeName;
#endif
};
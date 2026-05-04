#pragma once

#include <string>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <atomic>
#include <fstream>
#include <filesystem>
#include "myiperf/Protocol.h"
#include "myiperf/Config.h"

/**
 * @class Logger
 * @brief A thread-safe, asynchronous logging utility.
 *
 * This static class provides a simple logging framework that queues messages
 * from multiple threads and writes them to the console and optional log files.
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
     * @brief Writes the final report of the test to the log.
     * @param role The role of the current instance (Client or Server).
     * @param localStats The statistics of the local instance.
     * @param remoteStats The statistics of the remote instance.
     */
    static void writeFinalReport(const std::string& role,
                                 const TestStats& localStats,
                                 const TestStats& remoteStats);

    /**
     * @brief Sets whether console output is enabled.
     * @param enabled True to enable console output, false to disable.
     */
    static void setConsoleOutput(bool enabled);
    /**
     * @brief Checks if console output is enabled.
     * @return True if console output is enabled, false otherwise.
     */
    static bool isConsoleOutputEnabled();

private:
    /**
     * @brief The main function for the logger worker thread.
     */
    static void logWorker();

    /**
     * @brief The main function for the pipe connection worker thread.
     */
    static void pipeWorker();

    /**
     * @brief Manages log file rotation.
     * @param mode The mode of the logger (e.g., "CLIENT" or "SERVER").
     */
    static void manageLogRotation(const std::string& mode);

    // Logging framework members
    /**< Mutex to protect the message queue. */
    static std::mutex queueMutex;
    /**< Condition variable to signal the logger worker thread. */
    static std::condition_variable cv;
    /**< The queue of messages to be logged. */
    static std::deque<std::string> messageQueue;
    /**< The logger worker thread. */
    static std::thread workerThread;
    /**< Mutex guarding start/stop transitions. */
    static std::mutex startStopMutex;
    /**< Flag indicating the logger has been started. */
    static std::atomic<bool> started;
    /**< Flag to control the running state of the logger worker loop. */
    static std::atomic<bool> running;

    // File logging members
    /**< The output file stream for the log file. */
    static std::ofstream logStream;
    /**< Flag to control whether to save the log to a file. */
    static std::atomic<bool> saveToFile;
    /**< The directory where log files are stored. */
    static const std::string logDirectory;
    /**< Mutex protecting direct output when logger is not running. */
    static std::mutex immediateMutex;

    // Pipe logging members
    static std::string pipeName;
    static std::atomic<bool> pipeConnected;
    static std::thread pipeThread;
#ifdef _WIN32
    static void* hPipe; 
#endif

    static bool consoleOutput;  // 콘솔 출력 활성화 플래그
};

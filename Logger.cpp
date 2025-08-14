#include "Logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Initialize static member variables.
std::mutex Logger::queueMutex;
std::condition_variable Logger::cv;
std::deque<std::string> Logger::messageQueue;
std::thread Logger::workerThread;
std::atomic<bool> Logger::running(false);
std::ofstream Logger::logStream;

/**
 * @brief Starts the logging service.
 * It spawns a worker thread to handle log message processing.
 */
void Logger::start() {
    running = true;
    // Enable ANSI colors on Windows 10+ consoles
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
            SetConsoleMode(hOut, mode);
        }
    }
#endif
    // open per-process log file
    try {
        std::ostringstream name;
#ifdef _WIN32
        name << "ipeftc_" << GetCurrentProcessId() << ".log";
#else
        name << "ipeftc_" << getpid() << ".log";
#endif
        logStream.open(name.str(), std::ios::out | std::ios::app);
    } catch (...) {
        // ignore file open errors
    }
    workerThread = std::thread(logWorker);
}

/**
 * @brief Stops the logging service.
 * It signals the worker thread to terminate and waits for it to finish.
 */
void Logger::stop() {
    running = false;
    cv.notify_all(); // Notify all waiting threads to process remaining messages and exit.
    if (workerThread.joinable()) {
        workerThread.join(); // Wait for the worker thread to complete its work.
    }
    if (logStream.is_open()) {
        logStream.flush();
        logStream.close();
    }
}

/**
 * @brief Adds a message to the logging queue for asynchronous processing.
 * @param message The message to be logged.
 */
void Logger::log(const std::string& message) {
    if (!running) return; // Ignore logs if the service is not running.
    {
        // Lock the queue to ensure thread-safe access.
        std::lock_guard<std::mutex> lock(queueMutex);
        messageQueue.push_back(message);
    }
    // Notify the worker thread that a new message is available.
    cv.notify_one();
}

/**
 * @brief Formats and logs test statistics.
 * @param throughputMbps Throughput in Megabits per second.
 * @param duration Test duration in seconds.
 * @param totalBytes Total bytes transferred.
 */
void Logger::writeStats(double throughputMbps, double duration, long long totalBytes) {
    if (!running) return;
    log("--- Test Results ---");
    log("  Duration: " + std::to_string(duration) + " seconds");
    log("  Total Data Transferred: " + std::to_string(totalBytes) + " bytes");
    log("  Average Throughput: " + std::to_string(throughputMbps) + " Mbps");
    log("--------------------");
}

void Logger::writeFinalReport(const std::string& role,
                              long long totalBytes,
                              long long totalPackets,
                              long long checksumErrors,
                              long long sequenceErrors,
                              double durationSeconds,
                              double throughputMbps) {
    if (!running) return;
    log("==== Final Report (" + role + ") ====");
    log("  Total Bytes        : " + std::to_string(totalBytes));
    log("  Total Packets      : " + std::to_string(totalPackets));
    log("  Checksum Errors    : " + std::to_string(checksumErrors));
    log("  Sequence Errors    : " + std::to_string(sequenceErrors));
    log("  Duration (s)       : " + std::to_string(durationSeconds));
    log("  Throughput (Mbps)  : " + std::to_string(throughputMbps));
    log("================================");
}

/**
 * @brief The main function for the logger's worker thread.
 * It waits for messages in the queue and prints them to the console.
 */
void Logger::logWorker() {
    while (true) {
        std::deque<std::string> writeQueue;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            // Wait until a message is available or the logger is shutting down.
            // The condition now ensures that all messages are processed before exiting.
            cv.wait(lock, [] { return !messageQueue.empty() || !running; });

            // If the logger is stopped and the queue is empty, exit the thread.
            if (!running && messageQueue.empty()) {
                break;
            }
            // Swap the message queue with a local one to minimize time spent under the lock.
            writeQueue.swap(messageQueue);
        } // The lock is released here.

        // Write all messages from the local queue to the console and file.
        for (const auto& msg : writeQueue) {
            auto now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] " << msg;
            const std::string out = oss.str();
            // Colorize for console
            const std::string colored = [&]() {
                if (msg.rfind("Error:", 0) == 0) return std::string("\x1b[31m") + out + "\x1b[0m"; // red
                if (msg.rfind("Warning:", 0) == 0) return std::string("\x1b[33m") + out + "\x1b[0m"; // yellow
                if (msg.rfind("Info:", 0) == 0) return std::string("\x1b[32m") + out + "\x1b[0m"; // green
                if (msg.rfind("Debug:", 0) == 0) return std::string("\x1b[36m") + out + "\x1b[0m"; // cyan
                return out;
            }();
            std::cout << colored << std::endl;
            if (logStream.is_open()) {
                logStream << out << std::endl;
                logStream.flush();
            }
        }
    }
}
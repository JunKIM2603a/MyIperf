#include "Logger.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

// Initialize static member variables.
std::mutex Logger::queueMutex;
std::condition_variable Logger::cv;
std::deque<std::string> Logger::messageQueue;
std::thread Logger::workerThread;
std::mutex Logger::startStopMutex;
std::atomic<bool> Logger::started(false);
std::atomic<bool> Logger::running(false);
std::mutex Logger::immediateMutex;
std::ofstream Logger::logStream;
std::atomic<bool> Logger::saveToFile(false);
const std::string Logger::logDirectory = "Log";

namespace {

std::string buildTimestampedLine(const std::string& message) {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm = *std::localtime(&now_c);
    std::ostringstream oss;
    oss << "[" << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") << "] " << message;
    return oss.str();
}

std::string colorizeLine(const std::string& message, const std::string& formatted) {
    if (message.rfind("Error:", 0) == 0) return std::string("\x1b[31m") + formatted + "\x1b[0m";
    if (message.rfind("Warning:", 0) == 0) return std::string("\x1b[33m") + formatted + "\x1b[0m";
    if (message.rfind("Info:", 0) == 0) return std::string("\x1b[32m") + formatted + "\x1b[0m";
    if (message.rfind("Debug:", 0) == 0) return std::string("\x1b[36m") + formatted + "\x1b[0m";
    return formatted;
}

} // namespace

// wait function for debug step
void DebugPause(const std::string& message) {
#ifdef DEBUG_LOG
    Logger::log(message);
    std::cout.flush();
#endif
}

/**
 * @brief Manages log file rotation.
 *
 * This function checks the number of log files in the log directory for a specific mode
 * (CLIENT or SERVER) and deletes the oldest ones if the count exceeds a certain limit (e.g., 10).
 *
 * @param mode The mode of the logger (e.g., "CLIENT" or "SERVER").
 */
void Logger::manageLogRotation(const std::string& mode) {
    if (!std::filesystem::exists(logDirectory)) {
        return;
    }

    std::vector<std::filesystem::path> logFiles;
    std::string mode_str = "_" + mode + "_";

    for (const auto& entry : std::filesystem::directory_iterator(logDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            if (entry.path().string().find(mode_str) != std::string::npos) {
                logFiles.push_back(entry.path());
            }
        }
    }

    const unsigned int maxFiles = 100;
    if (logFiles.size() >= maxFiles) {
        std::sort(logFiles.begin(), logFiles.end());

        int filesToDelete = logFiles.size() - (maxFiles - 1);
        for (int i = 0; i < filesToDelete; ++i) {
            std::filesystem::remove(logFiles[i]);
        }
    }
}


/**
 * @brief Starts the logger with the given configuration.
 *
 * This function initializes the logger, sets up file logging if enabled,
 * and starts the logger worker thread.
 *
 * @param config The configuration for the logger.
 */
void Logger::start(const Config& config) {
    const std::string mode = config.getMode() == Config::TestMode::CLIENT ? "CLIENT" : "SERVER";
    std::string timestampLabel;
    std::string logOpenError;
    bool alreadyStarted = false;

    {
        std::lock_guard<std::mutex> lock(startStopMutex);
        if (started.load(std::memory_order_acquire)) {
            alreadyStarted = true;
        } else {
            {
                std::lock_guard<std::mutex> queueLock(queueMutex);
                messageQueue.clear();
            }

#ifdef _WIN32
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            if (hOut != INVALID_HANDLE_VALUE) {
                DWORD dmode = 0;
                if (GetConsoleMode(hOut, &dmode)) {
                    dmode |= 0x0004;
                    SetConsoleMode(hOut, dmode);
                }
            }
#endif

            if (logStream.is_open()) {
                logStream.close();
            }
            saveToFile.store(false, std::memory_order_release);

            if (config.getSaveLogs()) {
                if (!std::filesystem::exists(logDirectory)) {
                    std::filesystem::create_directory(logDirectory);
                }
                manageLogRotation(mode);

                auto now = std::chrono::system_clock::now();
                std::time_t now_c = std::chrono::system_clock::to_time_t(now);
                std::tm local_tm = *std::localtime(&now_c);

                std::ostringstream timestampStream;
                timestampStream << std::put_time(&local_tm, "%Y%m%d_%H%M%S");
                timestampLabel = timestampStream.str();

                std::ostringstream name;
                name << logDirectory << "/ipeftc_" << mode << "_" << timestampLabel << "_";
#ifdef _WIN32
                name << GetCurrentProcessId();
#else
                name << getpid();
#endif
                name << ".log";

                logStream.open(name.str(), std::ios::out | std::ios::app);
                if (logStream.is_open()) {
                    saveToFile.store(true, std::memory_order_release);
                } else {
                    timestampLabel.clear();
                    saveToFile.store(false, std::memory_order_release);
                    logOpenError = "Error: Failed to open log file: " + name.str();
                }
            }

            running.store(true, std::memory_order_release);
            started.store(true, std::memory_order_release);
            workerThread = std::thread(logWorker);
        }
    }

    if (alreadyStarted) {
        log("Warning: Logger::start() called while logger already running. Ignoring.");
        return;
    }

    if (!logOpenError.empty()) {
        log(logOpenError);
    }

    if (!timestampLabel.empty()) {
        log("Info: Logger started " + timestampLabel);
    } else {
        log("Info: Logger started.");
    }

    std::ostringstream optionStream;
    optionStream << " --mode " << mode
                 << " --target " << config.getTargetIP()
                 << " --port " << config.getPort()
                 << " --packet-size " << config.getPacketSize()
                 << " --num-packets " << config.getNumPackets()
                 << " --interval-ms " << config.getSendIntervalMs()
                 << " --save-logs " << (config.getSaveLogs() ? "true" : "false")
                 << " --handshake-timeout-ms " << config.getHandshakeTimeoutMs();
    log("Info: Options =>" + optionStream.str());
}

/**
 * @brief Stops the logger and waits for the worker threads to finish.
 *
 * This function signals the worker threads to stop, waits for them to join,
 * and cleans up any resources like file streams.
 */
void Logger::stop() {
    {
        std::lock_guard<std::mutex> lock(startStopMutex);
        if (!started.load(std::memory_order_acquire)) {
            return;
        }
        running.store(false, std::memory_order_release);
    }

    cv.notify_all();

    if (workerThread.joinable()) {
        workerThread.join();
    }
    workerThread = std::thread();

    if (saveToFile.load(std::memory_order_acquire) && logStream.is_open()) {
        logStream.flush();
        logStream.close();
    }
    saveToFile.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> queueLock(queueMutex);
        messageQueue.clear();
    }

    started.store(false, std::memory_order_release);
}

/**
 * @brief Logs a message.
 *
 * This function adds a message to the logger's queue to be processed by the worker thread.
 * It is thread-safe.
 *
 * @param message The message to log.
 */
void Logger::log(const std::string& message) {
    if (!started.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(immediateMutex);
        const std::string formatted = buildTimestampedLine(message);
        const std::string colored = colorizeLine(message, formatted);
        std::cout << colored << std::endl;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        messageQueue.push_back(message);
    }
    cv.notify_one();
}

/**
 * @brief The main function for the logger worker thread.
 *
 * This function runs on a dedicated thread and is responsible for taking messages
 * from the queue and writing them to the console and optional log file.
 */
void Logger::logWorker() {
    while (true) {
        std::deque<std::string> writeQueue;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [] { return !messageQueue.empty() || !running.load(std::memory_order_acquire); });
            if (!running.load(std::memory_order_acquire) && messageQueue.empty()) {
                break;
            }
            writeQueue.swap(messageQueue);
        }

        for (const auto& msg : writeQueue) {
            const std::string formatted = buildTimestampedLine(msg);
            const std::string colored = colorizeLine(msg, formatted);
            std::cout << colored << std::endl;

            if (saveToFile.load(std::memory_order_acquire) && logStream.is_open()) {
                logStream << formatted << std::endl;
                logStream.flush();
            }
        }
    }
}

/**
 * @brief Writes the final report of the test to the log.
 *
 * This function formats and logs the local and remote statistics of the test.
 *
 * @param role The role of the current instance (Client or Server).
 * @param localStats The statistics of the local instance.
 * @param remoteStats The statistics of the remote instance.
 */
void Logger::writeFinalReport(const std::string& role,
                              const TestStats& localStats,
                              const TestStats& remoteStats) {
    if (!running) return;

    log("==== Final Report (" + role + ") ====");
    log("--- Local Stats (This machine's perspective) ---");
    log("   Total Bytes Sent    : " + std::to_string(localStats.totalBytesSent) + " (Total bytes this machine attempted to send)");
    log("   Total Packets Sent  : " + std::to_string(localStats.totalPacketsSent) + " (Total packets this machine attempted to send)");
    log("   Total Bytes Recv    : " + std::to_string(localStats.totalBytesReceived) + " (Total bytes this machine received, including headers)");
    log("   Total Packets Recv  : " + std::to_string(localStats.totalPacketsReceived) + " (Total data packets this machine received)");
    log("   Checksum Errors     : " + std::to_string(localStats.failedChecksumCount) + " (Packets received by this machine with an invalid checksum)");
    log("   Sequence Errors     : " + std::to_string(localStats.sequenceErrorCount) + " (Data packets received by this machine out of order)");
    log("   Duration (s)        : " + std::to_string(localStats.duration) + " (The duration of the data transfer phase in seconds)");
    log("   Throughput (Mbps)   : " + std::to_string(localStats.throughputMbps) + " (Calculated as: [Total Bytes * 8] / [Duration * 1,000,000])");

    if (role == "CLIENT" || role == "SERVER") { 
        log("--- Remote Stats (Remote machine's perspective) ---");
        log("   Total Bytes Sent    : " + std::to_string(remoteStats.totalBytesSent) + " (Total bytes the remote machine sent)");
        log("   Total Packets Sent  : " + std::to_string(remoteStats.totalPacketsSent) + " (Total packets the remote machine sent)");
        log("   Total Bytes Recv    : " + std::to_string(remoteStats.totalBytesReceived) + " (Total bytes the remote machine received)");
        log("   Total Packets Recv  : " + std::to_string(remoteStats.totalPacketsReceived) + " (Total data packets the remote machine received)");
        log("   Checksum Errors     : " + std::to_string(remoteStats.failedChecksumCount) + " (Packets received by the remote machine with an invalid checksum)");
        log("   Sequence Errors     : " + std::to_string(remoteStats.sequenceErrorCount) + " (Data packets received by the remote machine out of order)");
        log("   Duration (s)        : " + std::to_string(remoteStats.duration) + " (The remote machine's measurement of the test duration)");
        log("   Throughput (Mbps)   : " + std::to_string(remoteStats.throughputMbps) + " (The remote machine's calculated throughput)");
    }
    log("================================");
}

/**
 * @brief Gets the current time as a formatted string.
 * @return The current time string in the format "[YYYY-MM-DD HH:MM:SS] ".
 */
const std::string Logger::getTimeNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] ";
    return oss.str();
}
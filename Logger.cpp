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
std::atomic<bool> Logger::running(false);
std::ofstream Logger::logStream;
std::atomic<bool> Logger::saveToFile(false);
const std::string Logger::logDirectory = "Log";

// wait function for debug step
void DebugPause(const std::string& message) {
    Logger::log(message);
    std::cout.flush();
}

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

    if (logFiles.size() >= 10) {
        std::sort(logFiles.begin(), logFiles.end());

        int filesToDelete = logFiles.size() - 9;
        for (int i = 0; i < filesToDelete; ++i) {
            std::filesystem::remove(logFiles[i]);
        }
    }
}

void Logger::start(bool enableFileLogging, const std::string& mode) {
    running = true;
    saveToFile = enableFileLogging;

#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dmode = 0;
        if (GetConsoleMode(hOut, &dmode)) {
            dmode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
            SetConsoleMode(hOut, dmode);
        }
    }
#endif

    if (saveToFile) {
        if (!std::filesystem::exists(logDirectory)) {
            std::filesystem::create_directory(logDirectory);
        }

        manageLogRotation(mode);

        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm = *std::localtime(&now_c);

        std::ostringstream name;
        name << logDirectory << "/ipeftc_" << mode << "_"
             << std::put_time(&local_tm, "%Y%m%d_%H%M%S") << "_";
#ifdef _WIN32
        name << GetCurrentProcessId();
#else
        name << getpid();
#endif
        name << ".log";

        try {
            logStream.open(name.str(), std::ios::out | std::ios::app);
        } catch (...) {
            log("Error: Could not open log file.");
        }
    }

    workerThread = std::thread(logWorker);
}

void Logger::stop() {
    running = false;
    cv.notify_all();
    if (workerThread.joinable()) {
        workerThread.join();
    }
    if (saveToFile && logStream.is_open()) {
        logStream.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        logStream.close();
    }
}

void Logger::log(const std::string& message) {
    if (!running) return;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        messageQueue.push_back(message);
    }
    cv.notify_one();
}

void Logger::writeFinalReport(const std::string& role,
                              const TestStats& localStats,
                              const TestStats& remoteStats) {
    if (!running) return;

    log("==== Final Report (" + role + ") ====");
    log("--- Local Stats (This machine's perspective) ---");
    log("  Total Bytes Sent   : " + std::to_string(localStats.totalBytesSent) + " (Total bytes this machine attempted to send)");
    log("  Total Packets Sent : " + std::to_string(localStats.totalPacketsSent) + " (Total packets this machine attempted to send)");
    log("  Total Bytes Recv   : " + std::to_string(localStats.totalBytesReceived) + " (Total bytes this machine received, including headers)");
    log("  Total Packets Recv : " + std::to_string(localStats.totalPacketsReceived) + " (Total data packets this machine received)");
    log("  Checksum Errors    : " + std::to_string(localStats.failedChecksumCount) + " (Packets received by this machine with an invalid checksum)");
    log("  Sequence Errors    : " + std::to_string(localStats.sequenceErrorCount) + " (Data packets received by this machine out of order)");
    log("  Duration (s)       : " + std::to_string(localStats.duration) + " (The duration of the data transfer phase in seconds)");
    log("  Throughput (Mbps)  : " + std::to_string(localStats.throughputMbps) + " (Calculated as: [Total Bytes * 8] / [Duration * 1,000,000])");

    if (role == "CLIENT" || role == "SERVER") { 
        log("--- Remote Stats (Remote machine's perspective) ---");
        log("  Total Bytes Sent   : " + std::to_string(remoteStats.totalBytesSent) + " (Total bytes the remote machine sent)");
        log("  Total Packets Sent : " + std::to_string(remoteStats.totalPacketsSent) + " (Total packets the remote machine sent)");
        log("  Total Bytes Recv   : " + std::to_string(remoteStats.totalBytesReceived) + " (Total bytes the remote machine received)");
        log("  Total Packets Recv : " + std::to_string(remoteStats.totalPacketsReceived) + " (Total data packets the remote machine received)");
        log("  Checksum Errors    : " + std::to_string(remoteStats.failedChecksumCount) + " (Packets received by the remote machine with an invalid checksum)");
        log("  Sequence Errors    : " + std::to_string(remoteStats.sequenceErrorCount) + " (Data packets received by the remote machine out of order)");
        log("  Duration (s)       : " + std::to_string(remoteStats.duration) + " (The remote machine's measurement of the test duration)");
        log("  Throughput (Mbps)  : " + std::to_string(remoteStats.throughputMbps) + " (The remote machine's calculated throughput)");
    }
    log("================================");
}

const std::string Logger::getTimeNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] ";
    return oss.str();
}

void Logger::logWorker() {
    while (true) {
        std::deque<std::string> writeQueue;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [] { return !messageQueue.empty() || !running; });

            if (!running && messageQueue.empty()) {
                break;
            }
            writeQueue.swap(messageQueue);
        }

        for (const auto& msg : writeQueue) {
            auto now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] " << msg;
            const std::string out =  oss.str();
            const std::string colored = [&]() {
                if (msg.rfind("Error:", 0) == 0) return std::string("\x1b[31m") + out + "\x1b[0m";
                if (msg.rfind("Warning:", 0) == 0) return std::string("\x1b[33m") + out + "\x1b[0m";
                if (msg.rfind("Info:", 0) == 0) return std::string("\x1b[32m") + out + "\x1b[0m";
                if (msg.rfind("Debug:", 0) == 0) return std::string("\x1b[36m") + out + "\x1b[0m";
                return out;
            }();
            std::cout << colored << std::endl;
            if (saveToFile && logStream.is_open()) {
                logStream << out << std::endl;
                logStream.flush();
            }
        }
    }
}
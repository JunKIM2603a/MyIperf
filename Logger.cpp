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

#ifdef _WIN32
HANDLE Logger::hPipe = INVALID_HANDLE_VALUE;
std::atomic<bool> Logger::pipeConnected(false);
std::string Logger::pipeName = ""; // Initialized as empty
std::thread Logger::pipeThread;
#else
// _WIN32가 아닌 환경을 위한 더미 변수
std::thread Logger::pipeThread;
#endif

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

#ifdef _WIN32
void Logger::pipeWorker() {
    std::cerr << "Debug: pipeWorker started.\n";
    while (running) {
        std::cerr << "Debug: pipeWorker loop - top.\n";
        if (pipeName.empty()) { // Don't start until pipe name is set
            std::cerr << "Debug: pipeWorker - pipeName is empty, waiting.\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::cerr << "Debug: pipeWorker - Creating named pipe.\n";
        hPipe = CreateNamedPipe(
            pipeName.c_str(),
            PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, // Only one instance
            4096, // Output buffer size
            4096, // Input buffer size
            0,    // Default timeout
            NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            logError("Could not create named pipe. Error: " + std::to_string(GetLastError()));
            std::cerr << "Debug: pipeWorker - CreateNamedPipe failed, sleeping.\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        Logger::log("Info: Named pipe '" + pipeName + "' created. Waiting for a client to connect...");
        std::cerr << "Debug: pipeWorker - Calling ConnectNamedPipe.\n";
        bool connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        std::cerr << "Debug: pipeWorker - ConnectNamedPipe returned. Connected: " + std::to_string(connected) + "\n";

        if (connected && running) {
            Logger::log("Info: Client connected to named pipe '" + pipeName + "'.");
            pipeConnected = true;
            while (pipeConnected && running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::cerr << "Debug: pipeWorker - Inner connected loop exited.\n";
        } else {
            pipeConnected = false;
            std::cerr << "Debug: pipeWorker - Not connected or running is false.\n";
        }

        if(hPipe != INVALID_HANDLE_VALUE) {
            std::cerr << "Debug: pipeWorker - Disconnecting and closing pipe handle.\n";
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
        }

        if(pipeConnected) {
             Logger::log("Info: Client disconnected from named pipe '" + pipeName + "'.");
        }
        pipeConnected = false;
        std::cerr << "Debug: pipeWorker loop - bottom.\n";
    }
    std::cerr << "Debug: pipeWorker finished.\n";
}
#endif

void Logger::start(const Config& config) {
    std::cerr << "DEBUG: Entering Logger::start()\n";
    running = true;

    const std::string mode = config.getMode() == Config::TestMode::CLIENT ? "CLIENT" : "SERVER";

#ifdef _WIN32
    pipeName = "\\\\.\\pipe\\myiperflog_" + mode + "_" + config.getTargetIP() + "_" + std::to_string(config.getPort());
    // Replace invalid characters for pipe names, like colons in IPv6
    std::replace(pipeName.begin(), pipeName.end(), ':', '_'); 

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dmode = 0;
        if (GetConsoleMode(hOut, &dmode)) {
            dmode |= 0x0004;
            SetConsoleMode(hOut, dmode);
        }
    }
#endif

    if (config.getSaveLogs()) {
        saveToFile = true;
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
        logStream.open(name.str(), std::ios::out | std::ios::app);
        if (!logStream.is_open()) {
            logError("Failed to open log file: " + name.str());
            saveToFile = false; // Disable file logging if open failed
        }
    }

    workerThread = std::thread(logWorker);
#ifdef _WIN32
    pipeThread = std::thread(pipeWorker);
#endif
}

void Logger::stop() {
    Logger::log("Info: Stopping the logger.");
    
    // 먼저 모든 작업 스레드에 종료 신호를 보냅니다.
    running = false;
    cv.notify_one();

#ifdef _WIN32
    // Windows 환경에서만 명명된 파이프를 처리합니다.
    if (hPipe != INVALID_HANDLE_VALUE) {
        Logger::log("Debug: Cancelling pending I/O on named pipe.");
        // 파이프 핸들에 대한 모든 보류 중인 I/O 작업을 취소합니다.
        // 이것은 ConnectNamedPipe() 호출이 즉시 반환되도록 합니다.
        if (!CancelIoEx(hPipe, NULL)) {
            Logger::log("Error: Failed to cancel named pipe I/O. Error: " + std::to_string(GetLastError()));
        }
        
        // 파이프 스레드가 종료될 때까지 기다립니다.
        if (pipeThread.joinable()) {
            pipeThread.join();
        }
        
        // 스레드가 종료된 후 핸들을 닫습니다.
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
    }
#endif

    // 로거 작업 스레드가 종료될 때까지 기다립니다.
    if (workerThread.joinable()) {
        workerThread.join();
    }

    if (logStream.is_open()) {
        logStream.close();
    }
    std::cerr << "Debug: Logger::stop completed." << std::endl;
}

void Logger::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (!running) return;
    messageQueue.push_back(message);
    cv.notify_one();
}

void Logger::logWorker() {
    std::cerr << "Debug: logWorker started.\n";
    while (true) {
        std::cerr << "Debug: logWorker loop - top.\n";
        std::deque<std::string> writeQueue;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [] { return !messageQueue.empty() || !running; });
            if (!running && messageQueue.empty()) {
                std::cerr << "Debug: logWorker - Shutdown condition met, breaking loop.\n";
                break;
            }
            writeQueue.swap(messageQueue);
            std::cerr << "Debug: logWorker - Message queue swapped. Count: " + std::to_string(writeQueue.size()) + "\n";
        }

        for (const auto& msg : writeQueue) {
            auto now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            std::ostringstream oss;
            oss << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] " << msg;
            const std::string out = oss.str();

            const std::string colored = [&]() {
                if (msg.rfind("Error:", 0) == 0) return std::string("\x1b[31m") + out + "\x1b[0m";
                if (msg.rfind("Warning:", 0) == 0) return std::string("\x1b[33m") + out + "\x1b[0m";
                if (msg.rfind("Info:", 0) == 0) return std::string("\x1b[32m") + out + "\x1b[0m";
                if (msg.rfind("Debug:", 0) == 0) return std::string("\x1b[36m") + out + "\x1b[0m";
                return out;
            }() ;
            
            std::cerr << colored << std::endl;

            if (saveToFile && logStream.is_open()) {
                logStream << out << std::endl;
                logStream.flush();
            }
#ifdef _WIN32
            if (pipeConnected) {
                DWORD bytesWritten = 0;
                bool success = WriteFile(hPipe, out.c_str(), out.length(), &bytesWritten, NULL);
                if (!success) {
                    std::cerr << "Debug: logWorker - WriteFile to pipe failed. Error: " + std::to_string(GetLastError()) + "\n";
                    pipeConnected = false;
                }
            }
#endif
        }
        std::cerr << "Debug: logWorker loop - bottom.\n";
    }
}

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

const std::string Logger::getTimeNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] ";
    return oss.str();
}

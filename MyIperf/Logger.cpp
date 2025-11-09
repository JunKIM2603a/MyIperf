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

#ifdef _WIN32
/**
 * @brief The main function for the named pipe worker thread.
 *
 * This function creates a named pipe and waits for a client to connect.
 * Once a client is connected, it sends log messages to the pipe.
 * This is a Windows-specific feature.
 */
void Logger::pipeWorker() {
    std::cerr << "Debug: pipeWorker started.\n";
    while (running) {
#ifdef DEBUG_PIPE 
        std::cerr << "Debug: pipeWorker loop - top.\n";
#endif
        if (pipeName.empty()) { // Don't start until pipe name is set
            std::cerr << "Debug: pipeWorker - pipeName is empty, waiting.\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

#ifdef DEBUG_PIPE 
        std::cerr << "Debug: pipeWorker - Creating named pipe.\n";
#endif        
        hPipe = CreateNamedPipeA(
            pipeName.c_str(),
            PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, // Only one instance
            4096, // Output buffer size
            4096, // Input buffer size
            NMPWAIT_USE_DEFAULT_WAIT,    // Default timeout for client to wait, not for server
            NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            log("Error: Could not create named pipe. Error: " + std::to_string(GetLastError()));
            std::cerr << "Debug: pipeWorker - CreateNamedPipe failed, sleeping.\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

#ifdef DEBUG_PIPE 
        Logger::log("Info: Named pipe '" + pipeName + "' created. Waiting for a client to connect...");
        std::cerr << "Debug: pipeWorker - Calling ConnectNamedPipe.\n";
#endif
        OVERLAPPED ov = {0};
        ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        bool connected = ConnectNamedPipe(hPipe, &ov);
        while (!connected && running)  {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // 이제 timeout을 걸고 기다릴 수 있음
                DWORD waitRes = WaitForSingleObject(ov.hEvent, 1000); // 5초 대기
                if (waitRes == WAIT_OBJECT_0) {
                    // 연결 성공
                    connected = TRUE;
                } else if (waitRes == WAIT_TIMEOUT) {
                    // 타임아웃 → 연결 실패 처리
                    CancelIo(hPipe);
                    connected = FALSE;
                }
            } else if (err == ERROR_PIPE_CONNECTED) {
                // 이미 연결된 상태
                connected = TRUE;
            }
        }
        // bool connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
#ifdef DEBUG_PIPE        
        std::cerr << "Debug: pipeWorker - ConnectNamedPipe returned. Connected: " + std::to_string(connected) + "\n";
#endif

        if (connected && running) {
#ifdef DEBUG_PIPE 
            Logger::log("Info: Client connected to named pipe '" + pipeName + "'.");
#endif
            pipeConnected = true;
            while (pipeConnected && running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
#ifdef DEBUG_PIPE   
            std::cerr << "Debug: pipeWorker - Inner connected loop exited.\n";
#endif
        } else {
            pipeConnected = false;
            std::cerr << "Debug: pipeWorker - Not connected or running is false.\n";
        }

        if(hPipe != INVALID_HANDLE_VALUE) {
#ifdef DEBUG_PIPE   
            std::cerr << "Debug: pipeWorker - Disconnecting and closing pipe handle.\n";
#endif
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
        }

        if(pipeConnected) {
             Logger::log("Info: Client disconnected from named pipe '" + pipeName + "'.");
        }
        pipeConnected = false;
#ifdef DEBUG_PIPE   
        std::cerr << "Debug: pipeWorker loop - bottom.\n";
#endif
    }
    std::cerr << "Debug: pipeWorker finished.\n";
}
#endif

/**
 * @brief Starts the logger with the given configuration.
 *
 * This function initializes the logger, sets up file logging if enabled,
 * and starts the logger and pipe worker threads.
 *
 * @param config The configuration for the logger.
 */
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

    std::ostringstream time;
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
        time << std::put_time(&local_tm, "%Y%m%d_%H%M%S");
#ifdef _WIN32
        name << GetCurrentProcessId();
#else
        name << getpid();
#endif
        name << ".log";
        logStream.open(name.str(), std::ios::out | std::ios::app);
        if (!logStream.is_open()) {
            log("Error: Failed to open log file: " + name.str());
            saveToFile = false; // Disable file logging if open failed
        }
    }

    workerThread = std::thread(logWorker);
    Logger::log("Info: Logger started " + time.str());
    std::ostringstream logStream;
        logStream << " --mode " << (config.getMode() == Config::TestMode::CLIENT ? "CLIENT" : "SERVER")
        << " --target " << config.getTargetIP()
        << " --port " << std::to_string(config.getPort())
        << " --packet-size " << std::to_string(config.getPacketSize())
        << " --num-packets " << std::to_string(config.getNumPackets())
        << " --interval-ms " << std::to_string(config.getSendIntervalMs())
        << " --save-logs " << (config.getSaveLogs() ? "true" : "false");
    Logger::log("Info: Options =>" + logStream.str());

#ifdef _WIN32
    pipeThread = std::thread(pipeWorker);
#endif
}

/**
 * @brief Stops the logger and waits for the worker threads to finish.
 *
 * This function signals the worker threads to stop, waits for them to join,
 * and cleans up any resources like file streams.
 */
void Logger::stop() {
    Logger::log("Debug: Logger::stop() called.");
    running = false;
    cv.notify_all();

    // Give logWorker a chance to process remaining messages and exit its loop
    // std::this_thread::sleep_for(std::chrono::milliseconds(200)); // ADDED DELAY

#ifdef _WIN32
    Logger::log("Debug: Logger::stop() - Attempting to unblock pipeThread.");
    if (!pipeName.empty()) {
        // Create a dummy client to unblock ConnectNamedPipe
        HANDLE hDummyPipe = CreateFileA(
            pipeName.c_str(),
            GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);
        if (hDummyPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(hDummyPipe);
            Logger::log("Debug: Logger::stop() - Dummy pipe client created and closed.");
        } else {
            Logger::log("Warning: Logger::stop() - Failed to create dummy pipe client. Error: " + std::to_string(GetLastError()));
        }
    }
#endif

    // workerThread를 먼저 join하여 모든 로그 처리를 완료하도록 합니다.
    Logger::log("Debug: Logger::stop() - Joining workerThread.");
    if (workerThread.joinable()) {
        workerThread.join();
    }
    std::cout << "Debug: Logger::stop() - workerThread joined.\n";

#ifdef _WIN32
    // Windows 환경에서만 pipeThread를 join합니다.
    std::cout << "Debug: Logger::stop() - Joining pipeThread.\n";
    if (pipeThread.joinable()) {
        std::cout << "Debug: Logger::stop() - pipeThread joinable.\n";
        pipeThread.join();
    }
    std::cout << "Debug: Logger::stop() - pipeThread joined.\n";
#endif

    if (saveToFile && logStream.is_open()) {
        Logger::log("Debug: Logger::stop() - Flushing and closing logStream.");
        logStream.flush();
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));
        logStream.close();
        Logger::log("Debug: Logger::stop() - logStream closed.");
    }
    std::cout << "Debug: Logger::stop() finished.\n";
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
    if (!running) return;
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
 * from the queue and writing them to the console, file, and named pipe.
 */
void Logger::logWorker() {
    std::cerr << "Debug: logWorker started.\n";
    while (true) {
#ifdef DEBUG_LOG
        std::cerr << "Debug: logWorker loop - top.\n";
#endif
        std::deque<std::string> writeQueue;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [] { return !messageQueue.empty() || !running; });
            if (!running && messageQueue.empty()) {
                std::cerr << "Debug: logWorker - Exiting loop (not running and queue empty).\n"; // REMOVED
                break;
            }
            writeQueue.swap(messageQueue);
#ifdef DEBUG_LOG
            std::cerr << "Debug: logWorker - Message queue swapped. Count: " + std::to_string(writeQueue.size()) + "\n";
#endif
        }

#ifndef DEBUG_LOG
        // bool isDebug = false;
        // std::ostringstream oss_debug;
        // for(auto iter = writeQueue.begin(); iter != writeQueue.end(); ++iter) {
        //     if(iter->rfind("Debug:", 0) == 0) writeQueue.erase(iter);
        // }
#endif
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
            
            std::cout << colored << std::endl;

            if (saveToFile && logStream.is_open()) {
                logStream << out << std::endl;
                logStream.flush();
            }
#ifdef _WIN32
            if (pipeConnected) {
                DWORD bytesWritten = 0;
                bool success = WriteFile(hPipe, out.c_str(), out.length(), &bytesWritten, NULL);
                if (!success) {
#ifdef DEBUG_PIPE   
                    std::cerr << "Debug: logWorker - WriteFile to pipe failed. Error: " + std::to_string(GetLastError()) + "\n";
#endif
                    pipeConnected = false;
                }
            }
#endif
        }
#ifdef DEBUG_LOG
        std::cerr << "Debug: logWorker loop - bottom.\n";
#endif
    }
    std::cerr <<"Debug: logWorker finished.\n"; // REMOVED
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
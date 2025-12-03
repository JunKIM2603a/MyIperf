#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <windows.h>

namespace TestRunner2 {

/**
 * @brief PipeReader handles Named Pipe connections and log reception
 * 
 * This class connects to a Named Pipe as a client and reads log messages
 * asynchronously in a separate thread. It provides thread-safe access to
 * accumulated logs.
 */
class PipeReader {
public:
    /**
     * @brief Construct a new PipeReader
     * @param pipeName The full pipe name (e.g., "\\\\.\\pipe\\myiperflog_client_192.168.1.1_60000")
     */
    explicit PipeReader(const std::string& pipeName);
    
    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~PipeReader();
    
    /**
     * @brief Start the pipe reader thread
     * @return true if started successfully, false otherwise
     */
    bool Start();
    
    /**
     * @brief Stop the pipe reader and cleanup resources
     */
    void Stop();
    
    /**
     * @brief Get all accumulated logs received so far
     * @return String containing all logs
     */
    std::string GetAccumulatedLogs();
    
    /**
     * @brief Check if currently connected to the pipe
     * @return true if connected, false otherwise
     */
    bool IsConnected() const;
    
    /**
     * @brief Check if the reader thread is running
     * @return true if running, false otherwise
     */
    bool IsRunning() const;

private:
    /**
     * @brief Thread function for pipe connection and reading
     */
    void ConnectPipeThread();
    
    std::string m_pipeName;
    std::thread m_pipeThread;
    std::atomic<bool> m_quit;
    std::atomic<bool> m_connected;
    
    // Thread-safe queue for log messages
    std::queue<std::string> m_logQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondVar;
    
    // Accumulated logs
    std::string m_accumulatedLogs;
    std::mutex m_logsMutex;
};

} // namespace TestRunner2

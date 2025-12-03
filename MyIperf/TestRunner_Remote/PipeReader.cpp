#include "PipeReader.h"
#include <iostream>
#include <chrono>

namespace TestRunner2 {

PipeReader::PipeReader(const std::string& pipeName)
    : m_pipeName(pipeName)
    , m_quit(false)
    , m_connected(false) {
}

PipeReader::~PipeReader() {
    Stop();
}

bool PipeReader::Start() {
    if (m_pipeThread.joinable()) {
        std::cerr << "[PipeReader] Thread already running" << std::endl;
        return false;
    }
    
    m_quit = false;
    m_connected = false;
    m_accumulatedLogs.clear();
    
    try {
        m_pipeThread = std::thread(&PipeReader::ConnectPipeThread, this);
        std::cout << "[PipeReader] Started pipe reader for: " << m_pipeName << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[PipeReader] Failed to start thread: " << e.what() << std::endl;
        return false;
    }
}

void PipeReader::Stop() {
    if (!m_pipeThread.joinable()) {
        return;
    }
    
    std::cout << "[PipeReader] Stopping pipe reader..." << std::endl;
    m_quit = true;
    m_queueCondVar.notify_all();
    
    if (m_pipeThread.joinable()) {
        m_pipeThread.join();
    }
    
    std::cout << "[PipeReader] Pipe reader stopped" << std::endl;
}

std::string PipeReader::GetAccumulatedLogs() {
    // First, process any remaining items in the queue
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        while (!m_logQueue.empty()) {
            std::string message = m_logQueue.front();
            m_logQueue.pop();
            
            std::lock_guard<std::mutex> logsLock(m_logsMutex);
            m_accumulatedLogs += message;
        }
    }
    
    // Return accumulated logs
    std::lock_guard<std::mutex> logsLock(m_logsMutex);
    return m_accumulatedLogs;
}

bool PipeReader::IsConnected() const {
    return m_connected;
}

bool PipeReader::IsRunning() const {
    return m_pipeThread.joinable() && !m_quit;
}

void PipeReader::ConnectPipeThread() {
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    
    std::cout << "[PipeReader] Connecting to pipe: " << m_pipeName << std::endl;
    
    while (!m_quit) {
        // Wait for pipe to become available
        if (!WaitNamedPipeA(m_pipeName.c_str(), 1000)) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND && !m_quit) {
                // Pipe doesn't exist yet, wait a bit
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            } else if (error == ERROR_SEM_TIMEOUT) {
                // Timeout waiting for pipe, retry
                continue;
            } else if (m_quit) {
                break;
            } else {
                std::cerr << "[PipeReader] WaitNamedPipe error: " << error << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }
        }
        
        // Try to connect to the pipe
        hPipe = CreateFileA(
            m_pipeName.c_str(),
            GENERIC_READ,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hPipe == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error == ERROR_PIPE_BUSY) {
                std::cout << "[PipeReader] Pipe is busy, retrying..." << std::endl;
                WaitNamedPipeA(m_pipeName.c_str(), 5000);
                continue;
            }
            std::cerr << "[PipeReader] CreateFile error: " << error << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }
        
        std::cout << "[PipeReader] Connected to pipe successfully" << std::endl;
        m_connected = true;
        
        // Read data from pipe
        char buffer[4096];
        DWORD bytesRead = 0;
        
        while (!m_quit && ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            
            // Add to queue
            {
                std::lock_guard<std::mutex> queueLock(m_queueMutex);
                m_logQueue.push(std::string(buffer, bytesRead));
            }
            
            // Also add directly to accumulated logs
            {
                std::lock_guard<std::mutex> logsLock(m_logsMutex);
                m_accumulatedLogs.append(buffer, bytesRead);
            }
            
            m_queueCondVar.notify_one();
        }
        
        DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE) {
            std::cout << "[PipeReader] Pipe closed by producer" << std::endl;
        } else if (error != 0 && !m_quit) {
            std::cerr << "[PipeReader] ReadFile error: " << error << std::endl;
        }
        
        m_connected = false;
        CloseHandle(hPipe);
        hPipe = INVALID_HANDLE_VALUE;
        
        // If we're not quitting, try to reconnect
        if (!m_quit) {
            std::cout << "[PipeReader] Attempting to reconnect..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
    }
    
    std::cout << "[PipeReader] Pipe thread exiting" << std::endl;
}

} // namespace TestRunner2

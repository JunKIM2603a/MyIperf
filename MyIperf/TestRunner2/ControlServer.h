#pragma once

// Must include winsock2.h before windows.h to avoid conflicts
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#include "Protocol.h"
#include "Message.h"
#include "ProcessManager.h"
#include <string>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

namespace TestRunner2 {

// Session information for each connected client
struct Session {
    SOCKET clientSocket;
    SessionState state;
    TestConfig config;
    ProcessHandles ipeftcProcess;
    std::string processOutput;
    TestResult result;
    std::thread workerThread;
    std::atomic<bool> running;
    
    Session() : clientSocket(INVALID_SOCKET), 
                state(SessionState::IDLE),
                running(false) {}
};

class ControlServer {
public:
    explicit ControlServer(int controlPort = Protocol::DEFAULT_CONTROL_PORT,
                          const std::string& ipeftcPath = "..\\build\\Release\\IPEFTC.exe",
                          bool defaultSaveLogs = true);
    ~ControlServer();

    // Start the server (blocking)
    bool Start();
    
    // Stop the server
    void Stop();

    // Get server status
    bool IsRunning() const { return m_running; }

private:
    // Initialize Winsock
    bool InitializeWinsock();
    
    // Create and bind server socket
    bool CreateServerSocket();
    
    // Accept client connections (main loop)
    void AcceptConnections();
    
    // Handle a client session (runs in separate thread)
    void HandleSession(SOCKET clientSocket);
    
    // Send a message to client
    bool SendMessage(SOCKET socket, const std::string& message);
    
    // Receive a message from client
    bool ReceiveMessage(SOCKET socket, std::string& message, int timeoutMs = 30000);
    
    // Process CONFIG_REQUEST
    bool ProcessConfigRequest(Session& session, const std::string& message);
    
    // Process RESULTS_REQUEST
    bool ProcessResultsRequest(Session& session, const std::string& message);
    
    // Wait for IPEFTC server to complete and collect results
    bool WaitForTestCompletion(Session& session);
    
    // Print test results for server side
    void PrintServerResult(const TestResult& result, 
                          long long expectedPackets, 
                          long long expectedBytes);
    
    // Print accumulated results from all sessions
    void PrintAccumulatedResults();

    int m_controlPort;
    std::string m_ipeftcPath;
    bool m_defaultSaveLogs;
    SOCKET m_serverSocket;
    std::atomic<bool> m_running;
    ProcessManager m_processManager;
    
    // Thread management
    std::vector<std::thread> m_sessionThreads;
    std::mutex m_threadsMutex;
    
    // Accumulated test results from all sessions
    std::vector<TestResult> m_allResults;
    std::mutex m_resultsMutex;
};

} // namespace TestRunner2


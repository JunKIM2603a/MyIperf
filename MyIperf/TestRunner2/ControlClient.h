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
#include <vector>

namespace TestRunner2 {

// Result from a single port test
struct PortTestResult {
    int port;
    TestResult clientResult;
    TestResult serverResult;
    bool success;
    std::string errorMessage;
    
    PortTestResult() : port(0), success(false) {}
};

class ControlClient {
public:
    explicit ControlClient(const std::string& serverIP,
                          int controlPort = Protocol::DEFAULT_CONTROL_PORT,
                          const std::string& ipeftcPath = "..\\build\\Release\\IPEFTC.exe");
    ~ControlClient();

    // Run a single port test
    PortTestResult RunSinglePortTest(const TestConfig& config);

    // Run multiple port tests simultaneously
    std::vector<PortTestResult> RunMultiPortTest(const TestConfig& baseConfig, int numPorts);

    // Print results summary
    void PrintResults(const std::vector<PortTestResult>& results, 
                     long long expectedPackets,
                     long long expectedBytes);

private:
    // Initialize Winsock
    bool InitializeWinsock();
    
    // Connect to control server
    SOCKET ConnectToServer();
    
    // Disconnect from server
    void Disconnect(SOCKET socket);
    
    // Send a message to server
    bool SendMessage(SOCKET socket, const std::string& message);
    
    // Receive a message from server
    bool ReceiveMessage(SOCKET socket, std::string& message, int timeoutMs = 30000);
    
    // Execute a single port test (internal)
    PortTestResult ExecutePortTest(const TestConfig& config);

    std::string m_serverIP;
    int m_controlPort;
    std::string m_ipeftcPath;
    ProcessManager m_processManager;
    bool m_wsaInitialized;
};

} // namespace TestRunner2


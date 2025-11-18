#pragma once

// Must include winsock2.h before windows.h to avoid conflicts
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>

#include "Message.h"
#include <string>

namespace TestRunner2 {

// Structure to hold handles for a running process
struct ProcessHandles {
    PROCESS_INFORMATION processInfo;
    HANDLE stdOutRead;
    HANDLE stdOutWrite;

    ProcessHandles() : stdOutRead(NULL), stdOutWrite(NULL) {
        ZeroMemory(&processInfo, sizeof(PROCESS_INFORMATION));
    }
};

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    // Launch IPEFTC process in server mode
    bool LaunchIPEFTCServer(const std::string& executablePath,
                            const TestConfig& config,
                            ProcessHandles& handles);

    // Launch IPEFTC process in client mode
    bool LaunchIPEFTCClient(const std::string& executablePath,
                            const std::string& targetIP,
                            const TestConfig& config,
                            ProcessHandles& handles);

    // Wait for server to be ready (look for "Server waiting for" message)
    bool WaitForServerReady(ProcessHandles& handles, 
                           std::string& output,
                           int timeoutMs = 15000);

    // Capture complete output from a process (blocking until exit)
    std::string CaptureProcessOutput(ProcessHandles& handles);

    // Read available output without blocking
    std::string ReadAvailableOutput(ProcessHandles& handles);

    // Check if process is still running
    bool IsProcessRunning(const ProcessHandles& handles);

    // Terminate process
    void TerminateProcess(ProcessHandles& handles);

    // Close process handles
    void CloseHandles(ProcessHandles& handles);

    // Parse test summary from IPEFTC output
    TestResult ParseTestSummary(const std::string& output, 
                               const std::string& role, 
                               int port);

private:
    // Launch a generic process with stdout/stderr capture
    bool LaunchProcess(const std::string& cmdline, ProcessHandles& handles);

    // Read from pipe with timeout
    bool ReadWithTimeout(HANDLE pipeHandle, 
                        char* buffer, 
                        DWORD bufferSize,
                        DWORD& bytesRead,
                        int timeoutMs);
};

} // namespace TestRunner2


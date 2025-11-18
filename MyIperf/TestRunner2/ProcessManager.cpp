#include "ProcessManager.h"
#include <iostream>
#include <sstream>
#include <regex>
#include <chrono>
#include <thread>

namespace TestRunner2 {

ProcessManager::ProcessManager() {
}

ProcessManager::~ProcessManager() {
}

bool ProcessManager::LaunchProcess(const std::string& cmdline, ProcessHandles& handles) {
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&handles.stdOutRead, &handles.stdOutWrite, &saAttr, 0)) {
        std::cerr << "[ProcessManager] Error: CreatePipe failed" << std::endl;
        return false;
    }
    if (!SetHandleInformation(handles.stdOutRead, HANDLE_FLAG_INHERIT, 0)) {
        std::cerr << "[ProcessManager] Error: SetHandleInformation failed" << std::endl;
        return false;
    }

    STARTUPINFOA siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));
    siStartInfo.cb = sizeof(STARTUPINFOA);
    siStartInfo.hStdError = handles.stdOutWrite;
    siStartInfo.hStdOutput = handles.stdOutWrite;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    std::vector<char> cmdline_writable(cmdline.begin(), cmdline.end());
    cmdline_writable.push_back('\0');

    BOOL bSuccess = CreateProcessA(
        NULL, &cmdline_writable[0], NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, &siStartInfo, &handles.processInfo
    );

    if (!bSuccess) {
        DWORD err = GetLastError();
        CloseHandle(handles.stdOutWrite);
        CloseHandle(handles.stdOutRead);
        std::cerr << "[ProcessManager] Error: CreateProcess failed with code " << err
                  << " (cmdline: " << cmdline << ")" << std::endl;
        return false;
    }

    CloseHandle(handles.stdOutWrite); // Close the write end in the parent
    handles.stdOutWrite = NULL;
    return true;
}

bool ProcessManager::LaunchIPEFTCServer(const std::string& executablePath,
                                       const TestConfig& config,
                                       ProcessHandles& handles) {
    std::stringstream cmd;
    cmd << executablePath 
        << " --mode server"
        << " --target 0.0.0.0"
        << " --port " << config.port
        << " --save-logs " << (config.saveLogs ? "true" : "false");
    
    std::cout << "[ProcessManager] Launching IPEFTC server: " << cmd.str() << std::endl;
    return LaunchProcess(cmd.str(), handles);
}

bool ProcessManager::LaunchIPEFTCClient(const std::string& executablePath,
                                       const std::string& targetIP,
                                       const TestConfig& config,
                                       ProcessHandles& handles) {
    std::stringstream cmd;
    cmd << executablePath 
        << " --mode client"
        << " --target " << targetIP
        << " --port " << config.port
        << " --packet-size " << config.packetSize
        << " --num-packets " << config.numPackets
        << " --interval-ms " << config.sendIntervalMs
        << " --save-logs " << (config.saveLogs ? "true" : "false");
    
    std::cout << "[ProcessManager] Launching IPEFTC client: " << cmd.str() << std::endl;
    return LaunchProcess(cmd.str(), handles);
}

bool ProcessManager::WaitForServerReady(ProcessHandles& handles, 
                                       std::string& output,
                                       int timeoutMs) {
    const std::string readyMsg = "Server waiting for a client connection";
    auto startTime = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() - startTime < timeout) {
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(handles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            CHAR buffer[4096];
            DWORD bytesRead = 0;
            if (ReadFile(handles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                output.append(buffer, bytesRead);
                if (output.find(readyMsg) != std::string::npos) {
                    std::cout << "[ProcessManager] Server is ready!" << std::endl;
                    return true;
                }
            }
        }

        // Check if process exited early
        DWORD exitCode = 0;
        if (GetExitCodeProcess(handles.processInfo.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            output += "\n[ProcessManager] Server process exited early during startup "
                     "(exitCode=" + std::to_string(exitCode) + ").";
            std::cerr << "[ProcessManager] Server process exited early" << std::endl;
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cerr << "[ProcessManager] Timeout waiting for server to be ready" << std::endl;
    return false;
}

std::string ProcessManager::CaptureProcessOutput(ProcessHandles& handles) {
    std::string output;
    DWORD dwRead;
    CHAR chBuf[4096];
    
    // Continuously read output while process is running
    while (true) {
        bool dataRead = false;
        
        // Check if data is available and read it first
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(handles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            if (ReadFile(handles.stdOutRead, chBuf, sizeof(chBuf) - 1, &dwRead, NULL)) {
                chBuf[dwRead] = '\0';
                output.append(chBuf, dwRead);
                dataRead = true;
                continue;
            } else {
                break;
            }
        }
        
        // Only check exit status if no data was read
        if (!dataRead) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(handles.processInfo.hProcess, &exitCode)) {
                if (exitCode != STILL_ACTIVE) {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    // Read any remaining output after process exit
    while (true) {
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(handles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            if (ReadFile(handles.stdOutRead, chBuf, sizeof(chBuf), &dwRead, NULL) && dwRead > 0) {
                output.append(chBuf, dwRead);
            } else {
                break;
            }
        } else {
            break;
        }
    }
    
    // Ensure process has fully terminated
    WaitForSingleObject(handles.processInfo.hProcess, INFINITE);
    return output;
}

std::string ProcessManager::ReadAvailableOutput(ProcessHandles& handles) {
    std::string output;
    CHAR buffer[4096];
    DWORD bytesRead = 0;
    
    while (true) {
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(handles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            if (ReadFile(handles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                output.append(buffer, bytesRead);
            } else {
                break;
            }
        } else {
            break;
        }
    }
    
    return output;
}

bool ProcessManager::IsProcessRunning(const ProcessHandles& handles) {
    if (handles.processInfo.hProcess == NULL) {
        return false;
    }
    
    DWORD exitCode = 0;
    if (GetExitCodeProcess(handles.processInfo.hProcess, &exitCode)) {
        return exitCode == STILL_ACTIVE;
    }
    return false;
}

void ProcessManager::TerminateProcess(ProcessHandles& handles) {
    if (handles.processInfo.hProcess && IsProcessRunning(handles)) {
        std::cout << "[ProcessManager] Terminating process..." << std::endl;
        ::TerminateProcess(handles.processInfo.hProcess, 1);
        WaitForSingleObject(handles.processInfo.hProcess, 5000);
    }
}

void ProcessManager::CloseHandles(ProcessHandles& handles) {
    if (handles.processInfo.hProcess) {
        CloseHandle(handles.processInfo.hProcess);
        handles.processInfo.hProcess = NULL;
    }
    if (handles.processInfo.hThread) {
        CloseHandle(handles.processInfo.hThread);
        handles.processInfo.hThread = NULL;
    }
    if (handles.stdOutRead) {
        CloseHandle(handles.stdOutRead);
        handles.stdOutRead = NULL;
    }
    if (handles.stdOutWrite) {
        CloseHandle(handles.stdOutWrite);
        handles.stdOutWrite = NULL;
    }
}

TestResult ProcessManager::ParseTestSummary(const std::string& output, 
                                           const std::string& role, 
                                           int port) {
    TestResult result;
    result.role = role;
    result.port = port;

    if (output.empty()) {
        result.success = false;
        result.failureReason = "No output captured from process";
        return result;
    }

    std::regex summaryRegex;
    if (role == "Server") {
        summaryRegex = std::regex(
            "--- Phase 1: Client to Server ---[\\s\\S]*?Server Received:"
            "[\\s\\S]*?Total Bytes Received:\\s*(\\d+)"
            "[\\s\\S]*?Total Packets Received:\\s*(\\d+)"
            "[\\s\\S]*?Duration:\\s*([\\d\\.]+)\\s*s"
            "[\\s\\S]*?Throughput:\\s*([\\d\\.]+)\\s*Mbps"
            "[\\s\\S]*?Sequence Errors:\\s*(\\d+)"
            "[\\s\\S]*?Failed Checksums:\\s*(\\d+)"
            "[\\s\\S]*?Content Mismatches:\\s*(\\d+)"
        );
    } else { // Client
        summaryRegex = std::regex(
            "--- Phase 2: Server to Client ---[\\s\\S]*?Client Received:"
            "[\\s\\S]*?Total Bytes Received:\\s*(\\d+)"
            "[\\s\\S]*?Total Packets Received:\\s*(\\d+)"
            "[\\s\\S]*?Duration:\\s*([\\d\\.]+)\\s*s"
            "[\\s\\S]*?Throughput:\\s*([\\d\\.]+)\\s*Mbps"
            "[\\s\\S]*?Sequence Errors:\\s*(\\d+)"
            "[\\s\\S]*?Failed Checksums:\\s*(\\d+)"
            "[\\s\\S]*?Content Mismatches:\\s*(\\d+)"
        );
    }

    std::smatch matches;
    if (std::regex_search(output, matches, summaryRegex) && matches.size() == 8) {
        try {
            result.totalBytes = std::stoll(matches[1].str());
            result.totalPackets = std::stoll(matches[2].str());
            result.duration = std::stod(matches[3].str());
            result.throughput = std::stod(matches[4].str());
            result.sequenceErrors = std::stoll(matches[5].str());
            result.checksumErrors = std::stoll(matches[6].str());
            result.contentMismatches = std::stoll(matches[7].str());
            result.success = true;
        } catch (const std::exception& e) {
            result.success = false;
            result.failureReason = "Parse error while converting statistics: " + std::string(e.what());
            std::cerr << "[ProcessManager] Parse error for port " << port << " (" << role << "): " << e.what() << std::endl;
        }
    } else {
        result.success = false;
        if (output.find("FINAL TEST SUMMARY") == std::string::npos) {
            result.failureReason = "Failed to find FINAL TEST SUMMARY in output. Process may have exited before completion.";
        } else {
            result.failureReason = "Failed to match test summary regex for role " + role + ". Output format may have changed or be incomplete.";
        }
        std::cerr << "[ProcessManager] Parse warning for port " << port << " (" << role << "): " << result.failureReason << std::endl;
    }

    return result;
}

} // namespace TestRunner2


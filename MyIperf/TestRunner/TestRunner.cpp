#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <regex>
#include <map>
#include <iomanip>
#include <functional>
#include <atomic>

// Structure to hold the results from a single test run
struct TestResult {
    std::string role;
    int port = 0;
    double duration = 0.0;
    double throughput = 0.0;
    long long totalBytes = 0;
    long long totalPackets = 0;
    long long expectedBytes = 0;
    long long expectedPackets = 0;
    long long sequenceErrors = 0;
    long long checksumErrors = 0;
    long long contentMismatches = 0;
    std::string failureReason;
    bool success = false;
};

// Structure to hold handles for a running process
struct ProcessHandles {
    PROCESS_INFORMATION processInfo;
    HANDLE stdOutRead;
    HANDLE stdOutWrite;

    ProcessHandles() : stdOutRead(NULL), stdOutWrite(NULL) {
        ZeroMemory(&processInfo, sizeof(PROCESS_INFORMATION));
    }
};

// Function to launch a process without waiting for it to exit
bool LaunchProcess(const std::string& cmdline, ProcessHandles& handles) {
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&handles.stdOutRead, &handles.stdOutWrite, &saAttr, 0)) {
        std::cerr << "Error: CreatePipe failed" << std::endl;
        return false;
    }
    if (!SetHandleInformation(handles.stdOutRead, HANDLE_FLAG_INHERIT, 0)) {
        std::cerr << "Error: SetHandleInformation failed" << std::endl;
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
        std::cerr << "Error: CreateProcess failed with code " << err
                  << " (cmdline: " << cmdline << ")" << std::endl;
        return false;
    }

    CloseHandle(handles.stdOutWrite); // Close the write end in the parent
    return true;
}

// Function to clean up process handles
void CloseProcessHandles(ProcessHandles& handles) {
    if (handles.processInfo.hProcess) CloseHandle(handles.processInfo.hProcess);
    if (handles.processInfo.hThread) CloseHandle(handles.processInfo.hThread);
    if (handles.stdOutRead) CloseHandle(handles.stdOutRead);
}

// Synchronous function to run a process and capture its full output
std::string ExecuteProcessAndCaptureOutput(const std::string& cmdline) {
    ProcessHandles handles;
    if (!LaunchProcess(cmdline, handles)) {
        return "Error: Failed to launch process.";
    }
    
    std::string output;
    DWORD dwRead;
    CHAR chBuf[4096];
    
    // Continuously read output while process is running
    while (true) {
        bool dataRead = false;
        
        // PRIORITY 1: Check if data is available and read it first
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(handles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            if (ReadFile(handles.stdOutRead, chBuf, sizeof(chBuf) - 1, &dwRead, NULL)) {
                chBuf[dwRead] = '\0';
                output.append(chBuf, dwRead);
                dataRead = true;
                // Continue to next iteration to check for more data
                continue;
            } else {
                // Read failed, process might have closed the pipe
                break;
            }
        }
        
        // PRIORITY 2: Only check exit status if no data was read
        if (!dataRead) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(handles.processInfo.hProcess, &exitCode)) {
                if (exitCode != STILL_ACTIVE) {
                    // Process has exited and no data available, break to read remaining output
                    break;
                }
            }
            // No data available and process still running, wait a bit before checking again
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
    CloseProcessHandles(handles);
    return output;
}


// Function to parse the "FINAL TEST SUMMARY" from the output
TestResult ParseTestSummary(const std::string& output, const std::string& role, int port) {
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
            std::cerr << "Parse error for port " << port << " (" << role << "): " << e.what() << std::endl;
        }
    } else {
        result.success = false;
        if (output.find("FINAL TEST SUMMARY") == std::string::npos) {
            if (output.find("[TestRunner] Server timed out") != std::string::npos) {
                 result.failureReason = "Server process timed out in TestRunner before FINAL TEST SUMMARY was printed.";
            } else {
                 result.failureReason = "Failed to find FINAL TEST SUMMARY in output. Process may have exited before completion.";
            }
        } else {
             result.failureReason = "Failed to match test summary regex for role " + role + ". Output format may have changed or be incomplete.";
        }
        std::cerr << "Parse warning for port " << port << " (" << role << "): " << result.failureReason << std::endl;
    }

    return result;
}


void PrintResults(std::vector<TestResult>& results, long long expectedPackets, long long expectedBytes) {
    std::cout << "\n--- FINAL TEST SUMMARY ---" << std::endl;
    std::cout << std::left << std::setw(8) << "Role"
              << std::setw(8) << "Port"
              << std::setw(15) << "Duration (s)"
              << std::setw(18) << "Throughput (Mbps)"
              << std::setw(22) << "Total Bytes Rx"
              << std::setw(24) << "Total Packets Rx"
              << std::setw(10) << "Status" << std::endl;
    std::cout << std::string(104, '-') << std::endl;

    bool all_ok = true;
    for (auto& res : results) {
        // Set expected values for validation
        res.expectedBytes = expectedBytes;
        res.expectedPackets = expectedPackets;
        
        // Apply same validation logic to both Server and Client
        bool packets_match = (res.totalPackets == expectedPackets);
        bool bytes_match = (res.totalBytes == expectedBytes);
        bool no_errors = (res.sequenceErrors == 0 && res.checksumErrors == 0 && res.contentMismatches == 0);
        bool pass = res.success && packets_match && bytes_match && no_errors;
        
        // Build detailed failure reason if validation fails
        if (!pass && res.success) {
            std::ostringstream failure;
            bool first = true;
            if (!packets_match) {
                failure << "Expected " << expectedPackets << " packets, got " << res.totalPackets;
                first = false;
            }
            if (!bytes_match) {
                if (!first) failure << ". ";
                failure << "Expected " << expectedBytes << " bytes, got " << res.totalBytes;
                first = false;
            }
            if (!no_errors) {
                if (!first) failure << ". ";
                failure << "Errors: ";
                bool errorFirst = true;
                if (res.sequenceErrors > 0) {
                    failure << "Sequence errors: " << res.sequenceErrors;
                    errorFirst = false;
                }
                if (res.checksumErrors > 0) {
                    if (!errorFirst) failure << ", ";
                    failure << "Checksum errors: " << res.checksumErrors;
                    errorFirst = false;
                }
                if (res.contentMismatches > 0) {
                    if (!errorFirst) failure << ", ";
                    failure << "Content mismatches: " << res.contentMismatches;
                }
            }
            res.failureReason = failure.str();
        }

        if (!pass) all_ok = false;

        std::cout << std::left << std::setw(8) << res.role
                  << std::setw(8) << res.port
                  << std::setw(15) << std::fixed << std::setprecision(2) << res.duration
                  << std::setw(18) << res.throughput
                  << std::setw(22) << res.totalBytes
                  << std::setw(24) << res.totalPackets
                  << std::setw(10) << (pass ? "PASS" : "FAIL") << std::endl;
        
        // Print detailed failure information
        if (!pass && !res.failureReason.empty()) {
            std::cout << "  -> " << res.failureReason << std::endl;
        }
    }
    
    if (!all_ok) {
        std::cout << "\nWARNING: One or more tests failed or did not match expected values." << std::endl;
    }
}


int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: " << argv[0] << " <repetitions> <packet_size> <num_packets> <interval_ms> <--save-logs>" << std::endl;
        return 1;
    }

    int repetitions = std::stoi(argv[1]);
    long long packetSize = std::stoll(argv[2]);
    long long numPackets = std::stoll(argv[3]);
    int intervalMs = std::stoi(argv[4]);
    std::string saveLogs = argv[5];

    if (numPackets == 0) {
        std::cerr << "Error: TestRunner does not support numPackets==0 (infinite mode)." << std::endl;
        return 1;
    }

    std::string executable = "..\\build\\Release\\IPEFTC.exe";
    std::string serverIp = "0.0.0.0";
    std::string clientTargetIp = "127.0.0.1";
    std::vector<int> ports = {60000, 60001, 60002, 60003, 60004};
    // std::vector<int> ports = {60000};

    std::cout << "--- Test Parameters ---" << std::endl;
    std::cout << "Repetitions: " << repetitions << std::endl;
    std::cout << "Packet Size: " << packetSize << " bytes" << std::endl;
    std::cout << "Packets to Send: " << numPackets << std::endl;
    std::cout << "Interval: " << intervalMs << " ms" << std::endl;
    std::cout << std::endl;

    std::vector<TestResult> total_run_results;
    for (int i = 1; i <= repetitions; ++i) {
        std::cout << "=================================================" << std::endl;
        time_t now = time(0);
        tm localTime;
        localtime_s(&localTime, &now);
        std::cout << localTime.tm_mon + 1 << "/" << localTime.tm_mday << "/" << localTime.tm_year + 1900 << " "
                  << localTime.tm_hour << ":" << localTime.tm_min << ":" << localTime.tm_sec << std::endl;
        std::cout << "--- Starting Iteration " << i << " of " << repetitions << " ---" << std::endl;
        std::cout << "=================================================" << std::endl;

        std::vector<std::thread> threads;
        std::vector<std::string> outputs(ports.size() * 2);

        for (size_t j = 0; j < ports.size(); ++j) {
            threads.emplace_back([j, &ports, &outputs, packetSize, numPackets, intervalMs, &executable, &serverIp, &clientTargetIp, &saveLogs]() {
                int port = ports[j];
                
                // 1. Launch Server
                std::stringstream serverCmd;
                serverCmd << executable << " --mode server --target " << serverIp << " --port " << port << " --save-logs " << saveLogs;
                
                ProcessHandles serverHandles;
                if (!LaunchProcess(serverCmd.str(), serverHandles)) {
                    outputs[j] = "Error: Failed to launch server on port " + std::to_string(port);
                    return;
                }

                // 2. Wait for server to be ready
                std::string serverOutput;
                const std::string readyMsg = "Server waiting for a client connection";
                bool serverReady = false;
                auto startTime = std::chrono::steady_clock::now();
                const auto timeout = std::chrono::seconds(10);

                while (std::chrono::steady_clock::now() - startTime < timeout) {
                    DWORD bytesAvailable = 0;
                    if (PeekNamedPipe(serverHandles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
                        CHAR buffer[4096];
                        DWORD bytesRead = 0;
                        if (ReadFile(serverHandles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                            buffer[bytesRead] = '\0';
                            serverOutput.append(buffer, bytesRead);
                            if (serverOutput.find(readyMsg) != std::string::npos) {
                                serverReady = true;
                                break;
                            }
                        }
                    }

                    DWORD exitCode = 0;
                    if (GetExitCodeProcess(serverHandles.processInfo.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                        serverOutput += "\n[TestRunner] Server process exited early during startup "
                                        "(exitCode=" + std::to_string(exitCode) + ").";
                        CloseProcessHandles(serverHandles);
                        outputs[j] = serverOutput;
                        return;
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                if (!serverReady) {
                    TerminateProcess(serverHandles.processInfo.hProcess, 1);
                    CloseProcessHandles(serverHandles);
                    outputs[j] = "Error: Server on port " + std::to_string(port) + " timed out.";
                    return;
                }

                // 3. Launch Client
                std::stringstream clientCmd;
                clientCmd << executable << " --mode client --target " << clientTargetIp << " --port " << port
                          << " --packet-size " << packetSize << " --num-packets " << numPackets
                          << " --interval-ms " << intervalMs << " --save-logs " << saveLogs;
                
                std::string clientOutput = ExecuteProcessAndCaptureOutput(clientCmd.str());
                outputs[ports.size() + j] = clientOutput;
                
                // 4. Continuously read server output while waiting for it to finish
                // This ensures we capture all output and don't miss any logs
                const std::string completionMsg = "IPEFTC application finished";
                bool serverFinished = false;
                auto serverStartTime = std::chrono::steady_clock::now();

                // Dynamically estimate a reasonable timeout based on numPackets and intervalMs.
                // This keeps long-running tests from being killed prematurely while still
                // guarding against hangs. We keep the original 30s as a minimum.
                double serverTimeoutSec = 30.0;
                if (intervalMs > 0 && numPackets > 0) {
                    // Rough estimate: two phases each sending numPackets at intervalMs plus a handshake margin.
                    double estimatedPhaseSec = static_cast<double>(numPackets) * static_cast<double>(intervalMs) / 1000.0;
                    double estimatedTotalSec = (estimatedPhaseSec * 2.0) + 10.0; // +10s handshake/overhead
                    // Clamp to a sane range: at least 30s, at most 10 minutes.
                    if (estimatedTotalSec > serverTimeoutSec) {
                        serverTimeoutSec = (std::min)(estimatedTotalSec, 600.0);
                    }
                }
                const auto serverTimeout = std::chrono::milliseconds(static_cast<long long>(serverTimeoutSec * 1000.0));
                
                while (!serverFinished && 
                       (std::chrono::steady_clock::now() - serverStartTime < serverTimeout)) {
                    DWORD bytesAvailable = 0;
                    if (PeekNamedPipe(serverHandles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
                        CHAR buffer[4096];
                        DWORD bytesRead = 0;
                        if (ReadFile(serverHandles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                            buffer[bytesRead] = '\0';
                            serverOutput.append(buffer, bytesRead);
                            // Check if server has finished
                            if (serverOutput.find(completionMsg) != std::string::npos) {
                                serverFinished = true;
                            }
                        }
                    }
                    
                    // Check if process has exited early (e.g., due to an error) while we are waiting
                    DWORD exitCode = 0;
                    if (GetExitCodeProcess(serverHandles.processInfo.hProcess, &exitCode)) {
                        if (exitCode != STILL_ACTIVE) {
                            serverFinished = true;
                            std::cout << "[TestRunner] Detected server process has exited early. iteration: " << j << " port: " << port << " exitCode: " << exitCode << std::endl;
                            break;
                        }
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                // Wait a bit more to ensure all output is flushed
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                // Read any remaining output
                CHAR buffer[4096];
                DWORD bytesRead = 0;
                while (PeekNamedPipe(serverHandles.stdOutRead, NULL, 0, NULL, &bytesRead, NULL) && bytesRead > 0) {
                    if (ReadFile(serverHandles.stdOutRead, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
                        serverOutput.append(buffer, bytesRead);
                    } else {
                        break;
                    }
                }
                
                // If server didn't finish naturally within the timeout, terminate it
                DWORD exitCode = 0;
                if (GetExitCodeProcess(serverHandles.processInfo.hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
                    TerminateProcess(serverHandles.processInfo.hProcess, 1);
                    serverOutput += "\n[TestRunner] Server timed out in TestRunner (timeout="
                                 + std::to_string(static_cast<int>(serverTimeoutSec))
                                 + "s) and was forcefully terminated.";
                }
                
                CloseProcessHandles(serverHandles);
                
                // Provide time for the process to be fully cleaned up
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                
                outputs[j] = serverOutput;
            });
        }


        for (auto& th : threads) {
            th.join();
        }
        
        std::cout << "All processes for iteration " << i << " have completed." << std::endl;
        
        std::vector<TestResult> all_results;
        for (size_t j = 0; j < ports.size(); ++j) {
            all_results.push_back(ParseTestSummary(outputs[j], "Server", ports[j]));
            all_results.push_back(ParseTestSummary(outputs[ports.size() + j], "Client", ports[j]));
        }
        
        long long expectedBytes = packetSize * numPackets;
        PrintResults(all_results, numPackets, expectedBytes);
        total_run_results.insert(total_run_results.end(), all_results.begin(), all_results.end());
        
        // Wait sufficiently before the next iteration to ensure ports/resources are fully released
        if (i < repetitions) {
            std::cout << "Waiting for resources to be fully released before next iteration..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
        
        std::cout << std::endl;
    }

    // Total results summary
    if (repetitions > 1 && !total_run_results.empty()) {
        std::cout << "=================================================" << std::endl;
        std::cout << "--- TOTAL RESULTS ACROSS ALL ITERATIONS ---" << std::endl;
        
        size_t total_tests = total_run_results.size();
        size_t total_passes = 0;
        
        for (const auto& res : total_run_results) {
            bool packets_match = (res.totalPackets == res.expectedPackets);
            bool bytes_match = (res.totalBytes == res.expectedBytes);
            bool no_errors = (res.sequenceErrors == 0 && res.checksumErrors == 0 && res.contentMismatches == 0);
            if (res.success && packets_match && bytes_match && no_errors) {
                total_passes++;
            }
        }
        
        size_t total_fails = total_tests - total_passes;
        
        std::cout << "Total Tests Run: " << total_tests << std::endl;
        std::cout << "  - Passed: " << total_passes << std::endl;
        std::cout << "  - Failed: " << total_fails << std::endl;
        
        if (total_fails > 0) {
            std::cout << "\nWARNING: Some tests failed across the total run." << std::endl;
        } else {
            std::cout << "\nSUCCESS: All tests passed across all iterations." << std::endl;
        }
    }
    

    std::cout << "=================================================" << std::endl;
    std::cout << "All test iterations completed." << std::endl;
    std::cout << "=================================================" << std::endl;

    return 0;
}
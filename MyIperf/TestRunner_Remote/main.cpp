#include "ControlServer.h"
#include "ControlClient.h"
#include <iostream>
#include <string>
#include <map>
#include <windows.h>
#include <sstream>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <iomanip>
#include "../nlohmann/json.hpp"

using namespace TestRunner2;
using json = nlohmann::json;

// Structure to store run results
struct RunResult {
    int runNumber;
    int totalRuns;
    bool success;
    int exitCode;
    std::vector<PortTestResult> portResults;
    
    RunResult() : runNumber(0), totalRuns(0), success(false), exitCode(0) {}
};

// Convert TestResult to JSON
json TestResultToJson(const TestResult& result) {
    json j;
    j["role"] = result.role;
    j["port"] = result.port;
    j["duration"] = result.duration;
    j["throughput"] = result.throughput;
    j["totalBytes"] = result.totalBytes;
    j["totalPackets"] = result.totalPackets;
    j["expectedBytes"] = result.expectedBytes;
    j["expectedPackets"] = result.expectedPackets;
    j["sequenceErrors"] = result.sequenceErrors;
    j["checksumErrors"] = result.checksumErrors;
    j["contentMismatches"] = result.contentMismatches;
    j["failureReason"] = result.failureReason;
    j["success"] = result.success;
    return j;
}

// Convert PortTestResult to JSON
json PortTestResultToJson(const PortTestResult& result) {
    json j;
    j["port"] = result.port;
    j["success"] = result.success;
    j["errorMessage"] = result.errorMessage;
    j["clientResult"] = TestResultToJson(result.clientResult);
    j["serverResult"] = TestResultToJson(result.serverResult);
    return j;
}

// Convert RunResult to JSON
json RunResultToJson(const RunResult& runResult) {
    json j;
    j["runNumber"] = runResult.runNumber;
    j["totalRuns"] = runResult.totalRuns;
    j["success"] = runResult.success;
    j["exitCode"] = runResult.exitCode;
    j["portResults"] = json::array();
    for (const auto& portResult : runResult.portResults) {
        j["portResults"].push_back(PortTestResultToJson(portResult));
    }
    return j;
}

// Convert JSON to TestResult
TestResult JsonToTestResult(const json& j) {
    TestResult result;
    result.role = j.value("role", "");
    result.port = j.value("port", 0);
    result.duration = j.value("duration", 0.0);
    result.throughput = j.value("throughput", 0.0);
    result.totalBytes = j.value("totalBytes", 0LL);
    result.totalPackets = j.value("totalPackets", 0LL);
    result.expectedBytes = j.value("expectedBytes", 0LL);
    result.expectedPackets = j.value("expectedPackets", 0LL);
    result.sequenceErrors = j.value("sequenceErrors", 0LL);
    result.checksumErrors = j.value("checksumErrors", 0LL);
    result.contentMismatches = j.value("contentMismatches", 0LL);
    result.failureReason = j.value("failureReason", "");
    result.success = j.value("success", false);
    return result;
}

// Convert JSON to PortTestResult
PortTestResult JsonToPortTestResult(const json& j) {
    PortTestResult result;
    result.port = j.value("port", 0);
    result.success = j.value("success", false);
    result.errorMessage = j.value("errorMessage", "");
    if (j.contains("clientResult")) {
        result.clientResult = JsonToTestResult(j["clientResult"]);
    }
    if (j.contains("serverResult")) {
        result.serverResult = JsonToTestResult(j["serverResult"]);
    }
    return result;
}

// Convert JSON to RunResult
RunResult JsonToRunResult(const json& j) {
    RunResult result;
    result.runNumber = j.value("runNumber", 0);
    result.totalRuns = j.value("totalRuns", 0);
    result.success = j.value("success", false);
    result.exitCode = j.value("exitCode", 0);
    if (j.contains("portResults") && j["portResults"].is_array()) {
        for (const auto& portJson : j["portResults"]) {
            result.portResults.push_back(JsonToPortTestResult(portJson));
        }
    }
    return result;
}

// Get the directory where the executable is located
std::string GetExecutableDirectory() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string path(exePath);
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return path.substr(0, lastSlash + 1);
    }
    return "";
}

// Save run result to JSON file
bool SaveRunResult(const RunResult& runResult) {
    std::string dir = GetExecutableDirectory();
    std::ostringstream filename;
    filename << dir << "TestRunner2_run_" << runResult.runNumber << ".json";
    
    try {
        json j = RunResultToJson(runResult);
        std::ofstream file(filename.str());
        if (!file.is_open()) {
            std::cerr << "[TestRunner2] Failed to open file for writing: " << filename.str() << std::endl;
            return false;
        }
        file << j.dump(2);  // Pretty print with 2-space indent
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[TestRunner2] Error saving run result: " << e.what() << std::endl;
        return false;
    }
}

// Load all run results from JSON files
std::vector<RunResult> LoadAllRunResults(int totalRuns) {
    std::vector<RunResult> allResults;
    std::string dir = GetExecutableDirectory();
    
    for (int runNum = 1; runNum <= totalRuns; runNum++) {
        std::ostringstream filename;
        filename << dir << "TestRunner2_run_" << runNum << ".json";
        
        try {
            std::ifstream file(filename.str());
            if (!file.is_open()) {
                // File doesn't exist, skip it
                continue;
            }
            
            std::string content((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
            file.close();
            
            json j = json::parse(content);
            RunResult result = JsonToRunResult(j);
            allResults.push_back(result);
        } catch (const std::exception& e) {
            std::cerr << "[TestRunner2] Error loading run result from " << filename.str() 
                      << ": " << e.what() << std::endl;
            // Continue loading other files
        }
    }
    
    return allResults;
}

// Print comprehensive summary of all runs
void PrintComprehensiveSummary(const std::vector<RunResult>& allResults, int totalRuns) {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "COMPREHENSIVE TEST SUMMARY" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    if (allResults.empty()) {
        std::cout << "No run results found." << std::endl;
        return;
    }
    
    // Calculate statistics
    int totalRunsFound = static_cast<int>(allResults.size());
    int successfulRuns = 0;
    int failedRuns = 0;
    std::vector<int> failedRunNumbers;
    
    for (const auto& result : allResults) {
        if (result.success) {
            successfulRuns++;
        } else {
            failedRuns++;
            failedRunNumbers.push_back(result.runNumber);
        }
    }
    
    // Print overall statistics
    std::cout << "\n--- Overall Statistics ---" << std::endl;
    std::cout << "Total Runs: " << totalRuns << std::endl;
    std::cout << "Runs Completed: " << totalRunsFound << std::endl;
    std::cout << "Successful Runs: " << successfulRuns << std::endl;
    std::cout << "Failed Runs: " << failedRuns << std::endl;
    
    if (totalRunsFound < totalRuns) {
        std::cout << "Warning: " << (totalRuns - totalRunsFound) << " run(s) not found." << std::endl;
    }
    
    // Print success rate
    if (totalRunsFound > 0) {
        double successRate = (static_cast<double>(successfulRuns) / totalRunsFound) * 100.0;
        std::cout << "Success Rate: " << std::fixed << std::setprecision(2) << successRate << "%" << std::endl;
    }
    
    // Print detailed failure information
    if (failedRuns > 0) {
        std::cout << "\n--- Failed Runs Details ---" << std::endl;
        
        for (const auto& result : allResults) {
            if (!result.success) {
                std::cout << "\nRun " << result.runNumber << " (Exit Code: " << result.exitCode << "):" << std::endl;
                
                if (result.portResults.empty()) {
                    std::cout << "  No port results available." << std::endl;
                    continue;
                }
                
                for (const auto& portResult : result.portResults) {
                    std::cout << "  Port " << portResult.port << ":" << std::endl;
                    
                    if (!portResult.errorMessage.empty()) {
                        std::cout << "    Error: " << portResult.errorMessage << std::endl;
                    }
                    
                    if (!portResult.success) {
                        std::cout << "    Overall: FAILED" << std::endl;
                    }
                    
                    // Client result details
                    if (!portResult.clientResult.success) {
                        std::cout << "    Client Result:" << std::endl;
                        std::cout << "      Success: FAILED" << std::endl;
                        if (!portResult.clientResult.failureReason.empty()) {
                            std::cout << "      Reason: " << portResult.clientResult.failureReason << std::endl;
                        }
                        std::cout << "      Bytes Received: " << portResult.clientResult.totalBytes 
                                  << " / Expected: " << portResult.clientResult.expectedBytes << std::endl;
                        std::cout << "      Packets Received: " << portResult.clientResult.totalPackets 
                                  << " / Expected: " << portResult.clientResult.expectedPackets << std::endl;
                        if (portResult.clientResult.sequenceErrors > 0) {
                            std::cout << "      Sequence Errors: " << portResult.clientResult.sequenceErrors << std::endl;
                        }
                        if (portResult.clientResult.checksumErrors > 0) {
                            std::cout << "      Checksum Errors: " << portResult.clientResult.checksumErrors << std::endl;
                        }
                        if (portResult.clientResult.contentMismatches > 0) {
                            std::cout << "      Content Mismatches: " << portResult.clientResult.contentMismatches << std::endl;
                        }
                    }
                    
                    // Server result details
                    if (!portResult.serverResult.success) {
                        std::cout << "    Server Result:" << std::endl;
                        std::cout << "      Success: FAILED" << std::endl;
                        if (!portResult.serverResult.failureReason.empty()) {
                            std::cout << "      Reason: " << portResult.serverResult.failureReason << std::endl;
                        }
                        std::cout << "      Bytes Received: " << portResult.serverResult.totalBytes 
                                  << " / Expected: " << portResult.serverResult.expectedBytes << std::endl;
                        std::cout << "      Packets Received: " << portResult.serverResult.totalPackets 
                                  << " / Expected: " << portResult.serverResult.expectedPackets << std::endl;
                        if (portResult.serverResult.sequenceErrors > 0) {
                            std::cout << "      Sequence Errors: " << portResult.serverResult.sequenceErrors << std::endl;
                        }
                        if (portResult.serverResult.checksumErrors > 0) {
                            std::cout << "      Checksum Errors: " << portResult.serverResult.checksumErrors << std::endl;
                        }
                        if (portResult.serverResult.contentMismatches > 0) {
                            std::cout << "      Content Mismatches: " << portResult.serverResult.contentMismatches << std::endl;
                        }
                    }
                    
                    // Check for data mismatches even if success flag is true
                    bool hasDataMismatch = false;
                    if (portResult.clientResult.totalBytes != portResult.clientResult.expectedBytes ||
                        portResult.clientResult.totalPackets != portResult.clientResult.expectedPackets) {
                        hasDataMismatch = true;
                        std::cout << "    Client Data Mismatch:" << std::endl;
                        std::cout << "      Received: " << portResult.clientResult.totalBytes << " bytes, " 
                                  << portResult.clientResult.totalPackets << " packets" << std::endl;
                        std::cout << "      Expected: " << portResult.clientResult.expectedBytes << " bytes, " 
                                  << portResult.clientResult.expectedPackets << " packets" << std::endl;
                    }
                    if (portResult.serverResult.totalBytes != portResult.serverResult.expectedBytes ||
                        portResult.serverResult.totalPackets != portResult.serverResult.expectedPackets) {
                        hasDataMismatch = true;
                        std::cout << "    Server Data Mismatch:" << std::endl;
                        std::cout << "      Received: " << portResult.serverResult.totalBytes << " bytes, " 
                                  << portResult.serverResult.totalPackets << " packets" << std::endl;
                        std::cout << "      Expected: " << portResult.serverResult.expectedBytes << " bytes, " 
                                  << portResult.serverResult.expectedPackets << " packets" << std::endl;
                    }
                }
            }
        }
    } else {
        std::cout << "\n--- All Runs Passed Successfully ---" << std::endl;
    }
    
    std::cout << "\n==================================================" << std::endl;
}

void PrintUsage(const char* programName) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "TestRunner2 - Distributed IPEFTC Tester" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << programName << " --mode <server|client> [options]\n" << std::endl;
    
    std::cout << "Server Mode:" << std::endl;
    std::cout << "  " << programName << " --mode server [options]" << std::endl;
    std::cout << "    --control-port <port>    Control port (default: 9000)" << std::endl;
    std::cout << "    --ipeftc-path <path>     Path to IPEFTC.exe (default: ..\\build\\Release\\IPEFTC.exe)" << std::endl;
    std::cout << "    --save-logs <true|false> Save IPEFTC server logs (default: true)\n" << std::endl;
    
    std::cout << "Client Mode (Single Port):" << std::endl;
    std::cout << "  " << programName << " --mode client --server <IP> [options]" << std::endl;
    std::cout << "    --server <IP>            Server IP address (required)" << std::endl;
    std::cout << "    --control-port <port>    Control port (default: 9000)" << std::endl;
    std::cout << "    --test-port <port>       IPEFTC test port (default: 60000)" << std::endl;
    std::cout << "    --packet-size <bytes>    Packet size (default: 8192)" << std::endl;
    std::cout << "    --num-packets <count>    Number of packets (default: 10000)" << std::endl;
    std::cout << "    --interval-ms <ms>       Send interval in ms (default: 0)" << std::endl;
    std::cout << "    --save-logs <true|false> Save logs (default: true)" << std::endl;
    std::cout << "    --ipeftc-path <path>     Path to IPEFTC.exe (default: ..\\build\\Release\\IPEFTC.exe)" << std::endl;
    std::cout << "    --total-runs <count>     Total number of test runs (default: 1)" << std::endl;
    std::cout << "                            Program will restart after each run for stability\n" << std::endl;
    
    std::cout << "Client Mode (Multi Port):" << std::endl;
    std::cout << "  " << programName << " --mode client --server <IP> --num-ports <N> [options]" << std::endl;
    std::cout << "    --num-ports <N>          Number of ports to test simultaneously" << std::endl;
    std::cout << "    (All single port options apply, ports start from --test-port)\n" << std::endl;
    
    std::cout << "Examples:" << std::endl;
    std::cout << "  Server:" << std::endl;
    std::cout << "    " << programName << " --mode server --control-port 9000" << std::endl;
    std::cout << "    " << programName << " --mode server --control-port 9000 --save-logs false\n" << std::endl;
    std::cout << "  Client (Single Port):" << std::endl;
    std::cout << "    " << programName << " --mode client --server 192.168.1.100 --test-port 60000 --num-packets 10000\n" << std::endl;
    std::cout << "  Client (Multi Port):" << std::endl;
    std::cout << "    " << programName << " --mode client --server 192.168.1.100 --num-ports 5 --test-port 60000\n" << std::endl;
    std::cout << "  Client (Multiple Runs):" << std::endl;
    std::cout << "    " << programName << " --mode client --server 192.168.1.100 --total-runs 5\n" << std::endl;
}

std::map<std::string, std::string> ParseArguments(int argc, char* argv[]) {
    std::map<std::string, std::string> args;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.substr(0, 2) == "--" && i + 1 < argc) {
            std::string key = arg.substr(2);
            std::string value = argv[i + 1];
            args[key] = value;
            i++; // Skip next argument as it's the value
        }
    }
    
    return args;
}

int RunServerMode(const std::map<std::string, std::string>& args) {
    int controlPort = Protocol::DEFAULT_CONTROL_PORT;
    std::string ipeftcPath = "..\\build\\Release\\IPEFTC.exe";
    bool saveLogs = true;
    
    if (args.count("control-port")) {
        controlPort = std::stoi(args.at("control-port"));
    }
    if (args.count("ipeftc-path")) {
        ipeftcPath = args.at("ipeftc-path");
    }
    if (args.count("save-logs")) {
        std::string saveLogsStr = args.at("save-logs");
        saveLogs = (saveLogsStr == "true" || saveLogsStr == "1");
    }
    
    std::cout << "\n==================================================" << std::endl;
    std::cout << "Starting TestRunner2 Server" << std::endl;
    std::cout << "Control Port: " << controlPort << std::endl;
    std::cout << "IPEFTC Path: " << ipeftcPath << std::endl;
    std::cout << "Save Logs: " << (saveLogs ? "true" : "false") << std::endl;
    std::cout << "==================================================" << std::endl;
    
    ControlServer server(controlPort, ipeftcPath, saveLogs);
    
    if (!server.Start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    return 0;
}

int RunClientMode(const std::map<std::string, std::string>& args) {
    // Check required argument
    if (!args.count("server")) {
        std::cerr << "Error: --server <IP> is required for client mode" << std::endl;
        return 1;
    }
    
    std::string serverIP = args.at("server");
    int controlPort = Protocol::DEFAULT_CONTROL_PORT;
    std::string ipeftcPath = "..\\build\\Release\\IPEFTC.exe";
    
    // Test configuration
    TestConfig config;
    config.port = Protocol::DEFAULT_TEST_PORT;
    config.packetSize = 8192;
    config.numPackets = 10000;
    config.sendIntervalMs = 0;
    config.protocol = "TCP";
    config.saveLogs = true;
    
    int numPorts = 1;  // Default: single port
    int totalRuns = 1;  // Default: single run
    
    // Parse optional arguments
    if (args.count("control-port")) {
        controlPort = std::stoi(args.at("control-port"));
    }
    if (args.count("ipeftc-path")) {
        ipeftcPath = args.at("ipeftc-path");
    }
    if (args.count("test-port")) {
        config.port = std::stoi(args.at("test-port"));
    }
    if (args.count("packet-size")) {
        config.packetSize = std::stoi(args.at("packet-size"));
    }
    if (args.count("num-packets")) {
        config.numPackets = std::stoll(args.at("num-packets"));
    }
    if (args.count("interval-ms")) {
        config.sendIntervalMs = std::stoi(args.at("interval-ms"));
    }
    if (args.count("save-logs")) {
        std::string saveLogsStr = args.at("save-logs");
        config.saveLogs = (saveLogsStr == "true" || saveLogsStr == "1");
    }
    if (args.count("num-ports")) {
        numPorts = std::stoi(args.at("num-ports"));
        if (numPorts < 1) {
            std::cerr << "Error: --num-ports must be at least 1" << std::endl;
            return 1;
        }
    }
    if (args.count("total-runs")) {
        totalRuns = std::stoi(args.at("total-runs"));
        if (totalRuns < 1) {
            std::cerr << "Error: --total-runs must be at least 1" << std::endl;
            return 1;
        }
    }
    
    // Get current run number from environment variable (set by parent process)
    int currentRun = 1;
    char buffer[32] = {0};
    size_t requiredSize = 0;
    if (getenv_s(&requiredSize, buffer, sizeof(buffer), "TESTRUNNER_CURRENT_RUN") == 0 && requiredSize > 0) {
        currentRun = std::stoi(buffer);
    }
    
    std::cout << "\n==================================================" << std::endl;
    std::cout << "Test Run " << currentRun << " of " << totalRuns << std::endl;
    std::cout << "==================================================" << std::endl;
    
    ControlClient client(serverIP, controlPort, ipeftcPath);
    
    std::vector<PortTestResult> results;
    
    if (numPorts == 1) {
        // Single port test
        PortTestResult result = client.RunSinglePortTest(config);
        results.push_back(result);
    } else {
        // Multi port test
        results = client.RunMultiPortTest(config, numPorts);
    }
    
    // Print results
    long long expectedBytes = config.packetSize * config.numPackets;
    client.PrintResults(results, config.numPackets, expectedBytes);
    
    // Check if all tests passed
    bool allPassed = true;
    for (const auto& result : results) {
        if (!result.success || !result.clientResult.success || !result.serverResult.success) {
            allPassed = false;
            break;
        }
    }
    
    int exitCode = allPassed ? 0 : 1;
    
    // Save current run result to file
    RunResult runResult;
    runResult.runNumber = currentRun;
    runResult.totalRuns = totalRuns;
    runResult.success = allPassed;
    runResult.exitCode = exitCode;
    runResult.portResults = results;
    
    SaveRunResult(runResult);
    
    // ControlClient destructor will be called here, releasing all resources
    
    // If there are more runs to do, restart the program
    if (currentRun < totalRuns) {
        std::cout << "\n==================================================" << std::endl;
        std::cout << "Run " << currentRun << " completed. Restarting for run " << (currentRun + 1) << "..." << std::endl;
        std::cout << "Waiting for resources to be released..." << std::endl;
        std::cout << "==================================================" << std::endl;
        
        // Wait a bit to ensure all resources are released
        Sleep(1000);
        
        // Get the executable path
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        
        // Reconstruct command line arguments
        std::ostringstream cmdLine;
        cmdLine << "\"" << exePath << "\"";
        
        // Rebuild all arguments
        for (const auto& arg : args) {
            cmdLine << " --" << arg.first << " " << arg.second;
        }
        
        // Set environment variable for next run
        std::ostringstream envVar;
        envVar << "TESTRUNNER_CURRENT_RUN=" << (currentRun + 1);
        _putenv(envVar.str().c_str());
        
        // Start new process
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        
        std::string cmdLineStr = cmdLine.str();
        char* cmdLineCStr = new char[cmdLineStr.length() + 1];
        strcpy_s(cmdLineCStr, cmdLineStr.length() + 1, cmdLineStr.c_str());
        
        if (CreateProcessA(
            NULL,           // Application name (use command line instead)
            cmdLineCStr,    // Command line
            NULL,           // Process security attributes
            NULL,           // Thread security attributes
            FALSE,          // Inherit handles
            0,              // Creation flags
            NULL,           // Environment (inherit)
            NULL,           // Current directory (inherit)
            &si,            // Startup info
            &pi             // Process information
        )) {
            std::cout << "[TestRunner2] Started new process for run " << (currentRun + 1) << std::endl;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            std::cerr << "[TestRunner2] Failed to start new process: " << GetLastError() << std::endl;
            exitCode = 1;
        }
        
        delete[] cmdLineCStr;
        
        // Exit current process - all resources will be released
        std::cout << "[TestRunner2] Exiting current process. Resources will be released." << std::endl;
    } else {
        std::cout << "\n==================================================" << std::endl;
        std::cout << "All " << totalRuns << " runs completed." << std::endl;
        std::cout << "==================================================" << std::endl;
        
        // Load all run results and print comprehensive summary
        std::vector<RunResult> allResults = LoadAllRunResults(totalRuns);
        PrintComprehensiveSummary(allResults, totalRuns);
    }
    
    return exitCode;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    auto args = ParseArguments(argc, argv);
    
    if (!args.count("mode")) {
        std::cerr << "Error: --mode is required" << std::endl;
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::string mode = args["mode"];
    
    try {
        if (mode == "server") {
            return RunServerMode(args);
        } else if (mode == "client") {
            return RunClientMode(args);
        } else {
            std::cerr << "Error: Invalid mode '" << mode << "'. Must be 'server' or 'client'." << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}


#include "ControlServer.h"
#include "ControlClient.h"
#include <iostream>
#include <string>
#include <map>

using namespace TestRunner2;

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
    std::cout << "    --ipeftc-path <path>     Path to IPEFTC.exe (default: ..\\build\\Release\\IPEFTC.exe)\n" << std::endl;
    
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
    
    return allPassed ? 0 : 1;
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


#include "nlohmann/json.hpp"
#include "ControlClient.h"
#include "ControlServer.h"
#include "ProcessManager.h"
#include "Version.h"
#include <algorithm>
#include <atomic>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace TestRunner;

// Helper to parse CLI arguments
std::map<std::string, std::string> ParseArguments(int argc, char *argv[]) {
  std::map<std::string, std::string> args;
  for (int i = 1; i < argc; ++i) {
    std::string token = argv[i];
    if (token == "-h") {
      args["help"] = "";
    } else if (token == "-v") {
      args["version"] = "";
    } else if (token.substr(0, 2) == "--") {
      std::string key = token.substr(2);
      if (i + 1 < argc && std::string(argv[i + 1]).substr(0, 2) != "--") {
        args[key] = argv[++i];
      } else {
        args[key] = ""; // Flag only
      }
    }
  }
  return args;
}

void PrintUsage(const char *progName) {
  std::cout << TestRunner::VersionString() << "\n"
            << "Usage: " << progName << " --mode <server|client> [options]\n"
            << "Options:\n"
            << "  --control-port <port>   (Default: 9500)\n"
            << "  --ipeftc-path <path>    (Path to IPEFTC.exe)\n"
            << "  -v, --version           Display version information and exit\n"
            << "  -h, --help              Display this help message and exit\n"
            << "Server Mode:\n"
            << "  --mode server\n"
            << "Client Mode:\n"
            << "  --mode client --server <ip>\n"
            << "  --server-bind <ip>      (Remote IPEFTC server bind IP, Default: 0.0.0.0)\n"
            << "  --test-port <port>      (Default: 5201)\n"
            << "  --packet-size <bytes>   (Default: 8192)\n"
            << "  --num-packets <count>   (Default: 10000)\n"
            << "  --interval-ms <ms>      (Default: 0)\n"
            << "  --result-dir <path>     (Default: Results)\n"
            << "  --num-ports <count>     (Default: 1, Multi-port test)\n"
            << "  --total-runs <count>    (Default: 1, Repeat test)\n";
}

struct PortTestSummary {
  int port;
  TestResult clientResult;
  TestResult serverResult;
};

void PrintStartupVersions() {
  std::cout << "TestRunner version:\n"
            << TestRunner::BuildInfoString() << std::endl;

  auto &processManager = ProcessManager::GetInstance();
  const std::string ipeftcPath = processManager.GetIPEFTCPath();
  std::cout << "[ProcessManager] Resolved IPEFTC path: " << ipeftcPath
            << std::endl;

  const std::string ipeftcVersion =
      processManager.GetIPEFTCVersion(ipeftcPath);
  if (!ipeftcVersion.empty()) {
    std::cout << "[ProcessManager] IPEFTC version:\n"
              << ipeftcVersion << std::endl;
  } else {
    std::cerr << "[ProcessManager] Warning: Could not read IPEFTC version. "
                 "Continuing with test execution."
              << std::endl;
  }
}

int main(int argc, char *argv[]) {
  auto args = ParseArguments(argc, argv);

  if (args.find("version") != args.end()) {
    std::cout << TestRunner::BuildInfoString() << std::endl;
    return 0;
  }

  if (args.find("help") != args.end() || args.find("mode") == args.end()) {
    PrintUsage(argv[0]);
    return 0;
  }

  std::string mode = args["mode"];
  int controlPort = Protocol::DEFAULT_CONTROL_PORT;
  if (args.find("control-port") != args.end()) {
    controlPort = std::stoi(args["control-port"]);
  }

  if (args.find("ipeftc-path") != args.end()) {
    ProcessManager::GetInstance().SetIPEFTCPath(args["ipeftc-path"]);
  }

  PrintStartupVersions();

  if (mode == "server") {
    ControlServer server(controlPort);
    server.Start();
  } else if (mode == "client") {
    if (args.find("server") == args.end()) {
      std::cerr << "Error: --server IP required for client mode" << std::endl;
      return 1;
    }

    std::string serverIP = args["server"];
    std::string serverBindIP = "0.0.0.0";
    if (args.find("server-bind") != args.end())
      serverBindIP = args["server-bind"];

    int startTestPort = Protocol::DEFAULT_TEST_PORT;
    if (args.find("test-port") != args.end())
      startTestPort = std::stoi(args["test-port"]);

    int packetSize = 8192;
    if (args.find("packet-size") != args.end())
      packetSize = std::stoi(args["packet-size"]);

    long long numPackets = 10000;
    if (args.find("num-packets") != args.end())
      numPackets = std::stoll(args["num-packets"]);

    int intervalMs = 0;
    if (args.find("interval-ms") != args.end())
      intervalMs = std::stoi(args["interval-ms"]);

    std::string resultDir = "Results";
    if (args.find("result-dir") != args.end())
      resultDir = args["result-dir"];

    int numPorts = 1;
    if (args.find("num-ports") != args.end())
      numPorts = std::stoi(args["num-ports"]);

    int totalRuns = 1;
    if (args.find("total-runs") != args.end())
      totalRuns = std::stoi(args["total-runs"]);

    if (numPorts <= 0 || totalRuns <= 0) {
      std::cerr << "Error: --num-ports and --total-runs must be positive"
                << std::endl;
      return 1;
    }

    std::cout << "Starting TestRunner Client\n"
              << "Server: " << serverIP << ", Control Port: " << controlPort
              << "\n"
              << "Remote server bind IP: " << serverBindIP << "\n"
              << "Ports: " << numPorts << " (Start: " << startTestPort << ")\n"
              << "Packets: " << numPackets << ", Size: " << packetSize << "\n"
              << "Total Runs: " << totalRuns << "\n"
              << "----------------------------------------" << std::endl;

    // Use a flat list for global summary for simplicity, or structured by run
    std::vector<PortTestSummary> globalHistory;
    std::atomic<bool> anyFailure{false};
    std::atomic<unsigned long long> runIdCounter{0};

    for (int run = 1; run <= totalRuns; ++run) {
      if (totalRuns > 1) {
        std::cout << "\n>>> Starting Run " << run << " of " << totalRuns
                  << " <<<\n"
                  << std::endl;
      }

      std::vector<std::thread> threads;
      std::vector<PortTestSummary> results(numPorts);
      std::mutex resultsMutex;
      std::mutex portRunMutex;

      // Calculate total launch time to stagger starts if needed, or just launch
      // all

      for (int i = 0; i < numPorts; ++i) {
        int currentPort = startTestPort + i;
        threads.emplace_back([&, i, currentPort]() {
          std::lock_guard<std::mutex> portRunLock(portRunMutex);
          ControlClient client;
          TestConfig config;
          config.port = currentPort;
          config.packetSize = packetSize;
          config.numPackets = numPackets;
          config.sendIntervalMs = intervalMs;
          config.targetIP = serverIP; // Will be handled by ControlClient
          config.serverBindIP = serverBindIP;
          config.resultDir = resultDir;
          config.runId = "tr-run" + std::to_string(run) + "-port" +
                         std::to_string(currentPort) + "-" +
                         std::to_string(runIdCounter.fetch_add(1));

          TestResult clientRes, serverRes;
          if (client.RunTest(serverIP, controlPort, config, clientRes,
                             serverRes)) {
            std::lock_guard<std::mutex> lock(resultsMutex);
            results[i] = {currentPort, clientRes, serverRes};
            if (!clientRes.success || !serverRes.success) {
              anyFailure.store(true);
            }
          } else {
            std::lock_guard<std::mutex> lock(resultsMutex);
            results[i].port = currentPort;
            results[i].clientResult.success = false;
            results[i].clientResult.failureReason = "Control Client Failed";
            results[i].serverResult.success = false;
            anyFailure.store(true);
          }
        });

        // Small delay between launches to avoid overwhelming server accept
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      for (auto &t : threads) {
        if (t.joinable())
          t.join();
      }

      // Print Summary for this run
      std::cout << "\n--- TEST SUMMARY (Client-side View: Run " << run
                << ") ---" << std::endl;

      // Define column widths matching user request + padding
      const int colRole = 8;
      const int colPort = 8;
      const int colDuration = 15;
      const int colThroughput = 18;
      const int colBytes = 22;
      const int colPackets = 24;
      const int colStatus = 10;

      // Header: Role Port Duration (s) Throughput (Mbps) Total Bytes Rx Total
      // Packets Rx Status
      std::cout << std::left << std::setw(colRole) << "Role"
                << std::setw(colPort) << "Port" << std::setw(colDuration)
                << "Duration (s)" << std::setw(colThroughput)
                << "Throughput (Mbps)" << std::setw(colBytes)
                << "Total Bytes Rx" << std::setw(colPackets)
                << "Total Packets Rx"
                << "Status" << std::endl;

      std::string separator(colRole + colPort + colDuration + colThroughput +
                                colBytes + colPackets + colStatus,
                            '-');
      std::cout << separator << std::endl;

      for (const auto &res : results) {
        // Client Row
        std::cout << std::left << std::setw(colRole) << "Client"
                  << std::setw(colPort) << res.port << std::fixed
                  << std::setprecision(2) << std::setw(colDuration)
                  << res.clientResult.duration << std::setw(colThroughput)
                  << res.clientResult.throughput << std::setw(colBytes)
                  << res.clientResult.totalBytes << std::setw(colPackets)
                  << res.clientResult.totalPackets
                  << (res.clientResult.success ? "PASS" : "FAIL") << std::endl;

        // Server Row
        std::cout << std::left << std::setw(colRole) << "Server"
                  << std::setw(colPort) << res.port << std::fixed
                  << std::setprecision(2) << std::setw(colDuration)
                  << res.serverResult.duration << std::setw(colThroughput)
                  << res.serverResult.throughput << std::setw(colBytes)
                  << res.serverResult.totalBytes << std::setw(colPackets)
                  << res.serverResult.totalPackets
                  << (res.serverResult.success ? "PASS" : "FAIL") << std::endl;

        std::cout << separator << std::endl;
      }

      // Add to global history
      globalHistory.insert(globalHistory.end(), results.begin(), results.end());

      // Cleanup wait for next run
      if (run < totalRuns) {
        std::cout << "Run " << run
                  << " completed. Waiting 1 second before next run..."
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }

    // --- FINAL GLOBAL SUMMARY (Client Side) ---
    std::cout << "\n========================================================"
              << std::endl;
    std::cout << "          FINAL GLOBAL SUMMARY (Client-side)" << std::endl;
    std::cout << "========================================================"
              << std::endl;

    const int colRole = 8;
    const int colPort = 8;
    const int colDuration = 15;
    const int colThroughput = 18;
    const int colBytes = 22;
    const int colPackets = 24;
    const int colStatus = 10;

    std::cout << std::left << std::setw(colRole) << "Role" << std::setw(colPort)
              << "Port" << std::setw(colDuration) << "Duration (s)"
              << std::setw(colThroughput) << "Throughput (Mbps)"
              << std::setw(colBytes) << "Total Bytes Rx"
              << std::setw(colPackets) << "Total Packets Rx"
              << "Status" << std::endl;

    std::string separator(colRole + colPort + colDuration + colThroughput +
                              colBytes + colPackets + colStatus,
                          '-');
    std::cout << separator << std::endl;

    for (const auto &res : globalHistory) {
      // Client Row
      std::cout << std::left << std::setw(colRole) << "Client"
                << std::setw(colPort) << res.port << std::fixed
                << std::setprecision(2) << std::setw(colDuration)
                << res.clientResult.duration << std::setw(colThroughput)
                << res.clientResult.throughput << std::setw(colBytes)
                << res.clientResult.totalBytes << std::setw(colPackets)
                << res.clientResult.totalPackets
                << (res.clientResult.success ? "PASS" : "FAIL") << std::endl;

      // Server Row
      std::cout << std::left << std::setw(colRole) << "Server"
                << std::setw(colPort) << res.port << std::fixed
                << std::setprecision(2) << std::setw(colDuration)
                << res.serverResult.duration << std::setw(colThroughput)
                << res.serverResult.throughput << std::setw(colBytes)
                << res.serverResult.totalBytes << std::setw(colPackets)
                << res.serverResult.totalPackets
                << (res.serverResult.success ? "PASS" : "FAIL") << std::endl;

      std::cout << separator << std::endl;
    }

    // --- Send SERVER_SHUTDOWN ---
    std::cout << "\n[Client] All runs completed. Sending SERVER_SHUTDOWN..."
              << std::endl;
    ControlClient shutdownClient;
    if (shutdownClient.Connect(serverIP, controlPort)) {
      shutdownClient.SendMessage(
          SerializeServerShutdown(ServerShutdownMessage()));
      std::cout << "[Client] Shutdown command sent." << std::endl;
    } else {
      std::cerr << "[Client] Failed to connect to server for shutdown."
                << std::endl;
    }
    return anyFailure.load() ? 1 : 0;
  } else {
    std::cerr << "Unknown mode: " << mode << std::endl;
    PrintUsage(argv[0]);
    return 1;
  }

  return 0;
}

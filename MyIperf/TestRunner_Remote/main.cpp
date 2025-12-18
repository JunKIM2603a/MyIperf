#include "../nlohmann/json.hpp"
#include "ControlClient.h"
#include "ControlServer.h"
#include "ProcessManager.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace TestRunner2;

// Helper to parse CLI arguments
std::map<std::string, std::string> ParseArguments(int argc, char *argv[]) {
  std::map<std::string, std::string> args;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]).substr(0, 2) == "--") {
      std::string key = std::string(argv[i]).substr(2);
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
  std::cout << "Usage: " << progName << " --mode <server|client> [options]\n"
            << "Options:\n"
            << "  --control-port <port>   (Default: 9500)\n"
            << "  --ipeftc-path <path>    (Path to IPEFTC.exe)\n"
            << "Server Mode:\n"
            << "  --mode server\n"
            << "Client Mode:\n"
            << "  --mode client --server <ip>\n"
            << "  --test-port <port>      (Default: 60000)\n"
            << "  --packet-size <bytes>   (Default: 8192)\n"
            << "  --num-packets <count>   (Default: 10000)\n"
            << "  --interval-ms <ms>      (Default: 0)\n"
            << "  --num-ports <count>     (Default: 1, Multi-port test)\n"
            << "  --total-runs <count>    (Default: 1, Repeat test)\n";
}

struct PortTestSummary {
  int port;
  TestResult clientResult;
  TestResult serverResult;
};

int main(int argc, char *argv[]) {
  auto args = ParseArguments(argc, argv);

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

  if (mode == "server") {
    ControlServer server(controlPort);
    server.Start();
  } else if (mode == "client") {
    if (args.find("server") == args.end()) {
      std::cerr << "Error: --server IP required for client mode" << std::endl;
      return 1;
    }

    std::string serverIP = args["server"];
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

    int numPorts = 1;
    if (args.find("num-ports") != args.end())
      numPorts = std::stoi(args["num-ports"]);

    int totalRuns = 1;
    if (args.find("total-runs") != args.end())
      totalRuns = std::stoi(args["total-runs"]);

    std::cout << "Starting TestRunner2 Client\n"
              << "Server: " << serverIP << ", Control Port: " << controlPort
              << "\n"
              << "Ports: " << numPorts << " (Start: " << startTestPort << ")\n"
              << "Packets: " << numPackets << ", Size: " << packetSize << "\n"
              << "Total Runs: " << totalRuns << "\n"
              << "----------------------------------------" << std::endl;

    // Use a flat list for global summary for simplicity, or structured by run
    std::vector<PortTestSummary> globalHistory;

    for (int run = 1; run <= totalRuns; ++run) {
      if (totalRuns > 1) {
        std::cout << "\n>>> Starting Run " << run << " of " << totalRuns
                  << " <<<\n"
                  << std::endl;
      }

      std::vector<std::thread> threads;
      std::vector<PortTestSummary> results(numPorts);
      std::mutex resultsMutex;

      // Calculate total launch time to stagger starts if needed, or just launch
      // all

      for (int i = 0; i < numPorts; ++i) {
        int currentPort = startTestPort + i;
        threads.emplace_back([&, i, currentPort]() {
          ControlClient client;
          TestConfig config;
          config.port = currentPort;
          config.packetSize = packetSize;
          config.numPackets = numPackets;
          config.sendIntervalMs = intervalMs;
          config.targetIP = serverIP; // Will be handled by ControlClient

          TestResult clientRes, serverRes;
          if (client.RunTest(serverIP, controlPort, config, clientRes,
                             serverRes)) {
            std::lock_guard<std::mutex> lock(resultsMutex);
            results[i] = {currentPort, clientRes, serverRes};
          } else {
            std::lock_guard<std::mutex> lock(resultsMutex);
            results[i].port = currentPort;
            results[i].clientResult.success = false;
            results[i].clientResult.failureReason = "Control Client Failed";
            results[i].serverResult.success = false;
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
  } else {
    std::cerr << "Unknown mode: " << mode << std::endl;
    PrintUsage(argv[0]);
    return 1;
  }

  return 0;
}

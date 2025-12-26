#include "ControlServer.h"
#include "ProcessManager.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <vector>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#define ZeroMemory(x, y) memset(x, 0, y)
#define Sleep(x) usleep((x) * 1000)
#endif

namespace TestRunner2 {

ControlServer::ControlServer(int port)
    : port(port), listenSocket(INVALID_SOCKET), running(false) {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed" << std::endl;
    exit(1);
  }
#endif
}

ControlServer::~ControlServer() {
  Stop();
#ifdef _WIN32
  WSACleanup();
#endif
}

void ControlServer::Start() {
  struct addrinfo hints, *result = NULL;
  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  std::string portStr = std::to_string(port);
  if (getaddrinfo(NULL, portStr.c_str(), &hints, &result) != 0) {
    std::cerr << "getaddrinfo failed" << std::endl;
    return;
  }

  listenSocket =
      socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (listenSocket == INVALID_SOCKET) {
    std::cerr << "socket failed" << std::endl;
    freeaddrinfo(result);
    return;
  }

#ifndef _WIN32
  int opt = 1;
  if (setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    std::cerr << "setsockopt(SO_REUSEADDR) failed" << std::endl;
  }
#endif

  if (bind(listenSocket, result->ai_addr, (int)result->ai_addrlen) ==
      SOCKET_ERROR) {
    std::cerr << "bind failed" << std::endl;
    freeaddrinfo(result);
    closesocket(listenSocket);
    return;
  }

  freeaddrinfo(result);

  if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
    std::cerr << "listen failed" << std::endl;
    closesocket(listenSocket);
    return;
  }

  running = true;
  std::cout << "[ControlServer] Server listening on port " << port << std::endl;

  while (running) {
    std::cout << "[ControlServer] Waiting for client connections..."
              << std::endl;
    SOCKET clientSocket = accept(listenSocket, NULL, NULL);
    if (clientSocket == INVALID_SOCKET) {
      if (running)
        std::cerr << "accept failed" << std::endl;
      continue;
    }

    std::cout << "[ControlServer] Client connected" << std::endl;
    HandleClient(clientSocket);
    // Loop continues to accept more clients until SERVER_SHUTDOWN is received
  }
}

void ControlServer::Stop() {
  running = false;
  if (listenSocket != INVALID_SOCKET) {
    closesocket(listenSocket);
    listenSocket = INVALID_SOCKET;
  }
}

void ControlServer::HandleClient(SOCKET clientSocket) {
  ProcessHandles ipeftcProcess;
  bool ipeftcRunning = false;
  int currentTestPort = 0;

  try {
    while (true) {
      std::string jsonStr = ReceiveMessage(clientSocket);
      if (jsonStr.empty())
        break; // Connection closed or error

      MessageType type = GetMessageType(jsonStr);
      std::cout << "[ControlServer] Received message: "
                << MessageTypeToString(type) << std::endl;

      if (type == MessageType::CONFIG_REQUEST) {
        ConfigRequestMessage req = DeserializeConfigRequest(jsonStr);
        std::cout << "[ControlServer] Processing CONFIG_REQUEST for port "
                  << req.config.port << std::endl;

        // Determine IPEFTC path
        std::string ipeftcPath = ProcessManager::GetInstance().GetIPEFTCPath();

        // Launch IPEFTC Server
        if (ProcessManager::GetInstance().LaunchIPEFTCServer(
                ipeftcPath, req.config, ipeftcProcess)) {
          ipeftcRunning = true;
          currentTestPort = req.config.port;

          // Give it a moment to bind port (simple approach)
          Sleep(500);

          ServerReadyMessage resp;
          resp.port = req.config.port;
          resp.serverIP = req.config.targetIP;
          SendMessage(clientSocket, SerializeServerReady(resp));
          std::cout << "[ControlServer] Sent SERVER_READY" << std::endl;
        } else {
          ErrorMessage err("Failed to launch IPEFTC server");
          SendMessage(clientSocket, SerializeError(err));
        }

      } else if (type == MessageType::RESULTS_REQUEST) {
        ResultsRequestMessage req = DeserializeResultsRequest(jsonStr);
        std::cout << "[ControlServer] Processing RESULTS_REQUEST" << std::endl;

        TestResult serverResult;
        serverResult.role = "Server";
        serverResult.port = currentTestPort;

        if (ipeftcRunning) {
          // Terminate and capture output
          std::string output =
              ProcessManager::GetInstance().WaitForProcessAndCaptureOutput(
                  ipeftcProcess);
          ipeftcRunning = false;

          // Parse output
          serverResult = ProcessManager::GetInstance().ParseOutput(
              output, "Server", currentTestPort);
        } else {
          serverResult.failureReason = "IPEFTC server was not running";
          serverResult.success = false;
        }

        // Print Summary
        std::cout << "\n--- TEST SUMMARY (Server-side View) ---" << std::endl;
        const int colRole = 8;
        const int colPort = 8;
        const int colDuration = 15;
        const int colThroughput = 18;
        const int colBytes = 22;
        const int colPackets = 24;
        const int colStatus = 10;

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

        // Client Row (from request)
        TestResult clientResult = req.clientResult;
        std::cout << std::left << std::setw(colRole) << "Client"
                  << std::setw(colPort) << clientResult.port << std::fixed
                  << std::setprecision(2) << std::setw(colDuration)
                  << clientResult.duration << std::setw(colThroughput)
                  << clientResult.throughput << std::setw(colBytes)
                  << clientResult.totalBytes << std::setw(colPackets)
                  << clientResult.totalPackets
                  << (clientResult.success ? "PASS" : "FAIL") << std::endl;

        // Server Row (local result)
        std::cout << std::left << std::setw(colRole) << "Server"
                  << std::setw(colPort) << serverResult.port << std::fixed
                  << std::setprecision(2) << std::setw(colDuration)
                  << serverResult.duration << std::setw(colThroughput)
                  << serverResult.throughput << std::setw(colBytes)
                  << serverResult.totalBytes << std::setw(colPackets)
                  << serverResult.totalPackets
                  << (serverResult.success ? "PASS" : "FAIL") << std::endl;

        std::cout << separator << std::endl;

        // Send response
        ResultsResponseMessage resp;
        resp.serverResult = serverResult;
        SendMessage(clientSocket, SerializeResultsResponse(resp));
        std::cout << "[ControlServer] Sent RESULTS_RESPONSE" << std::endl;

        // Store history
        sessionHistory.push_back({clientResult, serverResult});

        // Disconnect after sending results as one test session is complete
        break;

      } else if (type == MessageType::HEARTBEAT) {
        // Echo heartbeat or ignore
      } else if (type == MessageType::SERVER_SHUTDOWN) {
        std::cout
            << "[ControlServer] Received SERVER_SHUTDOWN. Stopping server."
            << std::endl;

        // --- FINAL GLOBAL SUMMARY ---
        std::cout
            << "\n========================================================"
            << std::endl;
        std::cout << "          FINAL GLOBAL SUMMARY (Server-side)"
                  << std::endl;
        std::cout << "========================================================"
                  << std::endl;

        const int colRole = 8;
        const int colPort = 8;
        const int colDuration = 15;
        const int colThroughput = 18;
        const int colBytes = 22;
        const int colPackets = 24;
        const int colStatus = 10;

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

        for (const auto &rec : sessionHistory) {
          // Client Row
          std::cout << std::left << std::setw(colRole) << "Client"
                    << std::setw(colPort) << rec.client.port << std::fixed
                    << std::setprecision(2) << std::setw(colDuration)
                    << rec.client.duration << std::setw(colThroughput)
                    << rec.client.throughput << std::setw(colBytes)
                    << rec.client.totalBytes << std::setw(colPackets)
                    << rec.client.totalPackets
                    << (rec.client.success ? "PASS" : "FAIL") << std::endl;

          // Server Row
          std::cout << std::left << std::setw(colRole) << "Server"
                    << std::setw(colPort) << rec.server.port << std::fixed
                    << std::setprecision(2) << std::setw(colDuration)
                    << rec.server.duration << std::setw(colThroughput)
                    << rec.server.throughput << std::setw(colBytes)
                    << rec.server.totalBytes << std::setw(colPackets)
                    << rec.server.totalPackets
                    << (rec.server.success ? "PASS" : "FAIL") << std::endl;

          std::cout << separator << std::endl;
        }
        running = false; // Flag for Start() loop
        break;           // Break HandleClient loop
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[ControlServer] Error handling client: " << e.what()
              << std::endl;
  }

  // Cleanup
  if (ipeftcRunning) {
    ProcessManager::GetInstance().TerminateProcess(ipeftcProcess);
  }
  closesocket(clientSocket);
  std::cout << "[ControlServer] Client disconnected" << std::endl;
}

bool ControlServer::SendMessage(SOCKET socket, const Message &msg) {
  return SendMessage(socket, SerializeMessage(msg));
}

bool ControlServer::SendMessage(SOCKET socket,
                                const std::string &serializedMsg) {
  // 1. Send Length (Network Byte Order)
  uint32_t networkLen = htonl((uint32_t)serializedMsg.size());
  if (send(socket, (const char *)&networkLen, 4, 0) == SOCKET_ERROR)
    return false;

  // 2. Send Data
  if (send(socket, serializedMsg.c_str(), (int)serializedMsg.size(), 0) ==
      SOCKET_ERROR)
    return false;

  return true;
}

std::string ControlServer::ReceiveMessage(SOCKET socket) {
  // 1. Receive Length
  uint32_t networkLen;
  int bytesRead = recv(socket, (char *)&networkLen, 4, 0);
  if (bytesRead <= 0)
    return ""; // Closed or Error

  uint32_t len = ntohl(networkLen);
  if (len > Protocol::MAX_MESSAGE_SIZE)
    return ""; // Too big

  // 2. Receive Data
  std::vector<char> buffer(len);
  int totalReceived = 0;
  while (totalReceived < (int)len) {
    bytesRead = recv(socket, buffer.data() + totalReceived,
                     (int)len - totalReceived, 0);
    if (bytesRead <= 0)
      return "";
    totalReceived += bytesRead;
  }

  return std::string(buffer.begin(), buffer.end());
}

} // namespace TestRunner2

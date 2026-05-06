#include "ControlClient.h"
#include "ProcessManager.h"
#include <chrono>
#include <iostream>
#include <thread>
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
#endif

#ifdef _WIN32
#define SEND_FLAGS 0
#else
#define SEND_FLAGS MSG_NOSIGNAL
#endif

namespace TestRunner {

namespace {
bool SendAll(SOCKET socket, const char *data, int length) {
  int totalSent = 0;
  while (totalSent < length) {
    int sent = send(socket, data + totalSent, length - totalSent, SEND_FLAGS);
    if (sent == SOCKET_ERROR || sent == 0)
      return false;
    totalSent += sent;
  }
  return true;
}

bool RecvAll(SOCKET socket, char *data, int length) {
  int totalReceived = 0;
  while (totalReceived < length) {
    int received = recv(socket, data + totalReceived, length - totalReceived, 0);
    if (received <= 0)
      return false;
    totalReceived += received;
  }
  return true;
}
} // namespace

ControlClient::ControlClient() : connectSocket(INVALID_SOCKET) {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed" << std::endl;
    // Consider throwing exception or handling error gracefully
  }
#endif
}

ControlClient::~ControlClient() {
  Disconnect();
#ifdef _WIN32
  WSACleanup();
#endif
}

bool ControlClient::Connect(const std::string &serverIP, int port) {
  struct addrinfo hints, *result = NULL;
  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  std::string portStr = std::to_string(port);
  if (getaddrinfo(serverIP.c_str(), portStr.c_str(), &hints, &result) != 0) {
    std::cerr << "getaddrinfo failed" << std::endl;
    return false;
  }

  // Attempt to connect to an address until one succeeds
  struct addrinfo *ptr = NULL;
  for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
    connectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (connectSocket == INVALID_SOCKET) {
      continue;
    }

    if (connect(connectSocket, ptr->ai_addr, (int)ptr->ai_addrlen) ==
        SOCKET_ERROR) {
      closesocket(connectSocket);
      connectSocket = INVALID_SOCKET;
      continue;
    }
    break;
  }

  freeaddrinfo(result);

  if (connectSocket == INVALID_SOCKET) {
    return false;
  }

  return true;
}

void ControlClient::Disconnect() {
  if (connectSocket != INVALID_SOCKET) {
    closesocket(connectSocket);
    connectSocket = INVALID_SOCKET;
  }
}

bool ControlClient::RunTest(const std::string &serverIP, int controlPort,
                            const TestConfig &config,
                            TestResult &outClientResult,
                            TestResult &outServerResult,
                            const std::function<void()> &beforeLaunch) {
  std::cout << "[ControlClient] Connecting to " << serverIP << ":"
            << controlPort << "..." << std::endl;
  if (!Connect(serverIP, controlPort)) {
    std::cerr << "Failed to connect to server" << std::endl;
    return false;
  }
  std::cout << "[ControlClient] Connected to server" << std::endl;

  // 1. Send CONFIG_REQUEST
  ConfigRequestMessage configMsg;
  configMsg.config = config;
  if (configMsg.config.serverBindIP.empty()) {
    configMsg.config.serverBindIP = "0.0.0.0";
  }

  std::cout << "[ControlClient] Sending CONFIG_REQUEST for port " << config.port
            << std::endl;
  if (!SendMessage(SerializeConfigRequest(configMsg))) {
    std::cerr << "Failed to send CONFIG_REQUEST" << std::endl;
    return false;
  }

  // 2. Wait for SERVER_READY or an explicit server-side error.
  std::string respStr = ReceiveMessage();
  if (respStr.empty()) {
    std::cerr << "Connection lost while waiting for SERVER_READY" << std::endl;
    return false;
  }

  MessageType type = GetMessageType(respStr);
  if (type == MessageType::ERROR_MESSAGE) {
    ErrorMessage err = DeserializeError(respStr);
    std::cerr << "Server reported error: " << err.error << std::endl;
    return false;
  } else if (type != MessageType::SERVER_READY) {
    std::cerr << "Unexpected message expecting SERVER_READY: "
              << MessageTypeToString(type) << std::endl;
    return false;
  }

  ServerReadyMessage readyMsg = DeserializeServerReady(respStr);
  std::cout << "[ControlClient] Server ready on port " << readyMsg.port
            << std::endl;
  if (beforeLaunch) {
    beforeLaunch();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // 3. Launch Local IPEFTC Client
  ProcessHandles clientProcess;
  std::string ipeftcPath = ProcessManager::GetInstance().GetIPEFTCPath();

  // Update config with potentially adjusted targetIP (if needed)
  TestConfig runConfig = config;
  runConfig.targetIP = serverIP; // Force target IP to server IP

  std::cout << "[ProcessManager] Launching IPEFTC client..." << std::endl;
  if (!ProcessManager::GetInstance().LaunchIPEFTCClient(ipeftcPath, runConfig,
                                                        clientProcess)) {
    std::cerr << "Failed to launch local IPEFTC client" << std::endl;
    return false;
  }

  // 4. Wait for Client to Finish and Capture Logs
  std::cout << "[ControlClient] Waiting for IPEFTC client to complete..."
            << std::endl;
  std::string clientOutput =
      ProcessManager::GetInstance().WaitForProcessAndCaptureOutput(
          clientProcess);
  std::cout << "[ControlClient] Client test completed" << std::endl;

  // 5. Parse Client Results
  std::string resultError;
  if (!ProcessManager::GetInstance().ParseResultFile(runConfig, "Client",
                                                     outClientResult,
                                                     resultError)) {
    std::cerr << "[ControlClient] " << resultError
              << ". Falling back to stdout parser." << std::endl;
    outClientResult = ProcessManager::GetInstance().ParseOutput(
        clientOutput, "Client", config.port);
  }
  outClientResult.expectedPackets = config.numPackets;
  outClientResult.expectedBytes =
      (long long)config.packetSize * config.numPackets;

  // Basic validation on client side
  AnalyzeTestResult(outClientResult, outClientResult.expectedPackets,
                    outClientResult.expectedBytes);

  // 6. Request Server Results
  ResultsRequestMessage resReq;
  resReq.port = config.port;
  resReq.clientResult = outClientResult;

  std::cout << "[ControlClient] Requesting results from server..." << std::endl;
  if (!SendMessage(SerializeResultsRequest(resReq))) {
    std::cerr << "Failed to send RESULTS_REQUEST" << std::endl;
    return false;
  }

  // 7. Receive Server Results
  respStr = ReceiveMessage();
  if (respStr.empty()) {
    std::cerr << "Connection lost while waiting for RESULTS_RESPONSE"
              << std::endl;
    return false;
  }

  type = GetMessageType(respStr);
  if (type == MessageType::RESULTS_RESPONSE) {
    ResultsResponseMessage resResp = DeserializeResultsResponse(respStr);
    outServerResult = resResp.serverResult;
    // Validate server result (using client expectation)
    AnalyzeTestResult(outServerResult, config.numPackets,
                      (long long)config.packetSize * config.numPackets);
    std::cout << "[ControlClient] Received server results" << std::endl;
  } else {
    std::cerr << "Unexpected message expecting RESULTS_RESPONSE: "
              << MessageTypeToString(type) << std::endl;
    return false;
  }

  Disconnect();
  return true;
}

bool ControlClient::SendMessage(const Message &msg) {
  return SendMessage(SerializeMessage(msg));
}

bool ControlClient::SendMessage(const std::string &serializedMsg) {
  if (connectSocket == INVALID_SOCKET)
    return false;
  if (serializedMsg.size() > Protocol::MAX_MESSAGE_SIZE)
    return false;

  uint32_t networkLen = htonl((uint32_t)serializedMsg.size());
  if (!SendAll(connectSocket, (const char *)&networkLen, 4))
    return false;

  if (!SendAll(connectSocket, serializedMsg.c_str(), (int)serializedMsg.size()))
    return false;

  return true;
}

std::string ControlClient::ReceiveMessage() {
  if (connectSocket == INVALID_SOCKET)
    return "";

  uint32_t networkLen;
  if (!RecvAll(connectSocket, (char *)&networkLen, 4))
    return "";

  uint32_t len = ntohl(networkLen);
  if (len == 0 || len > Protocol::MAX_MESSAGE_SIZE)
    return "";

  std::vector<char> buffer(len);
  if (!RecvAll(connectSocket, buffer.data(), (int)len))
    return "";

  return std::string(buffer.begin(), buffer.end());
}

} // namespace TestRunner

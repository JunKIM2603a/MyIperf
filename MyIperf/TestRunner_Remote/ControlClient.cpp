#include "ControlClient.h"
#include "ProcessManager.h"
#include <iostream>
#include <vector>

namespace TestRunner2 {

ControlClient::ControlClient() : connectSocket(INVALID_SOCKET) {
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed" << std::endl;
    // Consider throwing exception or handling error gracefully
  }
}

ControlClient::~ControlClient() {
  Disconnect();
  WSACleanup();
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
                            TestResult &outServerResult) {
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
  // If targetIP is 0.0.0.0, use the serverIP we connected to
  if (configMsg.config.targetIP == "0.0.0.0") {
    configMsg.config.targetIP = serverIP;
  }

  std::cout << "[ControlClient] Sending CONFIG_REQUEST for port " << config.port
            << std::endl;
  if (!SendMessage(SerializeConfigRequest(configMsg))) {
    std::cerr << "Failed to send CONFIG_REQUEST" << std::endl;
    return false;
  }

  // 2. Wait for SERVER_READY
  // TODO: Implement timeout
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
  outClientResult = ProcessManager::GetInstance().ParseOutput(
      clientOutput, "Client", config.port);
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

  u_long networkLen = htonl((u_long)serializedMsg.size());
  if (send(connectSocket, (const char *)&networkLen, 4, 0) == SOCKET_ERROR)
    return false;

  if (send(connectSocket, serializedMsg.c_str(), (int)serializedMsg.size(),
           0) == SOCKET_ERROR)
    return false;

  return true;
}

std::string ControlClient::ReceiveMessage() {
  if (connectSocket == INVALID_SOCKET)
    return "";

  u_long networkLen;
  int bytesRead = recv(connectSocket, (char *)&networkLen, 4, 0);
  if (bytesRead <= 0)
    return "";

  u_long len = ntohl(networkLen);
  if (len > Protocol::MAX_MESSAGE_SIZE)
    return "";

  std::vector<char> buffer(len);
  int totalReceived = 0;
  while (totalReceived < (int)len) {
    bytesRead = recv(connectSocket, buffer.data() + totalReceived,
                     (int)len - totalReceived, 0);
    if (bytesRead <= 0)
      return "";
    totalReceived += bytesRead;
  }

  return std::string(buffer.begin(), buffer.end());
}

} // namespace TestRunner2

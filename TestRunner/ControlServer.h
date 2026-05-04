#pragma once

#include "Message.h"
#include "Protocol.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#endif

namespace TestRunner {

class ControlServer {
public:
  ControlServer(int port = Protocol::DEFAULT_CONTROL_PORT);
  ~ControlServer();

  // Start the server loop
  void Start();

  // Stop the server
  void Stop();

private:
  int port;
  SOCKET listenSocket;
  std::atomic<bool> running;

  void HandleClient(SOCKET clientSocket);
  void JoinClientThreads();
  void PrintFinalSummary();

  // Helper to send message
  bool SendMessage(SOCKET socket, const Message &msg);
  bool SendMessage(SOCKET socket, const std::string &serializedMsg);

  // Helper to receive message
  std::string ReceiveMessage(SOCKET socket);

  struct SessionRecord {
    TestResult client;
    TestResult server;
  };

  std::vector<SessionRecord> sessionHistory;
  std::mutex sessionHistoryMutex;
  std::vector<std::thread> clientThreads;
};

} // namespace TestRunner

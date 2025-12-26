#pragma once

#include "Message.h"
#include "Protocol.h"
#include <string>

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

namespace TestRunner2 {

class ControlClient {
public:
  ControlClient();
  ~ControlClient();

  // Run a single test session
  // Returns true if test flow completed successfully (regardless of test
  // pass/fail)
  bool RunTest(const std::string &serverIP, int controlPort,
               const TestConfig &config, TestResult &outClientResult,
               TestResult &outServerResult);

  bool Connect(const std::string &serverIP, int port);
  void Disconnect();

  bool SendMessage(const Message &msg);
  bool SendMessage(const std::string &serializedMsg);
  std::string ReceiveMessage();

private:
  SOCKET connectSocket;
};

} // namespace TestRunner2

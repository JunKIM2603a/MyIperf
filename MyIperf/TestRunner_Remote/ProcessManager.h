#pragma once

#include "Message.h"
#include <string>
#include <windows.h>

namespace TestRunner2 {

// Structure to hold process handles
struct ProcessHandles {
  PROCESS_INFORMATION processInfo;
  HANDLE stdOutRead;
  HANDLE stdOutWrite;

  ProcessHandles() : stdOutRead(NULL), stdOutWrite(NULL) {
    ZeroMemory(&processInfo, sizeof(PROCESS_INFORMATION));
  }
};

class ProcessManager {
public:
  static ProcessManager &GetInstance() {
    static ProcessManager instance;
    return instance;
  }

  // Launch IPEFTC Server
  // Returns true if started successfully
  bool LaunchIPEFTCServer(const std::string &ipeftcPath, TestConfig config,
                          ProcessHandles &handles);

  // Launch IPEFTC Client
  // Returns true if started successfully
  bool LaunchIPEFTCClient(const std::string &ipeftcPath,
                          const TestConfig &config, ProcessHandles &handles);

  // Determine IPEFTC executable path based on current directory
  std::string GetIPEFTCPath();

  // Set explicit path to IPEFTC executable
  void SetIPEFTCPath(const std::string &path);

  // Wait for process to complete and capture output
  // Returns the captured output string
  std::string WaitForProcessAndCaptureOutput(ProcessHandles &handles);

  // Terminate a running process
  void TerminateProcess(ProcessHandles &handles);

  // Parse legacy IPEFTC output to extract TestResult
  TestResult ParseOutput(const std::string &output, const std::string &role,
                         int port);

private:
  ProcessManager() = default;
  ~ProcessManager() = default;

  ProcessManager(const ProcessManager &) = delete;
  ProcessManager &operator=(const ProcessManager &) = delete;

  // Helper helper to create pipes
  bool CreatePipes(HANDLE &hRead, HANDLE &hWrite);

  std::string overrideIPEFTCPath;
};

} // namespace TestRunner2

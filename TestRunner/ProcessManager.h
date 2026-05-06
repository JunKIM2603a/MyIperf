#pragma once

#include "Message.h"
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

namespace TestRunner {

// Structure to hold process handles
struct ProcessHandles {
#ifdef _WIN32
  PROCESS_INFORMATION processInfo;
  HANDLE stdOutRead;
  HANDLE stdOutWrite;

  ProcessHandles() : stdOutRead(NULL), stdOutWrite(NULL) {
    ZeroMemory(&processInfo, sizeof(PROCESS_INFORMATION));
  }
#else
  pid_t pid;
  int pipeReadFd;

  ProcessHandles() : pid(-1), pipeReadFd(-1) {}
#endif
  std::string startupOutput;
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

  // Resolve IPEFTC executable path from override, current directory, or known
  // MyIperf build output locations.
  std::string GetIPEFTCPath();

  // Set explicit path to IPEFTC executable
  void SetIPEFTCPath(const std::string &path);

  // Run IPEFTC --version and return its stdout. Returns an empty string if the
  // executable cannot be launched or does not support the option.
  std::string GetIPEFTCVersion(const std::string &ipeftcPath);

  // Wait for process to complete and capture output
  // Returns the captured output string
  std::string WaitForProcessAndCaptureOutput(ProcessHandles &handles);

  bool WaitForServerReady(ProcessHandles &handles, int timeoutMs = 20000);

  // Terminate a running process
  void TerminateProcess(ProcessHandles &handles);

  // Parse captured IPEFTC output to extract TestResult
  TestResult ParseOutput(const std::string &output, const std::string &role,
                         int port);
  bool ParseResultFile(const TestConfig &config, const std::string &role,
                       TestResult &result, std::string &error);

private:
  ProcessManager() = default;
  ~ProcessManager() = default;

  ProcessManager(const ProcessManager &) = delete;
  ProcessManager &operator=(const ProcessManager &) = delete;

#ifdef _WIN32
  // Helper helper to create pipes
  bool CreatePipes(HANDLE &hRead, HANDLE &hWrite);
#endif

  std::string overrideIPEFTCPath;
};

} // namespace TestRunner

#include "ProcessManager.h"
#include "IpeftcOutputParser.h"
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#endif

namespace TestRunner {

namespace {

std::mutex processLaunchMutex;

#ifdef _WIN32
const char *IpeftcExecutableName() { return "IPEFTC.exe"; }
#else
const char *IpeftcExecutableName() { return "IPEFTC"; }
#endif

bool HasServerReadyMarker(const std::string &output) {
  return output.find("Transitioning to state: ACCEPTING") !=
             std::string::npos ||
         output.find("Server is running. Waiting for the test to complete") !=
             std::string::npos;
}

std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
  char path[MAX_PATH] = {};
  DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return std::filesystem::current_path();
  }
  return std::filesystem::path(path).parent_path();
#else
  char path[4096] = {};
  ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (length <= 0) {
    return std::filesystem::current_path();
  }
  path[length] = '\0';
  return std::filesystem::path(path).parent_path();
#endif
}

std::vector<std::filesystem::path> IpeftcPathCandidates() {
  const auto cwd = std::filesystem::current_path();
  const auto exeDir = ExecutableDirectory();
  const auto exeName = IpeftcExecutableName();

  std::vector<std::filesystem::path> candidates{
      cwd / exeName,
      cwd / "MyIperf" / "build" / "bin" / "Release" / exeName,
      cwd / "MyIperf" / "build" / "bin" / "Debug" / exeName,
      exeDir / exeName,
      exeDir / ".." / ".." / ".." / "MyIperf" / "build" / "bin" /
          "Release" / exeName,
      exeDir / ".." / ".." / ".." / "MyIperf" / "build" / "bin" / "Debug" /
          exeName,
      exeDir / ".." / ".." / "MyIperf" / "build" / "bin" / "Release" /
          exeName,
      exeDir / ".." / ".." / "MyIperf" / "build" / "bin" / "Debug" / exeName,
  };

  std::vector<std::filesystem::path> unique;
  for (const auto &candidate : candidates) {
    auto normalized = std::filesystem::absolute(candidate).lexically_normal();
    if (std::find(unique.begin(), unique.end(), normalized) == unique.end()) {
      unique.push_back(normalized);
    }
  }
  return unique;
}

std::string FormatIpeftcCandidates() {
  std::ostringstream oss;
  for (const auto &candidate : IpeftcPathCandidates()) {
    oss << "\n  - " << candidate.string();
  }
  return oss.str();
}

bool FileExists(const std::string &path) {
  std::error_code ec;
  return std::filesystem::exists(std::filesystem::path(path), ec) &&
         !std::filesystem::is_directory(std::filesystem::path(path), ec);
}

#ifdef _WIN32
bool IsAvailableOnPath(const std::string &name) {
  char resolved[MAX_PATH] = {};
  return SearchPathA(NULL, name.c_str(), NULL, MAX_PATH, resolved, NULL) > 0;
}
#else
bool IsAvailableOnPath(const std::string &name) {
  if (name.find('/') != std::string::npos) {
    return access(name.c_str(), X_OK) == 0;
  }

  const char *pathEnv = std::getenv("PATH");
  if (!pathEnv) {
    return false;
  }

  std::stringstream paths(pathEnv);
  std::string dir;
  while (std::getline(paths, dir, ':')) {
    std::filesystem::path candidate = std::filesystem::path(dir) / name;
    if (access(candidate.string().c_str(), X_OK) == 0) {
      return true;
    }
  }
  return false;
}
#endif

bool IsExecutableResolvable(const std::string &path) {
  if (path.empty()) {
    return false;
  }
  return FileExists(path) || IsAvailableOnPath(path);
}

bool ValidateIpeftcPath(const std::string &ipeftcPath) {
  if (IsExecutableResolvable(ipeftcPath)) {
    return true;
  }

  std::cerr << "[ProcessManager] IPEFTC executable was not found: "
            << ipeftcPath << "\nSearched default candidates:"
            << FormatIpeftcCandidates() << std::endl;
  return false;
}

std::string QuoteArgument(const std::string &arg) {
  if (arg.empty()) {
    return "\"\"";
  }

  bool needsQuotes = arg.find_first_of(" \t\"") != std::string::npos;
  if (!needsQuotes) {
    return arg;
  }

  std::string quoted = "\"";
  for (char ch : arg) {
    if (ch == '"') {
      quoted += "\\\"";
    } else {
      quoted += ch;
    }
  }
  quoted += "\"";
  return quoted;
}

std::string BuildCommandLine(const std::string &exe,
                             const std::vector<std::string> &args) {
  std::string commandLine = QuoteArgument(exe);
  for (const auto &arg : args) {
    commandLine += " " + QuoteArgument(arg);
  }
  return commandLine;
}

std::vector<std::string> BuildServerArgs(const TestConfig &config) {
  std::vector<std::string> args{"--mode", "server"};
  const std::string bindIP =
      config.serverBindIP.empty() ? "0.0.0.0" : config.serverBindIP;
  args.push_back("--target");
  args.push_back(bindIP);
  if (config.port > 0) {
    args.push_back("--port");
    args.push_back(std::to_string(config.port));
  }
  args.push_back("--save-logs");
  args.push_back(config.saveLogs ? "true" : "false");
  return args;
}

std::vector<std::string> BuildClientArgs(const TestConfig &config) {
  std::vector<std::string> args{"--mode", "client"};
  if (!config.targetIP.empty()) {
    args.push_back("--target");
    args.push_back(config.targetIP);
  }
  if (config.port > 0) {
    args.push_back("--port");
    args.push_back(std::to_string(config.port));
  }
  if (config.packetSize > 0) {
    args.push_back("--packet-size");
    args.push_back(std::to_string(config.packetSize));
  }
  if (config.numPackets > 0) {
    args.push_back("--num-packets");
    args.push_back(std::to_string(config.numPackets));
  }
  args.push_back("--save-logs");
  args.push_back(config.saveLogs ? "true" : "false");
  if (config.sendIntervalMs > 0) {
    args.push_back("--interval-ms");
    args.push_back(std::to_string(config.sendIntervalMs));
  }
  return args;
}

#ifdef _WIN32
bool CreateProcessWithStdoutHandle(const std::string &ipeftcPath,
                                   const std::string &cmdLine,
                                   HANDLE stdOut,
                                   PROCESS_INFORMATION &processInfo) {
  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.hStdError = stdOut;
  si.hStdOutput = stdOut;
  si.dwFlags |= STARTF_USESTDHANDLES;

  std::vector<char> cmdVec(cmdLine.begin(), cmdLine.end());
  cmdVec.push_back('\0');

  LPCSTR applicationName = FileExists(ipeftcPath) ? ipeftcPath.c_str() : NULL;
  BOOL created =
      CreateProcessA(applicationName, cmdVec.data(), NULL, NULL, TRUE, 0, NULL,
                     NULL, &si, &processInfo);

  return created == TRUE;
}
#endif

} // namespace

std::string ProcessManager::GetIPEFTCPath() {
  if (!overrideIPEFTCPath.empty()) {
    return overrideIPEFTCPath;
  }

  for (const auto &candidate : IpeftcPathCandidates()) {
    if (FileExists(candidate.string())) {
      return candidate.string();
    }
  }

  return IpeftcExecutableName();
}

void ProcessManager::SetIPEFTCPath(const std::string &path) {
  overrideIPEFTCPath = path;
}

#ifdef _WIN32
bool ProcessManager::CreatePipes(HANDLE &hRead, HANDLE &hWrite) {
  SECURITY_ATTRIBUTES saAttr;
  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  constexpr DWORD pipeBufferSize = 1024 * 1024;
  if (!CreatePipe(&hRead, &hWrite, &saAttr, pipeBufferSize)) {
    return false;
  }

  // Ensure the read handle to the pipe for STDOUT is not inherited.
  if (!SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0)) {
    return false;
  }
  return true;
}

bool ProcessManager::LaunchIPEFTCServer(const std::string &ipeftcPath,
                                        TestConfig config,
                                        ProcessHandles &handles) {
  if (!ValidateIpeftcPath(ipeftcPath)) {
    return false;
  }

  std::lock_guard<std::mutex> launchLock(processLaunchMutex);
  std::string cmdLine = BuildCommandLine(ipeftcPath, BuildServerArgs(config));

  if (!CreatePipes(handles.stdOutRead, handles.stdOutWrite)) {
    std::cerr << "Failed to create pipes" << std::endl;
    return false;
  }

  if (!CreateProcessWithStdoutHandle(ipeftcPath, cmdLine, handles.stdOutWrite,
                                     handles.processInfo)) {
    std::cerr << "CreateProcess failed (" << GetLastError()
              << "). Cmd: " << cmdLine << std::endl;
    CloseHandle(handles.stdOutRead);
    CloseHandle(handles.stdOutWrite);
    return false;
  }

  CloseHandle(handles.stdOutWrite);
  handles.stdOutWrite = NULL;

  return true;
}

bool ProcessManager::LaunchIPEFTCClient(const std::string &ipeftcPath,
                                        const TestConfig &config,
                                        ProcessHandles &handles) {

  if (!ValidateIpeftcPath(ipeftcPath)) {
    return false;
  }

  std::lock_guard<std::mutex> launchLock(processLaunchMutex);
  std::string cmdLine = BuildCommandLine(ipeftcPath, BuildClientArgs(config));

  if (!CreatePipes(handles.stdOutRead, handles.stdOutWrite)) {
    std::cerr << "Failed to create pipes" << std::endl;
    return false;
  }

  if (!CreateProcessWithStdoutHandle(ipeftcPath, cmdLine, handles.stdOutWrite,
                                     handles.processInfo)) {
    std::cerr << "CreateProcess failed (" << GetLastError()
              << "). Cmd: " << cmdLine << std::endl;
    CloseHandle(handles.stdOutRead);
    CloseHandle(handles.stdOutWrite);
    return false;
  }

  CloseHandle(handles.stdOutWrite);
  handles.stdOutWrite = NULL;

  return true;
}

void ProcessManager::TerminateProcess(ProcessHandles &handles) {
  if (handles.processInfo.hProcess) {
    ::TerminateProcess(handles.processInfo.hProcess, 0);
    WaitForSingleObject(handles.processInfo.hProcess, 2000);
    CloseHandle(handles.processInfo.hProcess);
    CloseHandle(handles.processInfo.hThread);
    handles.processInfo.hProcess = NULL;
  }
  if (handles.stdOutRead) {
    CloseHandle(handles.stdOutRead);
    handles.stdOutRead = NULL;
  }
  if (handles.stdOutWrite) {
    CloseHandle(handles.stdOutWrite);
    handles.stdOutWrite = NULL;
  }
}

std::string
ProcessManager::WaitForProcessAndCaptureOutput(ProcessHandles &handles) {
  // Close write end of pipe so ReadFile loop terminates when process closes its
  // write end
  if (handles.stdOutWrite) {
    CloseHandle(handles.stdOutWrite);
    handles.stdOutWrite = NULL;
  }

  std::string output = handles.startupOutput;
  handles.startupOutput.clear();
  char buffer[4096];
  DWORD bytesRead;

  while (true) {
    if (!ReadFile(handles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead,
                  NULL) ||
        bytesRead == 0) {
      break;
    }
    buffer[bytesRead] = '\0';
    output += buffer;
    std::cout << buffer; // Echo to console for visibility
  }

  WaitForSingleObject(handles.processInfo.hProcess, INFINITE);

  if (handles.stdOutRead) {
    CloseHandle(handles.stdOutRead);
    handles.stdOutRead = NULL;
  }
  if (handles.processInfo.hProcess) {
    CloseHandle(handles.processInfo.hProcess);
    handles.processInfo.hProcess = NULL;
  }
  if (handles.processInfo.hThread) {
    CloseHandle(handles.processInfo.hThread);
    handles.processInfo.hThread = NULL;
  }

  return output;
}

bool ProcessManager::WaitForServerReady(ProcessHandles &handles, int timeoutMs) {
  auto startTime = std::chrono::steady_clock::now();
  const auto timeout = std::chrono::milliseconds(timeoutMs);

  while (std::chrono::steady_clock::now() - startTime < timeout) {
    DWORD bytesAvailable = 0;
    if (PeekNamedPipe(handles.stdOutRead, NULL, 0, NULL, &bytesAvailable, NULL) &&
        bytesAvailable > 0) {
      char buffer[4096];
      DWORD bytesRead = 0;
      if (ReadFile(handles.stdOutRead, buffer, sizeof(buffer) - 1, &bytesRead,
                   NULL) &&
          bytesRead > 0) {
        buffer[bytesRead] = '\0';
        handles.startupOutput.append(buffer, bytesRead);
        std::cout << buffer;
        if (HasServerReadyMarker(handles.startupOutput)) {
          std::cout << "[ProcessManager] Server is ready" << std::endl;
          return true;
        }
      }
    }

    DWORD exitCode = 0;
    if (GetExitCodeProcess(handles.processInfo.hProcess, &exitCode) &&
        exitCode != STILL_ACTIVE) {
      handles.startupOutput +=
          "\n[ProcessManager] Server process exited early during startup "
          "(exitCode=" +
          std::to_string(exitCode) + ").\n";
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  handles.startupOutput += "\n[ProcessManager] Timeout waiting for server readiness.\n";
  return false;
}
#else

bool ProcessManager::LaunchIPEFTCServer(const std::string &ipeftcPath,
                                        TestConfig config,
                                        ProcessHandles &handles) {
  if (!ValidateIpeftcPath(ipeftcPath)) {
    return false;
  }

  std::lock_guard<std::mutex> launchLock(processLaunchMutex);
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    perror("pipe");
    return false;
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }

  if (pid == 0) {                   // Child
    close(pipefd[0]);               // Close read end
    dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
    dup2(pipefd[1], STDERR_FILENO); // Redirect stderr to pipe
    close(pipefd[1]);

    std::vector<std::string> args;
    args.push_back(ipeftcPath);
    auto serverArgs = BuildServerArgs(config);
    args.insert(args.end(), serverArgs.begin(), serverArgs.end());

    std::vector<char *> c_args;
    for (auto &arg : args)
      c_args.push_back(&arg[0]);
    c_args.push_back(nullptr);

    execvp(ipeftcPath.c_str(), c_args.data());
    perror("execvp");
    exit(1);
  } else {            // Parent
    close(pipefd[1]); // Close write end
    handles.pid = pid;
    handles.pipeReadFd = pipefd[0];
    return true;
  }
}

bool ProcessManager::LaunchIPEFTCClient(const std::string &ipeftcPath,
                                        const TestConfig &config,
                                        ProcessHandles &handles) {
  if (!ValidateIpeftcPath(ipeftcPath)) {
    return false;
  }

  std::lock_guard<std::mutex> launchLock(processLaunchMutex);
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    perror("pipe");
    return false;
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }

  if (pid == 0) { // Child
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    std::vector<std::string> args;
    args.push_back(ipeftcPath);
    auto clientArgs = BuildClientArgs(config);
    args.insert(args.end(), clientArgs.begin(), clientArgs.end());

    std::vector<char *> c_args;
    for (auto &arg : args)
      c_args.push_back(&arg[0]);
    c_args.push_back(nullptr);

    execvp(ipeftcPath.c_str(), c_args.data());
    perror("execvp");
    exit(1);
  } else { // Parent
    close(pipefd[1]);
    handles.pid = pid;
    handles.pipeReadFd = pipefd[0];
    return true;
  }
}

void ProcessManager::TerminateProcess(ProcessHandles &handles) {
  if (handles.pid > 0) {
    kill(handles.pid, SIGTERM);
    waitpid(handles.pid, nullptr, 0);
    handles.pid = -1;
  }
  if (handles.pipeReadFd != -1) {
    close(handles.pipeReadFd);
    handles.pipeReadFd = -1;
  }
}

std::string
ProcessManager::WaitForProcessAndCaptureOutput(ProcessHandles &handles) {
  std::string output = handles.startupOutput;
  handles.startupOutput.clear();
  char buffer[4096];
  ssize_t bytesRead;

  int flags = fcntl(handles.pipeReadFd, F_GETFL, 0);
  if (flags != -1) {
    fcntl(handles.pipeReadFd, F_SETFL, flags & ~O_NONBLOCK);
  }

  while (true) {
    bytesRead = read(handles.pipeReadFd, buffer, sizeof(buffer) - 1);
    if (bytesRead <= 0)
      break;

    buffer[bytesRead] = '\0';
    output += buffer;
    std::cout << buffer; // Echo
  }

  waitpid(handles.pid, nullptr, 0);
  handles.pid = -1;
  if (handles.pipeReadFd != -1) {
    close(handles.pipeReadFd);
    handles.pipeReadFd = -1;
  }

  return output;
}

bool ProcessManager::WaitForServerReady(ProcessHandles &handles, int timeoutMs) {
  auto startTime = std::chrono::steady_clock::now();
  const auto timeout = std::chrono::milliseconds(timeoutMs);

  int flags = fcntl(handles.pipeReadFd, F_GETFL, 0);
  if (flags != -1) {
    fcntl(handles.pipeReadFd, F_SETFL, flags | O_NONBLOCK);
  }

  while (std::chrono::steady_clock::now() - startTime < timeout) {
    char buffer[4096];
    ssize_t bytesRead = read(handles.pipeReadFd, buffer, sizeof(buffer) - 1);
    if (bytesRead > 0) {
      buffer[bytesRead] = '\0';
      handles.startupOutput.append(buffer, bytesRead);
      std::cout << buffer;
      if (HasServerReadyMarker(handles.startupOutput)) {
        std::cout << "[ProcessManager] Server is ready" << std::endl;
        return true;
      }
    }

    int status = 0;
    pid_t result = waitpid(handles.pid, &status, WNOHANG);
    if (result == handles.pid) {
      handles.pid = -1;
      handles.startupOutput +=
          "\n[ProcessManager] Server process exited early during startup.\n";
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  handles.startupOutput += "\n[ProcessManager] Timeout waiting for server readiness.\n";
  return false;
}
#endif

TestResult ProcessManager::ParseOutput(const std::string &output,
                                       const std::string &role, int port) {
  return IpeftcOutputParser::Parse(output, role, port);
}

} // namespace TestRunner

#include "nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  std::string ipeftcPath;
  std::string host = "127.0.0.1";
  std::string bind = "0.0.0.0";
  int port = 55301;
  int packetSize = 1024;
  int numPackets = 5;
  int intervalMs = 1;
  std::string runId;
  fs::path resultDir;
  int timeoutMs = 30000;
};

struct ProcessResult {
  bool started = false;
  bool timedOut = false;
  int exitCode = -1;
  std::string output;
};

std::string platformExeName(const std::string &base) {
#ifdef _WIN32
  return base + ".exe";
#else
  return base;
#endif
}

int currentProcessId() {
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
}

std::string timestampForId() {
  auto now = std::chrono::system_clock::now();
  auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count();
  return std::to_string(millis);
}

std::string makeDefaultRunId() {
  return "rpv-" + timestampForId() + "-" + std::to_string(currentProcessId());
}

std::string sanitizeName(std::string value) {
  for (char &ch : value) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
    if (!ok) {
      ch = '_';
    }
  }
  return value.empty() ? "run" : value;
}

std::string quoteArg(const std::string &arg) {
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

std::string joinCommandLine(const std::string &exe,
                            const std::vector<std::string> &args) {
  std::string cmd = quoteArg(exe);
  for (const auto &arg : args) {
    cmd += " " + quoteArg(arg);
  }
  return cmd;
}

bool writeTextFile(const fs::path &path, const std::string &text,
                   std::string &error) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "Failed to create directory " + path.parent_path().string() +
            ": " + ec.message();
    return false;
  }

  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    error = "Failed to open " + path.string();
    return false;
  }
  out << text;
  if (!out.good()) {
    error = "Failed to write " + path.string();
    return false;
  }
  return true;
}

bool writeJsonFile(const fs::path &path, const json &value,
                   std::string &error) {
  return writeTextFile(path, value.dump(2) + "\n", error);
}

std::string linesToText(const std::vector<std::string> &lines) {
  std::ostringstream oss;
  for (const auto &line : lines) {
    oss << line << '\n';
  }
  return oss.str();
}

fs::path absoluteNormalized(const fs::path &path) {
  std::error_code ec;
  auto abs = fs::absolute(path, ec);
  if (ec) {
    return path.lexically_normal();
  }
  return abs.lexically_normal();
}

#ifdef _WIN32
bool isExecutableOnPath(const std::string &name) {
  char resolved[MAX_PATH] = {};
  return SearchPathA(NULL, name.c_str(), NULL, MAX_PATH, resolved, NULL) > 0;
}
#else
bool isExecutableOnPath(const std::string &name) {
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
    fs::path candidate = fs::path(dir) / name;
    if (access(candidate.string().c_str(), X_OK) == 0) {
      return true;
    }
  }
  return false;
}
#endif

bool fileExists(const fs::path &path) {
  std::error_code ec;
  return fs::exists(path, ec) && !fs::is_directory(path, ec);
}

std::vector<fs::path> ipeftcCandidates() {
  const auto cwd = fs::current_path();
  const auto exe = platformExeName("IPEFTC");
  return {
      cwd / exe,
      cwd / "MyIperf" / "build" / "bin" / "Release" / exe,
      cwd / "MyIperf" / "build" / "bin" / "Debug" / exe,
      cwd / ".." / "MyIperf" / "build" / "bin" / "Release" / exe,
      cwd / ".." / "MyIperf" / "build" / "bin" / "Debug" / exe,
  };
}

std::string resolveIpeftcPath(const std::string &overridePath,
                              std::vector<std::string> &searched) {
  if (!overridePath.empty()) {
    searched.push_back(overridePath);
    if (fileExists(overridePath) || isExecutableOnPath(overridePath)) {
      return overridePath;
    }
    return "";
  }

  for (const auto &candidate : ipeftcCandidates()) {
    auto normalized = absoluteNormalized(candidate);
    searched.push_back(normalized.string());
    if (fileExists(normalized)) {
      return normalized.string();
    }
  }

  const std::string exe = platformExeName("IPEFTC");
  searched.push_back(exe);
  if (isExecutableOnPath(exe)) {
    return exe;
  }
  return "";
}

class ChildProcess {
public:
  bool start(const std::string &exe, const std::vector<std::string> &args,
             std::string &error) {
    executable = exe;
    arguments = args;
#ifdef _WIN32
    return startWindows(error);
#else
    return startPosix(error);
#endif
  }

  bool waitFor(std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
      if (pollExited()) {
        joinReader();
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    timedOut.store(true);
    terminate();
    pollExited();
    joinReader();
    return false;
  }

  bool waitForOutput(const std::string &marker,
                     std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
      if (outputContains(marker)) {
        return true;
      }
      if (pollExited()) {
        return outputContains(marker);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return outputContains(marker);
  }

  void terminate() {
    if (exited.load()) {
      return;
    }
#ifdef _WIN32
    if (processHandle != NULL) {
      TerminateProcess(processHandle, 1);
    }
#else
    if (pid > 0) {
      kill(pid, SIGTERM);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      if (!pollExited()) {
        kill(pid, SIGKILL);
      }
    }
#endif
  }

  ProcessResult result() const {
    ProcessResult r;
    r.started = started.load();
    r.timedOut = timedOut.load();
    r.exitCode = exitCode.load();
    r.output = capturedOutput();
    return r;
  }

  std::string capturedOutput() const {
    std::lock_guard<std::mutex> lock(outputMutex);
    return output;
  }

  bool hasExited() { return pollExited(); }

  ~ChildProcess() {
    terminate();
    pollExited();
    joinReader();
    closeHandles();
  }

private:
  bool outputContains(const std::string &marker) const {
    std::lock_guard<std::mutex> lock(outputMutex);
    return output.find(marker) != std::string::npos;
  }

  void appendOutput(const char *data, size_t size) {
    std::lock_guard<std::mutex> lock(outputMutex);
    output.append(data, size);
  }

  void joinReader() {
    if (reader.joinable()) {
      reader.join();
    }
    closeHandles();
  }

#ifdef _WIN32
  bool startWindows(std::string &error) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE readPipe = NULL;
    HANDLE writePipe = NULL;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 64 * 1024)) {
      error = "CreatePipe failed: " + std::to_string(GetLastError());
      return false;
    }
    if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
      error = "SetHandleInformation failed: " + std::to_string(GetLastError());
      CloseHandle(readPipe);
      CloseHandle(writePipe);
      return false;
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::string commandLine = joinCommandLine(executable, arguments);
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');

    LPCSTR appName = fileExists(executable) ? executable.c_str() : NULL;
    BOOL ok = CreateProcessA(appName, mutableCommand.data(), NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    CloseHandle(writePipe);
    if (!ok) {
      error = "CreateProcess failed: " + std::to_string(GetLastError()) +
              " command=" + commandLine;
      CloseHandle(readPipe);
      return false;
    }

    processHandle = pi.hProcess;
    threadHandle = pi.hThread;
    stdoutRead = readPipe;
    started.store(true);
    reader = std::thread([this] { readerLoopWindows(); });
    return true;
  }

  void readerLoopWindows() {
    char buffer[4096];
    DWORD read = 0;
    while (stdoutRead != NULL &&
           ReadFile(stdoutRead, buffer, sizeof(buffer), &read, NULL) && read > 0) {
      appendOutput(buffer, read);
    }
  }

  bool pollExited() {
    if (exited.load()) {
      return true;
    }
    if (processHandle == NULL) {
      return false;
    }
    DWORD code = STILL_ACTIVE;
    if (!GetExitCodeProcess(processHandle, &code)) {
      return false;
    }
    if (code != STILL_ACTIVE) {
      exitCode.store(static_cast<int>(code));
      exited.store(true);
      return true;
    }
    return false;
  }

  void closeHandles() {
    if (stdoutRead != NULL) {
      CloseHandle(stdoutRead);
      stdoutRead = NULL;
    }
    if (threadHandle != NULL) {
      CloseHandle(threadHandle);
      threadHandle = NULL;
    }
    if (processHandle != NULL) {
      CloseHandle(processHandle);
      processHandle = NULL;
    }
  }
#else
  bool startPosix(std::string &error) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
      error = "pipe failed: errno=" + std::to_string(errno);
      return false;
    }

    pid_t child = fork();
    if (child == -1) {
      error = "fork failed: errno=" + std::to_string(errno);
      close(pipefd[0]);
      close(pipefd[1]);
      return false;
    }

    if (child == 0) {
      close(pipefd[0]);
      dup2(pipefd[1], STDOUT_FILENO);
      dup2(pipefd[1], STDERR_FILENO);
      close(pipefd[1]);

      std::vector<std::string> commandArgs;
      commandArgs.push_back(executable);
      commandArgs.insert(commandArgs.end(), arguments.begin(), arguments.end());

      std::vector<char *> argv;
      for (auto &arg : commandArgs) {
        argv.push_back(arg.data());
      }
      argv.push_back(nullptr);
      execvp(executable.c_str(), argv.data());
      _exit(127);
    }

    close(pipefd[1]);
    pid = child;
    stdoutFd = pipefd[0];
    started.store(true);
    reader = std::thread([this] { readerLoopPosix(); });
    return true;
  }

  void readerLoopPosix() {
    char buffer[4096];
    while (stdoutFd != -1) {
      ssize_t n = read(stdoutFd, buffer, sizeof(buffer));
      if (n > 0) {
        appendOutput(buffer, static_cast<size_t>(n));
      } else if (n == 0) {
        break;
      } else if (errno != EINTR) {
        break;
      }
    }
  }

  bool pollExited() {
    if (exited.load()) {
      return true;
    }
    if (pid <= 0) {
      return false;
    }
    int status = 0;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      if (WIFEXITED(status)) {
        exitCode.store(WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        exitCode.store(128 + WTERMSIG(status));
      } else {
        exitCode.store(-1);
      }
      exited.store(true);
      pid = -1;
      return true;
    }
    return false;
  }

  void closeHandles() {
    if (stdoutFd != -1) {
      close(stdoutFd);
      stdoutFd = -1;
    }
  }
#endif

  std::string executable;
  std::vector<std::string> arguments;
  std::atomic<bool> started{false};
  std::atomic<bool> exited{false};
  std::atomic<bool> timedOut{false};
  std::atomic<int> exitCode{-1};
  mutable std::mutex outputMutex;
  std::string output;
  std::thread reader;

#ifdef _WIN32
  HANDLE processHandle = NULL;
  HANDLE threadHandle = NULL;
  HANDLE stdoutRead = NULL;
#else
  pid_t pid = -1;
  int stdoutFd = -1;
#endif
};

class PipeReader {
public:
  PipeReader(std::string displayName, std::string pipePath)
      : name(std::move(displayName)), path(std::move(pipePath)) {}

  bool prepare(std::string &error) {
#ifndef _WIN32
    struct stat st;
    if (lstat(path.c_str(), &st) == 0) {
      if (!S_ISFIFO(st.st_mode)) {
        error = path + " exists but is not a FIFO";
        return false;
      }
      return true;
    }
    fs::create_directories(fs::path(path).parent_path());
    if (mkfifo(path.c_str(), 0666) == -1 && errno != EEXIST) {
      error = "mkfifo failed for " + path + ": errno=" + std::to_string(errno);
      return false;
    }
#endif
    return true;
  }

  void start(std::chrono::milliseconds timeout) {
    stopRequested.store(false);
    worker = std::thread([this, timeout] { run(timeout); });
  }

  void stop() {
    stopRequested.store(true);
    if (worker.joinable()) {
      worker.join();
    }
  }

  ~PipeReader() { stop(); }

  std::vector<std::string> lines() const {
    std::lock_guard<std::mutex> lock(mutex);
    return eventLines;
  }

  std::string errorMessage() const {
    std::lock_guard<std::mutex> lock(mutex);
    return error;
  }

private:
  void addLine(const std::string &line) {
    if (line.empty()) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    eventLines.push_back(line);
  }

  void setError(const std::string &message) {
    std::lock_guard<std::mutex> lock(mutex);
    if (error.empty()) {
      error = message;
    }
  }

  void consumeChunk(std::string &pending, const char *data, size_t size) {
    pending.append(data, size);
    size_t pos = 0;
    while (true) {
      size_t newline = pending.find('\n', pos);
      if (newline == std::string::npos) {
        if (pos > 0) {
          pending.erase(0, pos);
        }
        return;
      }
      std::string line = pending.substr(pos, newline - pos);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      addLine(line);
      pos = newline + 1;
    }
  }

  void flushPending(std::string &pending) {
    if (!pending.empty()) {
      if (!pending.empty() && pending.back() == '\r') {
        pending.pop_back();
      }
      addLine(pending);
      pending.clear();
    }
  }

#ifdef _WIN32
  static std::string normalizePipeName(const std::string &pipeName) {
    if (pipeName.rfind("\\\\.\\pipe\\", 0) == 0) {
      return pipeName;
    }
    return "\\\\.\\pipe\\" + pipeName;
  }

  void run(std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    const std::string fullPipeName = normalizePipeName(path);
    HANDLE handle = INVALID_HANDLE_VALUE;

    while (!stopRequested.load() && Clock::now() < deadline) {
      handle = CreateFileA(fullPipeName.c_str(), GENERIC_READ, 0, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (handle != INVALID_HANDLE_VALUE) {
        break;
      }
      DWORD lastError = GetLastError();
      if (lastError == ERROR_PIPE_BUSY) {
        WaitNamedPipeA(fullPipeName.c_str(), 100);
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }

    if (handle == INVALID_HANDLE_VALUE) {
      if (!stopRequested.load()) {
        setError(name + " pipe connection timeout: " + fullPipeName);
      }
      return;
    }

    std::string pending;
    char buffer[4096];
    DWORD read = 0;
    while (!stopRequested.load()) {
      BOOL ok = ReadFile(handle, buffer, sizeof(buffer), &read, NULL);
      if (ok && read > 0) {
        consumeChunk(pending, buffer, read);
        continue;
      }
      DWORD lastError = GetLastError();
      if (lastError == ERROR_BROKEN_PIPE ||
          lastError == ERROR_PIPE_NOT_CONNECTED || (ok && read == 0)) {
        break;
      }
      setError(name + " pipe read failed: " + std::to_string(lastError));
      break;
    }
    flushPending(pending);
    CloseHandle(handle);
  }
#else
  void run(std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    int fd = -1;
    while (!stopRequested.load() && Clock::now() < deadline) {
      fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
      if (fd != -1) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (fd == -1) {
      if (!stopRequested.load()) {
        setError(name + " FIFO open timeout: " + path);
      }
      return;
    }

    std::string pending;
    char buffer[4096];
    while (!stopRequested.load() && Clock::now() < deadline) {
      ssize_t n = read(fd, buffer, sizeof(buffer));
      if (n > 0) {
        consumeChunk(pending, buffer, static_cast<size_t>(n));
      } else if (n == 0 || errno == EAGAIN || errno == EWOULDBLOCK ||
                 errno == EINTR) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      } else {
        setError(name + " FIFO read failed: errno=" + std::to_string(errno));
        break;
      }
    }
    flushPending(pending);
    close(fd);
  }
#endif

  std::string name;
  std::string path;
  std::atomic<bool> stopRequested{false};
  mutable std::mutex mutex;
  std::vector<std::string> eventLines;
  std::string error;
  std::thread worker;
};

void printUsage(const char *program) {
  std::cout
      << "ResultPipeVerifier 0.1.0\n"
      << "Usage: " << program << " [options]\n\n"
      << "Options:\n"
      << "  --ipeftc-path <path>   Path to IPEFTC executable\n"
      << "  --host <ip>            Client target IP (default 127.0.0.1)\n"
      << "  --bind <ip>            Server bind IP (default 0.0.0.0)\n"
      << "  --port <port>          Test port (default 55301)\n"
      << "  --packet-size <bytes>  Packet size (default 1024)\n"
      << "  --num-packets <count>  Packet count (default 5)\n"
      << "  --interval-ms <ms>     Send interval (default 1)\n"
      << "  --run-id <id>          Run ID (default rpv-<timestamp>-<pid>)\n"
      << "  --result-dir <path>    Output directory (default ResultPipeVerifier/results/<runId>)\n"
      << "  --timeout-ms <ms>      Overall timeout (default 30000)\n"
      << "  -h, --help             Show this help\n";
}

bool parseIntOption(const std::string &name, const std::string &value,
                    int &target, std::vector<std::string> &errors) {
  try {
    size_t used = 0;
    int parsed = std::stoi(value, &used);
    if (used != value.size()) {
      throw std::invalid_argument("trailing characters");
    }
    target = parsed;
    return true;
  } catch (const std::exception &) {
    errors.push_back("Invalid integer for " + name + ": " + value);
    return false;
  }
}

Options parseArgs(int argc, char **argv, bool &help,
                  std::vector<std::string> &errors) {
  Options options;
  options.runId = makeDefaultRunId();

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto requireValue = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc) {
        errors.push_back("Missing value for " + name);
        return {};
      }
      return argv[++i];
    };

    if (arg == "-h" || arg == "--help") {
      help = true;
    } else if (arg == "--ipeftc-path") {
      options.ipeftcPath = requireValue(arg);
    } else if (arg == "--host") {
      options.host = requireValue(arg);
    } else if (arg == "--bind") {
      options.bind = requireValue(arg);
    } else if (arg == "--port") {
      parseIntOption(arg, requireValue(arg), options.port, errors);
    } else if (arg == "--packet-size") {
      parseIntOption(arg, requireValue(arg), options.packetSize, errors);
    } else if (arg == "--num-packets") {
      parseIntOption(arg, requireValue(arg), options.numPackets, errors);
    } else if (arg == "--interval-ms") {
      parseIntOption(arg, requireValue(arg), options.intervalMs, errors);
    } else if (arg == "--run-id") {
      options.runId = requireValue(arg);
    } else if (arg == "--result-dir") {
      options.resultDir = requireValue(arg);
    } else if (arg == "--timeout-ms") {
      parseIntOption(arg, requireValue(arg), options.timeoutMs, errors);
    } else {
      errors.push_back("Unknown argument: " + arg);
    }
  }

  if (options.resultDir.empty()) {
    options.resultDir = fs::path("ResultPipeVerifier") / "results" /
                        sanitizeName(options.runId);
  }
  if (options.port <= 0 || options.port > 65535) {
    errors.push_back("--port must be between 1 and 65535");
  }
  if (options.packetSize <= 0) {
    errors.push_back("--packet-size must be positive");
  }
  if (options.numPackets <= 0) {
    errors.push_back("--num-packets must be positive for this smoke test");
  }
  if (options.intervalMs < 0) {
    errors.push_back("--interval-ms must be >= 0");
  }
  if (options.timeoutMs <= 0) {
    errors.push_back("--timeout-ms must be positive");
  }
  return options;
}

std::vector<std::string> serverArgs(const Options &options,
                                    const std::string &pipeName) {
  return {"--mode",        "server",
          "--target",      options.bind,
          "--port",        std::to_string(options.port),
          "--run-id",      options.runId,
          "--result-dir",  options.resultDir.string(),
          "--result-pipe", pipeName,
          "--save-logs",   "false"};
}

std::vector<std::string> clientArgs(const Options &options,
                                    const std::string &pipeName) {
  return {"--mode",
          "client",
          "--target",
          options.host,
          "--port",
          std::to_string(options.port),
          "--packet-size",
          std::to_string(options.packetSize),
          "--num-packets",
          std::to_string(options.numPackets),
          "--interval-ms",
          std::to_string(options.intervalMs),
          "--run-id",
          options.runId,
          "--result-dir",
          options.resultDir.string(),
          "--result-pipe",
          pipeName,
          "--save-logs",
          "false"};
}

std::string pipeNameForRole(const Options &options, const std::string &role) {
  const std::string safeRunId = sanitizeName(options.runId);
#ifdef _WIN32
  return "rpv-" + safeRunId + "-" + role;
#else
  return (options.resultDir / (role + ".fifo")).string();
#endif
}

json parseJsonLine(const std::string &line, std::vector<std::string> &errors,
                   const std::string &label, size_t index) {
  try {
    return json::parse(line);
  } catch (const std::exception &e) {
    errors.push_back(label + " event " + std::to_string(index) +
                     " is not valid JSON: " + e.what());
    return json::object();
  }
}

bool expectString(const json &event, const std::string &key,
                  const std::string &expected, std::vector<std::string> &errors,
                  const std::string &context) {
  if (!event.contains(key) || !event.at(key).is_string()) {
    errors.push_back(context + " missing string field '" + key + "'");
    return false;
  }
  const std::string actual = event.at(key).get<std::string>();
  if (actual != expected) {
    errors.push_back(context + " field '" + key + "' mismatch: actual=" +
                     actual + " expected=" + expected);
    return false;
  }
  return true;
}

void verifyEvents(const std::string &label, const std::string &role,
                  const std::vector<std::string> &lines,
                  const std::string &runId, std::vector<std::string> &errors) {
  const std::vector<std::string> expectedTypes{"run_started", "phase_result",
                                               "phase_result", "final_result"};
  if (lines.size() != expectedTypes.size()) {
    errors.push_back(label + " expected 4 events but got " +
                     std::to_string(lines.size()));
  }

  std::vector<json> events;
  for (size_t i = 0; i < lines.size(); ++i) {
    events.push_back(parseJsonLine(lines[i], errors, label, i));
  }

  const size_t count = std::min(events.size(), expectedTypes.size());
  for (size_t i = 0; i < count; ++i) {
    const std::string context = label + " event " + std::to_string(i);
    expectString(events[i], "type", expectedTypes[i], errors, context);
    expectString(events[i], "runId", runId, errors, context);
    expectString(events[i], "role", role, errors, context);
  }

  if (events.size() >= 3) {
    for (size_t i = 1; i <= 2; ++i) {
      const std::string context = label + " phase event " + std::to_string(i);
      if (!events[i].contains("phaseNumber") ||
          !events[i].at("phaseNumber").is_number_integer()) {
        errors.push_back(context + " missing integer phaseNumber");
      } else if (events[i].at("phaseNumber").get<int>() !=
                 static_cast<int>(i)) {
        errors.push_back(context + " phaseNumber mismatch");
      }
      if (!events[i].contains("phase") || !events[i].at("phase").is_object()) {
        errors.push_back(context + " missing phase object");
      }
    }
  }

  if (events.size() >= 4) {
    const json &finalEvent = events[3];
    const std::string context = label + " final_result";
    if (!finalEvent.contains("result") || !finalEvent.at("result").is_object()) {
      errors.push_back(context + " missing result object");
      return;
    }
    const json &result = finalEvent.at("result");
    if (!result.value("success", false)) {
      errors.push_back(context + " result.success is not true");
    }
    if (result.value("finalState", "") != "FINISHED") {
      errors.push_back(context + " result.finalState is not FINISHED");
    }
    if (result.value("role", "") != role) {
      errors.push_back(context + " result.role mismatch");
    }
    if (result.value("runId", "") != runId) {
      errors.push_back(context + " result.runId mismatch");
    }
  }
}

void verifyResultFile(const fs::path &path, const std::string &role,
                      const std::string &runId,
                      std::vector<std::string> &errors) {
  std::ifstream in(path);
  if (!in.is_open()) {
    errors.push_back("Result JSON not found: " + path.string());
    return;
  }
  try {
    json root = json::parse(in);
    if (root.value("runId", "") != runId) {
      errors.push_back(path.string() + " runId mismatch");
    }
    if (root.value("role", "") != role) {
      errors.push_back(path.string() + " role mismatch");
    }
    if (!root.value("success", false)) {
      errors.push_back(path.string() + " success is not true");
    }
    if (root.value("finalState", "") != "FINISHED") {
      errors.push_back(path.string() + " finalState is not FINISHED");
    }
  } catch (const std::exception &e) {
    errors.push_back("Failed to parse " + path.string() + ": " + e.what());
  }
}

json buildReport(const Options &options, const std::string &ipeftcPath,
                 const ProcessResult &serverResult,
                 const ProcessResult &clientResult,
                 const std::vector<std::string> &serverEvents,
                 const std::vector<std::string> &clientEvents,
                 const std::vector<std::string> &errors, bool passed) {
  json report;
  report["passed"] = passed;
  report["errors"] = errors;
  report["ipeftcPath"] = ipeftcPath;
  report["options"] = {
      {"host", options.host},
      {"bind", options.bind},
      {"port", options.port},
      {"packetSize", options.packetSize},
      {"numPackets", options.numPackets},
      {"intervalMs", options.intervalMs},
      {"runId", options.runId},
      {"resultDir", options.resultDir.string()},
      {"timeoutMs", options.timeoutMs},
  };
  report["processes"] = {
      {"server",
       {{"started", serverResult.started},
        {"timedOut", serverResult.timedOut},
        {"exitCode", serverResult.exitCode}}},
      {"client",
       {{"started", clientResult.started},
        {"timedOut", clientResult.timedOut},
        {"exitCode", clientResult.exitCode}}},
  };
  report["events"] = {
      {"serverCount", serverEvents.size()},
      {"clientCount", clientEvents.size()},
      {"serverFile", (options.resultDir / "server-events.jsonl").string()},
      {"clientFile", (options.resultDir / "client-events.jsonl").string()},
  };
  report["resultFiles"] = {
      {"server", (options.resultDir /
                   ("result-" + options.runId + "-SERVER.json"))
                      .string()},
      {"client", (options.resultDir /
                   ("result-" + options.runId + "-CLIENT.json"))
                      .string()},
  };
  return report;
}

int runVerifier(const Options &options, const std::string &ipeftcPath) {
  std::vector<std::string> errors;
  std::error_code ec;
  fs::create_directories(options.resultDir, ec);
  if (ec) {
    std::cerr << "FAIL: failed to create result directory "
              << options.resultDir << ": " << ec.message() << "\n";
    return 1;
  }

  const std::string serverPipe = pipeNameForRole(options, "server");
  const std::string clientPipe = pipeNameForRole(options, "client");
  PipeReader serverReader("server", serverPipe);
  PipeReader clientReader("client", clientPipe);

  std::string prepError;
  if (!serverReader.prepare(prepError)) {
    std::cerr << "FAIL: " << prepError << "\n";
    return 1;
  }
  if (!clientReader.prepare(prepError)) {
    std::cerr << "FAIL: " << prepError << "\n";
    return 1;
  }

  const auto timeout = std::chrono::milliseconds(options.timeoutMs);
  serverReader.start(timeout);
  clientReader.start(timeout);

  ChildProcess server;
  ChildProcess client;

  std::cout << "ResultPipeVerifier starting\n"
            << "  IPEFTC: " << ipeftcPath << "\n"
            << "  Run ID: " << options.runId << "\n"
            << "  Result dir: " << options.resultDir << "\n"
            << "  Server pipe: " << serverPipe << "\n"
            << "  Client pipe: " << clientPipe << "\n";

  std::string processError;
  if (!server.start(ipeftcPath, serverArgs(options, serverPipe), processError)) {
    errors.push_back("Failed to start server: " + processError);
  } else if (!server.waitForOutput("Transitioning to state: ACCEPTING",
                                   std::chrono::milliseconds(
                                       std::min(options.timeoutMs, 10000)))) {
    if (!server.waitForOutput("Server is running. Waiting for the test to complete",
                              std::chrono::milliseconds(1))) {
      errors.push_back("Server did not become ready before timeout");
    }
  }

  if (errors.empty()) {
    if (!client.start(ipeftcPath, clientArgs(options, clientPipe),
                      processError)) {
      errors.push_back("Failed to start client: " + processError);
    }
  }

  if (errors.empty()) {
    if (!client.waitFor(timeout)) {
      errors.push_back("Client timed out");
    }
    if (!server.waitFor(timeout)) {
      errors.push_back("Server timed out");
    }
  } else {
    client.terminate();
    server.terminate();
    client.waitFor(std::chrono::milliseconds(1000));
    server.waitFor(std::chrono::milliseconds(1000));
  }

  serverReader.stop();
  clientReader.stop();

  auto serverResult = server.result();
  auto clientResult = client.result();
  auto serverEvents = serverReader.lines();
  auto clientEvents = clientReader.lines();

  std::string writeError;
  writeTextFile(options.resultDir / "server-output.log", serverResult.output,
                writeError);
  if (!writeError.empty()) {
    errors.push_back(writeError);
    writeError.clear();
  }
  writeTextFile(options.resultDir / "client-output.log", clientResult.output,
                writeError);
  if (!writeError.empty()) {
    errors.push_back(writeError);
    writeError.clear();
  }
  writeTextFile(options.resultDir / "server-events.jsonl",
                linesToText(serverEvents), writeError);
  if (!writeError.empty()) {
    errors.push_back(writeError);
    writeError.clear();
  }
  writeTextFile(options.resultDir / "client-events.jsonl",
                linesToText(clientEvents), writeError);
  if (!writeError.empty()) {
    errors.push_back(writeError);
    writeError.clear();
  }

  if (!serverReader.errorMessage().empty()) {
    errors.push_back(serverReader.errorMessage());
  }
  if (!clientReader.errorMessage().empty()) {
    errors.push_back(clientReader.errorMessage());
  }

  if (serverResult.started && serverResult.exitCode != 0) {
    errors.push_back("Server exit code is " +
                     std::to_string(serverResult.exitCode));
  }
  if (clientResult.started && clientResult.exitCode != 0) {
    errors.push_back("Client exit code is " +
                     std::to_string(clientResult.exitCode));
  }

  verifyEvents("server", "SERVER", serverEvents, options.runId, errors);
  verifyEvents("client", "CLIENT", clientEvents, options.runId, errors);
  verifyResultFile(options.resultDir /
                       ("result-" + options.runId + "-SERVER.json"),
                   "SERVER", options.runId, errors);
  verifyResultFile(options.resultDir /
                       ("result-" + options.runId + "-CLIENT.json"),
                   "CLIENT", options.runId, errors);

  const bool passed = errors.empty();
  json report = buildReport(options, ipeftcPath, serverResult, clientResult,
                            serverEvents, clientEvents, errors, passed);
  writeJsonFile(options.resultDir / "verification-report.json", report,
                writeError);
  if (!writeError.empty()) {
    std::cerr << "Warning: " << writeError << "\n";
  }

  std::cout << "\n--- ResultPipeVerifier Summary ---\n"
            << "Status: " << (passed ? "PASS" : "FAIL") << "\n"
            << "Server events: " << serverEvents.size() << "\n"
            << "Client events: " << clientEvents.size() << "\n"
            << "Server exit code: " << serverResult.exitCode << "\n"
            << "Client exit code: " << clientResult.exitCode << "\n"
            << "Report: "
            << (options.resultDir / "verification-report.json") << "\n";

  if (!passed) {
    std::cout << "\nFailures:\n";
    for (const auto &error : errors) {
      std::cout << "  - " << error << "\n";
    }
  }

  return passed ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
  bool help = false;
  std::vector<std::string> errors;
  Options options = parseArgs(argc, argv, help, errors);

  if (help) {
    printUsage(argv[0]);
    return 0;
  }
  if (!errors.empty()) {
    for (const auto &error : errors) {
      std::cerr << "Error: " << error << "\n";
    }
    printUsage(argv[0]);
    return 2;
  }

  std::vector<std::string> searched;
  const std::string ipeftcPath = resolveIpeftcPath(options.ipeftcPath, searched);
  if (ipeftcPath.empty()) {
    std::cerr << "Error: IPEFTC executable was not found. Searched:\n";
    for (const auto &path : searched) {
      std::cerr << "  - " << path << "\n";
    }
    return 2;
  }

  return runVerifier(options, ipeftcPath);
}

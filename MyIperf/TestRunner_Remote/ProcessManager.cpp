#include "ProcessManager.h"
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

namespace TestRunner2 {

std::string ProcessManager::GetIPEFTCPath() {
  if (!overrideIPEFTCPath.empty()) {
    return overrideIPEFTCPath;
  }
  // Try default relative path from cmake build structure:
  // TestRunner2/build/Release/TestRunner2.exe ->
  // TestRunner2/../build/Release/IPEFTC.exe (This logic depends on where
  // TestRunner is run from) Assuming standard build:
  // build/Release/TestRunner_Remote.exe
  // We want to find IPEFTC.exe in the same directory or specific relative path.
  // For now, assume it's in the same directory or ../Release/IPEFTC.exe
  return "IPEFTC.exe"; // Rely on PATH or being in same dir for simplicity, can
                       // be overridden by args
}

void ProcessManager::SetIPEFTCPath(const std::string &path) {
  overrideIPEFTCPath = path;
}

bool ProcessManager::CreatePipes(HANDLE &hRead, HANDLE &hWrite) {
  SECURITY_ATTRIBUTES saAttr;
  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  if (!CreatePipe(&hRead, &hWrite, &saAttr, 0)) {
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
  std::string cmdLine = ipeftcPath + " --mode server";
  if (!config.targetIP.empty()) {
    cmdLine += " --target " + config.targetIP;
  }
  if (config.port > 0) {
    cmdLine += " --port " + std::to_string(config.port);
  }
  cmdLine +=
      config.saveLogs == true ? " --save-logs true" : " --save-logs false";

  if (!CreatePipes(handles.stdOutRead, handles.stdOutWrite)) {
    std::cerr << "Failed to create pipes" << std::endl;
    return false;
  }

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.hStdError = handles.stdOutWrite;
  si.hStdOutput = handles.stdOutWrite;
  si.dwFlags |= STARTF_USESTDHANDLES;

  if (!CreateProcessA(NULL, const_cast<char *>(cmdLine.c_str()), NULL, NULL,
                      TRUE, 0, NULL, NULL, &si, &handles.processInfo)) {
    std::cerr << "CreateProcess failed (" << GetLastError()
              << "). Cmd: " << cmdLine << std::endl;
    CloseHandle(handles.stdOutRead);
    CloseHandle(handles.stdOutWrite);
    return false;
  }

  return true;
}

bool ProcessManager::LaunchIPEFTCClient(const std::string &ipeftcPath,
                                        const TestConfig &config,
                                        ProcessHandles &handles) {

  std::string cmdLine = ipeftcPath + " --mode client";
  if (!config.targetIP.empty()) {
    cmdLine += " --target " + config.targetIP;
  }
  if (config.port > 0) {
    cmdLine += " --port " + std::to_string(config.port);
  }
  if (config.packetSize > 0) {
    cmdLine += " --packet-size " + std::to_string(config.packetSize);
  }
  if (config.numPackets > 0) {
    cmdLine += " --num-packets " + std::to_string(config.numPackets);
  }
  cmdLine +=
      config.saveLogs == true ? " --save-logs true" : " --save-logs false";

  if (config.sendIntervalMs > 0) {
    cmdLine += " --interval-ms " + std::to_string(config.sendIntervalMs);
  }

  if (!CreatePipes(handles.stdOutRead, handles.stdOutWrite)) {
    std::cerr << "Failed to create pipes" << std::endl;
    return false;
  }

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.hStdError = handles.stdOutWrite;
  si.hStdOutput = handles.stdOutWrite;
  si.dwFlags |= STARTF_USESTDHANDLES;

  if (!CreateProcessA(NULL, const_cast<char *>(cmdLine.c_str()), NULL, NULL,
                      TRUE, 0, NULL, NULL, &si, &handles.processInfo)) {
    std::cerr << "CreateProcess failed (" << GetLastError()
              << "). Cmd: " << cmdLine << std::endl;
    CloseHandle(handles.stdOutRead);
    CloseHandle(handles.stdOutWrite);
    return false;
  }

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

  std::string output;
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

TestResult ProcessManager::ParseOutput(const std::string &output,
                                       const std::string &role, int port) {
  TestResult result;
  result.role = role;
  result.port = port;

  // Simple regex parsing based on typical MyIperf output
  // Adjust these regexes to match actual output format of IPEFTC
  // Example IPEFTC output line:
  // "Test Duration: 5.00 s"
  // "Or throughput lines..."

  // NOTE: This parsing logic needs to be robust against variation.
  // Using simple string search for now if regex is too brittle, or specific key
  // phrases.

  try {
    // 1. Parse Duration
    std::regex durationRegex(R"(Test Duration:\s*([\d\.]+)\s*s)");
    std::smatch match;
    if (std::regex_search(output, match, durationRegex)) {
      result.duration = std::stod(match[1]);
    }

    // 2. Parse Throughput
    // Looking for "Throughput: 123.45 Mbps" or similar
    std::regex throughputRegex(R"(Throughput:\s*([\d\.]+)\s*Mbps)");
    if (std::regex_search(output, match, throughputRegex)) {
      result.throughput = std::stod(match[1]);
    }

    // 3. Parse Total Bytes
    // Look for "Total Bytes Received: 123456" OR "Total Bytes Sent: 123456"
    // Since we are parsing based on Role, we might want to be specific, or just
    // grab the one that appears in the summary block. The IPEFTC summary prints
    // both, but usually we care about "Sent" for Sender and "Received" for
    // Receiver. However, the ProcessManager doesn't fully know if it's looking
    // at the "sender" or "receiver" summary block specifically if they are
    // mixed. But typically: Client (Sender) log has "Client-side (sent): ..."
    // Server (Receiver) log has "Server-side (received): ..."
    // Let's try to capture the relevant one.

    // Simplification: Match "Total Bytes.*:\s*(\d+)" and likely take the last
    // one or specific one? Actually, relying on the test role passed in `role`
    // might be better or just grabbing "Total Bytes Sent" or "Total Bytes
    // Received" If successful, IPEFTC usually prints a final summary.

    // Try explicit matches first
    std::regex bytesSentRegex(R"(Total Bytes Sent:\s*(\d+))");
    std::regex bytesRecvRegex(R"(Total Bytes Received:\s*(\d+))");

    if (std::regex_search(output, match, bytesSentRegex)) {
      long long val = std::stoll(match[1]);
      if (val > 0)
        result.totalBytes = val; // Prioritize non-zero
    }
    if (std::regex_search(output, match, bytesRecvRegex)) {
      long long val = std::stoll(match[1]);
      if (val > 0)
        result.totalBytes =
            val; // Overwrite if we found recv (mostly what we care about for
                 // integrity? or depends on role?)
      // Actually, for "Throughput", usually we care about what was Transferred.
      // For Client (Sender), Bytes Sent. For Server (Receiver), Bytes Received.
      // Let's refine based on role if possible, or just take the max?
      // Let's just catch *any* valid byte count found in the summary.
    }

    // 4. Parse Total Packets
    std::regex pktsSentRegex(R"(Total Packets Sent:\s*(\d+))");
    std::regex pktsRecvRegex(R"(Total Packets Received:\s*(\d+))");

    if (std::regex_search(output, match, pktsSentRegex)) {
      long long val = std::stoll(match[1]);
      if (val > 0)
        result.totalPackets = val;
    }
    if (std::regex_search(output, match, pktsRecvRegex)) {
      long long val = std::stoll(match[1]);
      if (val > 0)
        result.totalPackets = val;
    }

    // 5. Parse Error Counters (if any)
    std::regex seqErrRegex(R"(Sequence Errors:\s*(\d+))");
    if (std::regex_search(output, match, seqErrRegex)) {
      result.sequenceErrors = std::stoll(match[1]);
    }

    std::regex chkErrRegex(R"(Checksum Errors:\s*(\d+))");
    if (std::regex_search(output, match, chkErrRegex)) {
      result.checksumErrors = std::stoll(match[1]);
    }

    // Determine basic success based on parsed data presence
    if (result.totalPackets > 0) {
      result.success = true; // Refined later by AnalyzeTestResult
    }

  } catch (const std::exception &e) {
    result.failureReason = "Parsing exception: " + std::string(e.what());
    result.success = false;
  }

  // Fallback: if throughput is missing, try to calculate
  if (result.throughput == 0.0 && result.duration > 0 &&
      result.totalBytes > 0) {
    result.throughput =
        (result.totalBytes * 8.0) / (result.duration * 1000000.0);
  }

  return result;
}

} // namespace TestRunner2

#include "myiperf/TestController.h"

#include "ClientTestSession.h"
#include "ControlChannel.h"
#include "ControlMessageBus.h"
#include "ControlProtocol.h"
#include "NetworkInterfaceFactory.h"
#include "PacketGenerator.h"
#include "PacketReceiver.h"
#include "ResultEventSink.h"
#include "ServerTestSession.h"
#include "TestSessionContext.h"
#include "myiperf/Logger.h"
#include "myiperf/NetworkInterface.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

const char* stateToString(TestController::State state) {
  switch (state) {
  case TestController::State::IDLE:
    return "IDLE";
  case TestController::State::INITIALIZING:
    return "INITIALIZING";
  case TestController::State::CONNECTING:
    return "CONNECTING";
  case TestController::State::SENDING_CONFIG:
    return "SENDING_CONFIG";
  case TestController::State::WAITING_FOR_ACK:
    return "WAITING_FOR_ACK";
  case TestController::State::ACCEPTING:
    return "ACCEPTING";
  case TestController::State::WAITING_FOR_CONFIG:
    return "WAITING_FOR_CONFIG";
  case TestController::State::RUNNING_TEST:
    return "RUNNING_TEST";
  case TestController::State::FINISHING:
    return "FINISHING";
  case TestController::State::EXCHANGING_STATS:
    return "EXCHANGING_STATS";
  case TestController::State::WAITING_FOR_CLIENT_READY:
    return "WAITING_FOR_CLIENT_READY";
  case TestController::State::RUNNING_SERVER_TEST:
    return "RUNNING_SERVER_TEST";
  case TestController::State::WAITING_FOR_SERVER_FIN:
    return "WAITING_FOR_SERVER_FIN";
  case TestController::State::SERVER_TEST_FINISHING:
    return "SERVER_TEST_FINISHING";
  case TestController::State::EXCHANGING_SERVER_STATS:
    return "EXCHANGING_SERVER_STATS";
  case TestController::State::WAITING_FOR_SHUTDOWN_ACK:
    return "WAITING_FOR_SHUTDOWN_ACK";
  case TestController::State::FINISHED:
    return "FINISHED";
  case TestController::State::ERRORED:
    return "ERRORED";
  default:
    return "UNKNOWN";
  }
}

std::string nowIsoString() {
  auto now = std::chrono::system_clock::now();
  std::time_t now_c = std::chrono::system_clock::to_time_t(now);
  std::tm local_tm{};
#ifdef _WIN32
  localtime_s(&local_tm, &now_c);
#else
  localtime_r(&now_c, &local_tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&local_tm, "%Y-%m-%dT%H:%M:%S");
  return oss.str();
}

int currentProcessId() {
#ifdef _WIN32
  return _getpid();
#else
  return getpid();
#endif
}

std::string generateRunId() {
  static std::atomic<unsigned long long> counter{0};
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  std::ostringstream oss;
  oss << millis << "-" << currentProcessId() << "-" << counter.fetch_add(1);
  return oss.str();
}

std::string roleString(Config::TestMode mode) {
  return mode == Config::TestMode::CLIENT ? "CLIENT" : "SERVER";
}

bool replaceFile(const std::filesystem::path& tmp,
                 const std::filesystem::path& dest,
                 std::string& error) {
#ifdef _WIN32
  if (!MoveFileExA(tmp.string().c_str(), dest.string().c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = "MoveFileEx failed for " + dest.string() + " (error=" +
            std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
#else
  std::error_code ec;
  std::filesystem::rename(tmp, dest, ec);
  if (ec) {
    error = "rename failed for " + dest.string() + ": " + ec.message();
    return false;
  }
  return true;
#endif
}

std::filesystem::path makeTempPath(const std::filesystem::path& path) {
  static std::atomic<unsigned long long> tempCounter{0};
  std::filesystem::path tmp = path;
  tmp += ".";
  tmp += std::to_string(currentProcessId());
  tmp += ".";
  tmp += std::to_string(tempCounter.fetch_add(1));
  tmp += ".tmp";
  return tmp;
}

bool writeJsonAtomically(const std::filesystem::path& path,
                         const nlohmann::json& json,
                         std::string& error) {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "Failed to create result directory " +
              path.parent_path().string() + ": " + ec.message();
      return false;
    }
  }

  std::filesystem::path tmp = makeTempPath(path);

  {
    std::ofstream out(tmp, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      error = "Failed to open result temp file " + tmp.string();
      return false;
    }
    out << json.dump(2) << '\n';
    if (!out.good()) {
      error = "Failed to write result temp file " + tmp.string();
      return false;
    }
  }

  return replaceFile(tmp, path, error);
}

std::string combineReasons(const std::string& left, const std::string& right) {
  if (left.empty()) {
    return right;
  }
  if (right.empty()) {
    return left;
  }
  return left + "; " + right;
}

std::string validateReceiverStats(const TestStats& receiver,
                                  const Config& config) {
  std::string reason;
  const long long expectedPackets = config.getNumPackets();
  const long long expectedBytes =
      expectedPackets > 0
          ? static_cast<long long>(config.getPacketSize()) * expectedPackets
          : 0;

  if (expectedPackets > 0 &&
      receiver.totalPacketsReceived != expectedPackets) {
    reason = combineReasons(reason,
                            "Packet count mismatch (Rx: " +
                                std::to_string(receiver.totalPacketsReceived) +
                                ", Exp: " + std::to_string(expectedPackets) +
                                ")");
  }
  if (expectedBytes > 0 && receiver.totalBytesReceived != expectedBytes) {
    reason = combineReasons(reason,
                            "Byte count mismatch (Rx: " +
                                std::to_string(receiver.totalBytesReceived) +
                                ", Exp: " + std::to_string(expectedBytes) +
                                ")");
  }
  if (receiver.failedChecksumCount > 0) {
    reason = combineReasons(reason,
                            "Checksum errors detected (" +
                                std::to_string(receiver.failedChecksumCount) +
                                ")");
  }
  if (receiver.sequenceErrorCount > 0) {
    reason = combineReasons(reason,
                            "Sequence errors detected (" +
                                std::to_string(receiver.sequenceErrorCount) +
                                ")");
  }
  if (receiver.contentMismatchCount > 0) {
    reason = combineReasons(reason,
                            "Content mismatches detected (" +
                                std::to_string(receiver.contentMismatchCount) +
                                ")");
  }
  return reason;
}

} // namespace

TestController::TestController()
    : networkInterface(createNetworkInterface()),
      packetGenerator(std::make_unique<PacketGenerator>(networkInterface.get())),
      packetReceiver(std::make_unique<PacketReceiver>(networkInterface.get())),
      controlMessages(std::make_unique<ControlMessageBus>()),
      controlChannel(
          std::make_unique<ControlChannel>(*networkInterface, *controlMessages)),
      resultEventSink(std::make_unique<ResultEventSink>()) {
  reset();
}

TestController::~TestController() {
  stopTest();
}

void TestController::reset() {
  if (resultEventSink) {
    resultEventSink->stop();
  }

  currentState = State::IDLE;
  m_stopped = false;
  testCompletionPromise_set = false;
  m_cliBlockFlag = false;
  m_resultFinalized = false;
  m_testStarted = false;
  m_phase1EventPublished = false;
  m_phase2EventPublished = false;

  currentConfig = Config();
  currentRunOptions = RunOptions{};
  m_clientStatsPhase1 = {};
  m_serverStatsPhase1 = {};
  m_clientStatsPhase2 = {};
  m_serverStatsPhase2 = {};
  m_startedAt.clear();
  m_resultExportWarning.clear();
  {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_lastResult = TestRunResult{};
  }

  if (packetGenerator) {
    packetGenerator->resetStats();
  }
  if (packetReceiver) {
    packetReceiver->resetStats();
  }
  if (controlChannel) {
    controlChannel->clear();
  }

  testCompletionPromise = std::promise<void>();
}

nlohmann::json
TestController::parseStats(const std::vector<char>& payload) const {
  return ControlProtocol::parseJsonPayload(payload);
}

void TestController::startTest(const Config& config) {
  startTest(config, RunOptions{});
}

void TestController::startTest(const Config& config, const RunOptions& options) {
  reset();
  currentConfig = config;
  currentRunOptions = options;
  if (currentRunOptions.runId.empty()) {
    currentRunOptions.runId = generateRunId();
  }
  if (currentRunOptions.resultDir.empty()) {
    currentRunOptions.resultDir = "Results";
  }
  m_startedAt = nowIsoString();
  m_testStarted = true;

  std::string logMessage = "Info: Starting test in ";
  logMessage +=
      (config.getMode() == Config::TestMode::CLIENT ? "CLIENT" : "SERVER");
  logMessage += " mode.";
  Logger::log(logMessage);
  Logger::log("Info: Test run ID: " + currentRunOptions.runId);

  if (resultEventSink) {
    resultEventSink->start(currentRunOptions.resultPipe);
  }
  publishRunStarted();

  mainTestTask = runTestCoroutine();
  mainTestTask.start();
}

std::future<TestRunResult>
TestController::runTestAsync(const Config& config, const RunOptions& options) {
  return std::async(std::launch::async, [this, config, options] {
    startTest(config, options);
    getTestCompletionFuture().wait();
    return getLastResult();
  });
}

TestRunResult TestController::getLastResult() const {
  std::lock_guard<std::mutex> lock(m_resultMutex);
  return m_lastResult;
}

bool TestController::completedSuccessfully() const {
  if (currentState.load() != State::FINISHED) {
    return false;
  }
  if (!m_testStarted.load(std::memory_order_acquire) ||
      !m_resultFinalized.load(std::memory_order_acquire)) {
    return true;
  }
  std::lock_guard<std::mutex> lock(m_resultMutex);
  return m_lastResult.success;
}

void TestController::stopTest() {
  Logger::log("Debug: TestController::stopTest() called.");
  if (m_stopped.exchange(true)) {
    Logger::log(
        "Debug: TestController::stopTest() already stopped, returning.");
    return;
  }
  Logger::log("Info: Stopping the test components.");

  if (controlChannel) {
    controlChannel->cancelAll();
  }

  Logger::log("Debug: Calling packetGenerator->stop().");
  packetGenerator->stop();
  Logger::log("Debug: packetGenerator->stop() completed.");
  Logger::log("Debug: Calling packetReceiver->stop().");
  packetReceiver->stop();
  Logger::log("Debug: packetReceiver->stop() completed.");
  Logger::log("Debug: Calling networkInterface->close().");
  networkInterface->close();
  Logger::log("Debug: networkInterface->close() completed.");
  Logger::log("Debug: TestController::stopTest() finished.");

  if (m_testStarted.load(std::memory_order_acquire)) {
    finalizeResultOnce("Test stopped before completion");
  }
}

void TestController::transitionTo(State newState) {
  std::lock_guard<std::mutex> lock(m_stateMachineMutex);
  transitionTo_nolock(newState);
}

void TestController::transitionTo_nolock(State newState) {
  currentState = newState;
  Logger::log("Info: Transitioning to state: " +
              std::string(stateToString(newState)));
}

Task TestController::runTestCoroutine() {
  Logger::log("Coroutine: Starting test coroutine.");

  TestSessionContext context{
      currentConfig,
      *networkInterface,
      *packetGenerator,
      *packetReceiver,
      *controlChannel,
      m_clientStatsPhase1,
      m_serverStatsPhase1,
      m_clientStatsPhase2,
      m_serverStatsPhase2,
      [this](State state) { transitionTo(state); },
      [this](int phaseNumber) { publishPhaseResult(phaseNumber); },
  };

  std::string failureReason;
  try {
    if (currentConfig.getMode() == Config::TestMode::CLIENT) {
      ClientTestSession session(context);
      co_await session.run();
    } else {
      ServerTestSession session(context);
      co_await session.run();
    }
  } catch (const std::exception& e) {
    failureReason = e.what();
    Logger::log(std::string("Coroutine Error: ") + failureReason);
    transitionTo(State::ERRORED);
  }

  finalizeResultOnce(failureReason);
  Logger::log("Coroutine: Test coroutine finished.");
}

void TestController::signalCompletion() {
  if (!testCompletionPromise_set.exchange(true)) {
    testCompletionPromise.set_value();
  }

  {
    std::lock_guard<std::mutex> lock(m_cliBlockMutex);
    m_cliBlockFlag = true;
  }
  m_cliBlockCv.notify_all();
}

TestRunResult
TestController::buildCurrentResult(const std::string& failureReason) const {
  TestRunResult result;
  result.runId = currentRunOptions.runId;
  result.role = roleString(currentConfig.getMode());
  result.startedAt = m_startedAt;
  result.finishedAt = nowIsoString();
  result.finalState = stateToString(currentState.load());
  result.config = currentConfig;
  result.resultExportWarning = m_resultExportWarning;

  result.phase1.phaseName = "client_to_server";
  result.phase1.senderRole = "CLIENT";
  result.phase1.receiverRole = "SERVER";
  result.phase1.senderStats = m_clientStatsPhase1;
  result.phase1.receiverStats = m_serverStatsPhase1;
  result.phase1.failureReason =
      validateReceiverStats(result.phase1.receiverStats, currentConfig);
  result.phase1.success = result.phase1.failureReason.empty();

  result.phase2.phaseName = "server_to_client";
  result.phase2.senderRole = "SERVER";
  result.phase2.receiverRole = "CLIENT";
  result.phase2.senderStats = m_serverStatsPhase2;
  result.phase2.receiverStats = m_clientStatsPhase2;
  result.phase2.failureReason =
      validateReceiverStats(result.phase2.receiverStats, currentConfig);
  result.phase2.success = result.phase2.failureReason.empty();

  const bool finished = currentState.load() == State::FINISHED;
  result.success = finished && result.phase1.success && result.phase2.success;
  if (!failureReason.empty()) {
    result.failureReason = failureReason;
  }
  if (!finished) {
    result.failureReason =
        combineReasons(result.failureReason,
                       "Final state is " + result.finalState);
  }
  if (!result.phase1.success) {
    result.failureReason =
        combineReasons(result.failureReason,
                       "Phase 1 failed: " + result.phase1.failureReason);
  }
  if (!result.phase2.success) {
    result.failureReason =
        combineReasons(result.failureReason,
                       "Phase 2 failed: " + result.phase2.failureReason);
  }
  return result;
}

std::string TestController::exportResult(const TestRunResult& result) {
  std::string warning;
  nlohmann::json json = result;
  const std::string role = result.role.empty() ? "UNKNOWN" : result.role;
  const std::filesystem::path resultDir(currentRunOptions.resultDir);

  auto writeOne = [&](const std::filesystem::path& path) {
    std::string error;
    if (!writeJsonAtomically(path, json, error)) {
      warning = combineReasons(warning, error);
      Logger::log("Warning: " + error);
    }
  };

  writeOne(resultDir / ("result-" + result.runId + "-" + role + ".json"));
  writeOne(resultDir / ("latest-" + role + ".json"));
  if (!currentRunOptions.resultJson.empty()) {
    writeOne(std::filesystem::path(currentRunOptions.resultJson));
  }
  return warning;
}

void TestController::finalizeResultOnce(const std::string& failureReason) {
  if (!m_resultFinalized.exchange(true, std::memory_order_acq_rel)) {
    TestRunResult result = buildCurrentResult(failureReason);
    std::string exportWarning = exportResult(result);
    if (!exportWarning.empty()) {
      result.resultExportWarning = exportWarning;
      m_resultExportWarning = exportWarning;
    }

    {
      std::lock_guard<std::mutex> lock(m_resultMutex);
      m_lastResult = result;
    }

    if (resultEventSink && resultEventSink->enabled()) {
      nlohmann::json event;
      event["type"] = "final_result";
      event["runId"] = result.runId;
      event["role"] = result.role;
      event["result"] = result;
      resultEventSink->publish(event);
      resultEventSink->stop();
    }
  }

  signalCompletion();
}

void TestController::publishRunStarted() {
  if (!resultEventSink || !resultEventSink->enabled()) {
    return;
  }

  nlohmann::json event;
  event["type"] = "run_started";
  event["runId"] = currentRunOptions.runId;
  event["role"] = roleString(currentConfig.getMode());
  event["startedAt"] = m_startedAt;
  event["config"] = currentConfig.toJson();
  resultEventSink->publish(event);
}

void TestController::publishPhaseResult(int phaseNumber) {
  if (!resultEventSink || !resultEventSink->enabled()) {
    return;
  }
  if (phaseNumber == 1 &&
      m_phase1EventPublished.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  if (phaseNumber == 2 &&
      m_phase2EventPublished.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

  TestRunResult snapshot = buildCurrentResult("");
  const TestPhaseResult& phase =
      phaseNumber == 1 ? snapshot.phase1 : snapshot.phase2;

  nlohmann::json event;
  event["type"] = "phase_result";
  event["runId"] = snapshot.runId;
  event["role"] = snapshot.role;
  event["phaseNumber"] = phaseNumber;
  event["phase"] = phase;
  resultEventSink->publish(event);
}

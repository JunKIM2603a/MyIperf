#include "myiperf/TestController.h"

#include "ClientTestSession.h"
#include "ControlChannel.h"
#include "ControlMessageBus.h"
#include "ControlProtocol.h"
#include "NetworkInterfaceFactory.h"
#include "PacketGenerator.h"
#include "PacketReceiver.h"
#include "ServerTestSession.h"
#include "TestSessionContext.h"
#include "myiperf/Logger.h"
#include "myiperf/NetworkInterface.h"

#include <memory>
#include <string>

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

} // namespace

TestController::TestController()
    : networkInterface(createNetworkInterface()),
      packetGenerator(std::make_unique<PacketGenerator>(networkInterface.get())),
      packetReceiver(std::make_unique<PacketReceiver>(networkInterface.get())),
      controlMessages(std::make_unique<ControlMessageBus>()),
      controlChannel(
          std::make_unique<ControlChannel>(*networkInterface, *controlMessages)) {
  reset();
}

TestController::~TestController() {
  stopTest();
}

void TestController::reset() {
  currentState = State::IDLE;
  m_stopped = false;
  testCompletionPromise_set = false;
  m_cliBlockFlag = false;

  currentConfig = Config();
  m_clientStatsPhase1 = {};
  m_serverStatsPhase1 = {};
  m_clientStatsPhase2 = {};
  m_serverStatsPhase2 = {};

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
  reset();
  currentConfig = config;

  std::string logMessage = "Info: Starting test in ";
  logMessage +=
      (config.getMode() == Config::TestMode::CLIENT ? "CLIENT" : "SERVER");
  logMessage += " mode.";
  Logger::log(logMessage);

  mainTestTask = runTestCoroutine();
  mainTestTask.start();
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

  signalCompletion();
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
  };

  try {
    if (currentConfig.getMode() == Config::TestMode::CLIENT) {
      ClientTestSession session(context);
      co_await session.run();
    } else {
      ServerTestSession session(context);
      co_await session.run();
    }
  } catch (const std::exception& e) {
    Logger::log(std::string("Coroutine Error: ") + e.what());
    transitionTo(State::ERRORED);
  }

  signalCompletion();
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

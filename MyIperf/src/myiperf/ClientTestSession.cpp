#include "ClientTestSession.h"

#include "ControlProtocol.h"
#include "myiperf/Logger.h"

#include <stdexcept>

ClientTestSession::ClientTestSession(TestSessionContext& context)
    : context(context) {}

Task ClientTestSession::run() {
  Logger::log("Coroutine: Running Client Logic");

  co_await connectAndHandshake();
  co_await runClientToServerPhase();
  co_await runServerToClientPhase();

  co_await context.control.send(MessageType::SHUTDOWN_ACK);
  Logger::log("CONTROL: Sent SHUTDOWN_ACK.");
  context.transitionTo(TestController::State::FINISHED);
}

void ClientTestSession::startReceiver() {
  context.control.attachReceiver(context.receiver);
}

[[noreturn]] void ClientTestSession::fail(const std::string& message) {
  Logger::log(message);
  context.transitionTo(TestController::State::ERRORED);
  throw std::runtime_error(message);
}

Task ClientTestSession::connectAndHandshake() {
  context.transitionTo(TestController::State::CONNECTING);
  if (!context.network.initialize("0.0.0.0", 0)) {
    fail("Error: Client init failed");
  }

  bool connected = co_await context.network.connect(context.config.getTargetIP(),
                                                   context.config.getPort());
  if (!connected) {
    fail("Error: Failed to connect to server");
  }
  Logger::log("Info: Client connected.");

  startReceiver();

  context.transitionTo(TestController::State::SENDING_CONFIG);
  const std::string configText = context.config.toJson().dump();
  std::vector<char> configPayload(configText.begin(), configText.end());
  co_await context.control.send(MessageType::CONFIG_HANDSHAKE,
                                std::move(configPayload));
  Logger::log("CONTROL: Sent CONFIG_HANDSHAKE.");

  context.transitionTo(TestController::State::WAITING_FOR_ACK);
  co_await context.control.waitFor(MessageType::CONFIG_ACK,
                                   context.config.getHandshakeTimeoutMs());
  Logger::log("CONTROL: Received CONFIG_ACK.");
}

Task ClientTestSession::runClientToServerPhase() {
  context.transitionTo(TestController::State::RUNNING_TEST);

  co_await context.generator.sendPackets(context.config);
  Logger::log("Info: Client generator finished.");

  context.transitionTo(TestController::State::FINISHING);
  co_await context.control.send(MessageType::TEST_FIN);
  Logger::log("CONTROL: Sent TEST_FIN for Phase 1.");

  context.transitionTo(TestController::State::EXCHANGING_STATS);
  co_await context.control.waitFor(MessageType::TEST_FIN);
  Logger::log("CONTROL: Received TEST_FIN from server for Phase 1.");

  TestStats clientStats = context.generator.getStats();
  context.generator.saveLastStats(clientStats);
  co_await context.control.send(MessageType::STATS_EXCHANGE,
                                ControlProtocol::statsToPayload(clientStats));
  Logger::log("CONTROL: Sent STATS_EXCHANGE for Phase 1.");

  auto statsAck = co_await context.control.waitFor(MessageType::STATS_ACK);
  Logger::log("CONTROL: Received STATS_ACK for Phase 1.");

  context.serverStatsPhase1 =
      ControlProtocol::parseStatsPayload(statsAck.payload);
  context.clientStatsPhase1 = context.generator.lastStats();

  ControlProtocol::logPhaseSummary("Test Phase 1 Summary",
                                   "Client-side (sent)",
                                   context.clientStatsPhase1,
                                   "Server-side (received)",
                                   context.serverStatsPhase1);
  context.notifyPhaseComplete(1);
}

Task ClientTestSession::runServerToClientPhase() {
  co_await context.control.send(MessageType::CLIENT_READY);
  Logger::log("CONTROL: Sent CLIENT_READY for Phase 2.");
  context.transitionTo(TestController::State::WAITING_FOR_SERVER_FIN);

  context.receiver.resetStats();

  co_await context.control.waitFor(MessageType::TEST_FIN);
  Logger::log("CONTROL: Received TEST_FIN from server for Phase 2.");

  context.transitionTo(TestController::State::EXCHANGING_SERVER_STATS);
  context.clientStatsPhase2 = context.receiver.getStats();

  co_await context.control.send(
      MessageType::STATS_EXCHANGE,
      ControlProtocol::statsToPayload(context.clientStatsPhase2));
  Logger::log("CONTROL: Sent STATS_EXCHANGE for Phase 2.");

  auto finalAck = co_await context.control.waitFor(MessageType::STATS_ACK);
  Logger::log("CONTROL: Received STATS_ACK for Phase 2.");
  context.serverStatsPhase2 =
      ControlProtocol::parseStatsPayload(finalAck.payload);

  ControlProtocol::logPhaseSummary("Test Phase 2 Summary",
                                   "Server-side (sent)",
                                   context.serverStatsPhase2,
                                   "Client-side (received)",
                                   context.clientStatsPhase2);
  context.notifyPhaseComplete(2);
}

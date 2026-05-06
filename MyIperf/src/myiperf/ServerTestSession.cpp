#include "ServerTestSession.h"

#include "ControlProtocol.h"
#include "myiperf/Logger.h"

#include <stdexcept>

ServerTestSession::ServerTestSession(TestSessionContext& context)
    : context(context) {}

Task ServerTestSession::run() {
  Logger::log("Coroutine: Running Server Logic");

  co_await acceptAndReceiveConfig();
  co_await runClientToServerPhase();
  co_await runServerToClientPhase();

  context.transitionTo(TestController::State::FINISHED);
}

void ServerTestSession::startReceiver() {
  context.control.attachReceiver(context.receiver);
}

[[noreturn]] void ServerTestSession::fail(const std::string& message) {
  Logger::log(message);
  context.transitionTo(TestController::State::ERRORED);
  throw std::runtime_error(message);
}

Task ServerTestSession::acceptAndReceiveConfig() {
  if (!context.network.prepareServer(context.config.getTargetIP(),
                                     context.config.getPort())) {
    fail("Error: Server init failed");
  }

  context.transitionTo(TestController::State::ACCEPTING);
  auto acceptResult = co_await context.network.accept();
  if (!acceptResult.success) {
    fail("Error: Accept failed");
  }
  Logger::log("Info: Client connected from " + acceptResult.clientIP);

  startReceiver();

  context.transitionTo(TestController::State::WAITING_FOR_CONFIG);
  auto configMessage =
      co_await context.control.waitFor(MessageType::CONFIG_HANDSHAKE);
  Logger::log("CONTROL: Received CONFIG_HANDSHAKE.");

  Config receivedConfig =
      Config::fromJson(ControlProtocol::parseJsonPayload(configMessage.payload));
  receivedConfig.setMode(Config::TestMode::SERVER);
  context.config = receivedConfig;
  Logger::log("Info: Received Config.");

  co_await context.control.send(MessageType::CONFIG_ACK);
  Logger::log("CONTROL: Sent CONFIG_ACK.");
}

Task ServerTestSession::runClientToServerPhase() {
  context.transitionTo(TestController::State::RUNNING_TEST);
  context.receiver.resetStats();

  co_await context.control.waitFor(MessageType::TEST_FIN);
  Logger::log("CONTROL: Received TEST_FIN from client for Phase 1.");

  context.transitionTo(TestController::State::FINISHING);
  co_await context.control.send(MessageType::TEST_FIN);
  Logger::log("CONTROL: Sent TEST_FIN for Phase 1.");

  auto statsMessage =
      co_await context.control.waitFor(MessageType::STATS_EXCHANGE);
  Logger::log("CONTROL: Received STATS_EXCHANGE for Phase 1.");
  context.clientStatsPhase1 =
      ControlProtocol::parseStatsPayload(statsMessage.payload);
  context.serverStatsPhase1 = context.receiver.getStats();

  ControlProtocol::logPhaseSummary("Test Phase 1 Summary",
                                   "Client-side (sent)",
                                   context.clientStatsPhase1,
                                   "Server-side (received)",
                                   context.serverStatsPhase1);
  context.notifyPhaseComplete(1);

  context.transitionTo(TestController::State::WAITING_FOR_CLIENT_READY);
  co_await context.control.send(
      MessageType::STATS_ACK,
      ControlProtocol::statsToPayload(context.serverStatsPhase1));
  Logger::log("CONTROL: Sent STATS_ACK for Phase 1.");
}

Task ServerTestSession::runServerToClientPhase() {
  co_await context.control.waitFor(MessageType::CLIENT_READY);
  Logger::log("CONTROL: Received CLIENT_READY. Starting Phase 2.");

  context.transitionTo(TestController::State::RUNNING_SERVER_TEST);
  context.generator.resetStats();

  co_await context.generator.sendPackets(context.config);
  Logger::log("Info: Server generator finished.");

  context.transitionTo(TestController::State::SERVER_TEST_FINISHING);
  co_await context.control.send(MessageType::TEST_FIN);
  Logger::log("CONTROL: Sent TEST_FIN for Phase 2.");

  auto statsMessage =
      co_await context.control.waitFor(MessageType::STATS_EXCHANGE);
  Logger::log("CONTROL: Received STATS_EXCHANGE for Phase 2.");
  context.clientStatsPhase2 =
      ControlProtocol::parseStatsPayload(statsMessage.payload);
  context.serverStatsPhase2 = context.generator.getStats();

  ControlProtocol::logPhaseSummary("Test Phase 2 Summary",
                                   "Server-side (sent)",
                                   context.serverStatsPhase2,
                                   "Client-side (received)",
                                   context.clientStatsPhase2);
  context.notifyPhaseComplete(2);

  context.transitionTo(TestController::State::WAITING_FOR_SHUTDOWN_ACK);
  co_await context.control.send(
      MessageType::STATS_ACK,
      ControlProtocol::statsToPayload(context.serverStatsPhase2));
  Logger::log("CONTROL: Sent STATS_ACK for Phase 2.");

  co_await context.control.waitFor(MessageType::SHUTDOWN_ACK);
  Logger::log("CONTROL: Received SHUTDOWN_ACK.");
}

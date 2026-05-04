#pragma once

#include "ControlChannel.h"
#include "PacketGenerator.h"
#include "PacketReceiver.h"
#include "myiperf/Config.h"
#include "myiperf/NetworkInterface.h"
#include "myiperf/TestController.h"

#include <functional>

struct TestSessionContext {
  Config& config;
  NetworkInterface& network;
  PacketGenerator& generator;
  PacketReceiver& receiver;
  ControlChannel& control;
  TestStats& clientStatsPhase1;
  TestStats& serverStatsPhase1;
  TestStats& clientStatsPhase2;
  TestStats& serverStatsPhase2;
  std::function<void(TestController::State)> transitionTo;
};

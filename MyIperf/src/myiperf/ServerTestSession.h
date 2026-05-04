#pragma once

#include "TestSessionContext.h"

#include <string>

class ServerTestSession {
public:
  explicit ServerTestSession(TestSessionContext& context);

  Task run();

private:
  TestSessionContext& context;

  void startReceiver();
  [[noreturn]] void fail(const std::string& message);

  Task acceptAndReceiveConfig();
  Task runClientToServerPhase();
  Task runServerToClientPhase();
};

#pragma once

#include "TestSessionContext.h"

#include <string>

class ClientTestSession {
public:
  explicit ClientTestSession(TestSessionContext& context);

  Task run();

private:
  TestSessionContext& context;

  void startReceiver();
  [[noreturn]] void fail(const std::string& message);

  Task connectAndHandshake();
  Task runClientToServerPhase();
  Task runServerToClientPhase();
};

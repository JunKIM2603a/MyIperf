#pragma once

#include "ControlMessageBus.h"
#include "PacketReceiver.h"
#include "myiperf/CoroutineSupport.h"
#include "myiperf/NetworkInterface.h"
#include "myiperf/Protocol.h"

#include <vector>

class ControlChannel {
public:
  ControlChannel(NetworkInterface& network, ControlMessageBus& messages);

  void attachReceiver(PacketReceiver& receiver);

  ControlMessageBus::Awaiter waitFor(MessageType type, int timeoutMs = 5000);
  Task send(MessageType type, std::vector<char> payload = {});

  void clear();
  void cancelAll();

private:
  NetworkInterface& network;
  ControlMessageBus& messages;
};

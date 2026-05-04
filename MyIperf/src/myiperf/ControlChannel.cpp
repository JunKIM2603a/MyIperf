#include "ControlChannel.h"

#include "ControlProtocol.h"

ControlChannel::ControlChannel(NetworkInterface& network,
                               ControlMessageBus& messages)
    : network(network), messages(messages) {}

void ControlChannel::attachReceiver(PacketReceiver& receiver) {
  receiver.start(messages);
}

ControlMessageBus::Awaiter ControlChannel::waitFor(MessageType type,
                                                   int timeoutMs) {
  return messages.waitFor(type, timeoutMs);
}

Task ControlChannel::send(MessageType type, std::vector<char> payload) {
  co_await ControlProtocol::sendControlPacket(network, type, std::move(payload));
}

void ControlChannel::clear() {
  messages.clear();
}

void ControlChannel::cancelAll() {
  messages.cancelAll();
}

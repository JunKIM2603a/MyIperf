#pragma once

#include "PacketReceiveStats.h"

#include <vector>

class ControlMessageBus;

class PacketDispatcher {
public:
    PacketDispatcher(ControlMessageBus& messages, PacketReceiveStats& stats);

    void dispatch(const std::vector<ParsedPacket>& packets);

private:
    ControlMessageBus& messages;
    PacketReceiveStats& stats;
};

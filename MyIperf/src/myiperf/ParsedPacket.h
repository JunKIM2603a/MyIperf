#pragma once

#include "myiperf/Protocol.h"

#include <cstddef>
#include <vector>

struct ParsedPacket {
    PacketHeader header{};
    std::vector<char> payload;
    size_t totalPacketSize = 0;
};

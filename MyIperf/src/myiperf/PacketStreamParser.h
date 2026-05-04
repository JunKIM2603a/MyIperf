#pragma once

#include "ParsedPacket.h"

#include <cstddef>
#include <vector>

struct PacketParseResult {
    std::vector<ParsedPacket> packets;
    size_t checksumFailures = 0;
};

class PacketStreamParser {
public:
    explicit PacketStreamParser(size_t maxPayloadSize);

    void reset();
    void append(const std::vector<char>& data, size_t bytesReceived);
    PacketParseResult drainPackets();

private:
    size_t maxPayloadSize;
    std::vector<char> buffer;
    int consecutiveFailures = 0;
};

#pragma once

#include "ParsedPacket.h"

#include <chrono>
#include <mutex>

class PacketReceiveStats {
public:
    PacketReceiveStats();

    void reset();
    void onDataPacket(const ParsedPacket& packet);
    void onChecksumFailure();
    TestStats snapshot() const;

private:
    mutable std::mutex mutex;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    long long totalBytesReceived = 0;
    long long totalPacketsReceived = 0;
    long long failedChecksumCount = 0;
    long long sequenceErrorCount = 0;
    long long contentMismatchCount = 0;
    uint32_t expectedPacketCounter = 0;
};

#include "PacketReceiveStats.h"

#include "myiperf/Logger.h"

#include <algorithm>

PacketReceiveStats::PacketReceiveStats() {
    reset();
}

void PacketReceiveStats::reset() {
    std::lock_guard<std::mutex> lock(mutex);
    startTime = std::chrono::steady_clock::now();
    endTime = startTime;
    totalBytesReceived = 0;
    totalPacketsReceived = 0;
    failedChecksumCount = 0;
    sequenceErrorCount = 0;
    contentMismatchCount = 0;
    expectedPacketCounter = 0;
}

void PacketReceiveStats::onDataPacket(const ParsedPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex);

    const std::string expected =
        buildExpectedPayload(packet.header.packetCounter, packet.header.payloadSize);
    if (expected.size() == packet.payload.size()
        && !std::equal(packet.payload.begin(), packet.payload.end(), expected.begin())) {
        Logger::log("Warning: Payload content mismatch for packet "
                    + std::to_string(packet.header.packetCounter));
        contentMismatchCount++;
    }

    endTime = std::chrono::steady_clock::now();
    totalBytesReceived += static_cast<long long>(packet.totalPacketSize);
    totalPacketsReceived++;

    if (packet.header.packetCounter != expectedPacketCounter) {
        sequenceErrorCount++;
        expectedPacketCounter = packet.header.packetCounter;
    }
    expectedPacketCounter++;
}

void PacketReceiveStats::onChecksumFailure() {
    std::lock_guard<std::mutex> lock(mutex);
    failedChecksumCount++;
}

TestStats PacketReceiveStats::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    TestStats stats;
    stats.totalPacketsReceived = totalPacketsReceived;
    stats.failedChecksumCount = failedChecksumCount;
    stats.sequenceErrorCount = sequenceErrorCount;
    stats.contentMismatchCount = contentMismatchCount;
    stats.totalBytesReceived = totalBytesReceived;

    if (endTime > startTime) {
        stats.duration = std::chrono::duration<double>(endTime - startTime).count();
        if (stats.duration > 0) {
            stats.throughputMbps =
                (static_cast<double>(stats.totalBytesReceived) * 8.0)
                / stats.duration / 1'000'000.0;
        }
    }

    return stats;
}

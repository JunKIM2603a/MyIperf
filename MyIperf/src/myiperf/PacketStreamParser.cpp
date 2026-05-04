#include "PacketStreamParser.h"

#include "myiperf/Logger.h"

#include <cstring>
#include <thread>

namespace {

constexpr int MAX_CONSECUTIVE_FAILURES = 100;

} // namespace

PacketStreamParser::PacketStreamParser(size_t maxPayloadSize)
    : maxPayloadSize(maxPayloadSize) {}

void PacketStreamParser::reset() {
    buffer.clear();
    consecutiveFailures = 0;
}

void PacketStreamParser::append(const std::vector<char>& data,
                                size_t bytesReceived) {
    buffer.insert(buffer.end(), data.begin(), data.begin() + bytesReceived);
}

PacketParseResult PacketStreamParser::drainPackets() {
    PacketParseResult result;

    while (!buffer.empty()) {
        if (buffer.size() < sizeof(PacketHeader)) {
            break;
        }

        PacketHeader header{};
        std::memcpy(&header, buffer.data(), sizeof(PacketHeader));

        if (header.payloadSize > maxPayloadSize) {
            Logger::log("Error: Invalid payload size in header. Clearing buffer to resynchronize. "
                        + std::to_string(header.payloadSize)
                        + " bytes exceeds maximum allowed "
                        + std::to_string(maxPayloadSize));
            buffer.clear();
            break;
        }

        const size_t totalPacketSize = sizeof(PacketHeader) + header.payloadSize;
        if (buffer.size() < totalPacketSize) {
#ifdef DEBUG_LOG
            Logger::log("Debug: PacketStreamParser incomplete packet, have="
                        + std::to_string(buffer.size()) + ", need="
                        + std::to_string(totalPacketSize));
#endif
            break;
        }

        const char* payload = buffer.data() + sizeof(PacketHeader);
        if (header.startCode != PROTOCOL_START_CODE) {
            Logger::log("Error: Invalid start code detected. Discarding one byte to find the next packet.");
            buffer.erase(buffer.begin());
            consecutiveFailures++;
            if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
                std::this_thread::yield();
                consecutiveFailures = 0;
            }
            continue;
        }

        if (!verifyPacket(header, payload)) {
            Logger::log("Error: Checksum validation failed. Discarding one byte to find the next packet.");
            if (header.messageType != MessageType::DATA_PACKET) {
                Logger::log("Error: Checksum failure for control message type "
                            + std::to_string(static_cast<int>(header.messageType))
                            + " (expected size " + std::to_string(totalPacketSize)
                            + ")");
            }
            buffer.erase(buffer.begin());
            result.checksumFailures++;
            consecutiveFailures++;
            if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
                std::this_thread::yield();
                consecutiveFailures = 0;
            }
            continue;
        }

        consecutiveFailures = 0;
        ParsedPacket packet;
        packet.header = header;
        packet.payload.assign(payload, payload + header.payloadSize);
        packet.totalPacketSize = totalPacketSize;
        result.packets.push_back(std::move(packet));

        buffer.erase(buffer.begin(), buffer.begin() + totalPacketSize);
    }

    return result;
}

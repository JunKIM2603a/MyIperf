#pragma once

#include <cstdint>
#include <string>
#include "nlohmann/json.hpp"

/**
 * @enum MessageType
 * @brief Defines the different types of messages that can be exchanged.
 */
enum class MessageType : uint8_t {
    CONFIG_HANDSHAKE = 0, // Sent by the client to the server with test configuration.
    CONFIG_ACK       = 1, // Sent by the server to the client to acknowledge configuration.
    DATA_PACKET      = 2, // A data packet used for the performance test.
    STATS_EXCHANGE   = 3, // Sent after the test to exchange performance statistics.
    STATS_ACK        = 4  // An acknowledgment of receiving statistics.
};

/**
 * @struct TestStats
 * @brief Holds comprehensive statistics for a test, including sent and received data.
 */
struct TestStats {
    long long totalBytesSent;           // Total bytes sent.
    long long totalPacketsSent;         // Total packets sent.
    long long totalBytesReceived;       // Total bytes received.
    long long totalPacketsReceived;     // Total packets received.
    long long failedChecksumCount;      // Number of packets that failed checksum validation.
    long long sequenceErrorCount;       // Number of packets received out of sequence.
    double duration;                    // Duration of the test in seconds.
    double throughputMbps;              // Calculated throughput in Megabits per second.

    // Default constructor to initialize all stats to zero.
    TestStats() : totalBytesSent(0), totalPacketsSent(0), totalBytesReceived(0), totalPacketsReceived(0),
                  failedChecksumCount(0), sequenceErrorCount(0), duration(0.0), throughputMbps(0.0) {}
};

namespace nlohmann {
    template <>
    struct adl_serializer<TestStats> {
        static void to_json(json& j, const TestStats& s) {
            j = nlohmann::json{{"totalBytesSent", s.totalBytesSent},
                                 {"totalPacketsSent", s.totalPacketsSent},
                                 {"totalBytesReceived", s.totalBytesReceived},
                                 {"totalPacketsReceived", s.totalPacketsReceived},
                                 {"failedChecksumCount", s.failedChecksumCount},
                                 {"sequenceErrorCount", s.sequenceErrorCount},
                                 {"duration", s.duration},
                                 {"throughputMbps", s.throughputMbps}};
        }

        static void from_json(const json& j, TestStats& s) {
            j.at("totalBytesSent").get_to(s.totalBytesSent);
            j.at("totalPacketsSent").get_to(s.totalPacketsSent);
            j.at("totalBytesReceived").get_to(s.totalBytesReceived);
            j.at("totalPacketsReceived").get_to(s.totalPacketsReceived);
            j.at("failedChecksumCount").get_to(s.failedChecksumCount);
            j.at("sequenceErrorCount").get_to(s.sequenceErrorCount);
            j.at("duration").get_to(s.duration);
            j.at("throughputMbps").get_to(s.throughputMbps);
        }
    };
}

// Use #pragma pack(push, 1) to disable struct padding.
// This ensures a precise byte layout for network transmission.
#pragma pack(push, 1)
/**
 * @struct PacketHeader
 * @brief The header structure that precedes every packet.
 */
struct PacketHeader {
    uint16_t startCode;       // A fixed start code (e.g., 0xABCD) to identify the beginning of a packet.
    uint8_t  senderId;        // The ID of the sender (e.g., 0 for server, 1 for client).
    uint8_t  receiverId;      // The ID of the receiver.
    MessageType messageType;  // The type of the message (see MessageType enum).
    uint32_t packetCounter;   // A sequence number for the packet.
    uint32_t payloadSize;     // The size of the data (payload) following the header, in bytes.
    uint32_t checksum;        // A checksum calculated over the payload to verify its integrity.
};
#pragma pack(pop)

// The constant start code used to identify the beginning of a valid packet.
constexpr uint16_t PROTOCOL_START_CODE = 0xABCD;

/**
 * @brief Calculates a simple checksum for a block of data.
 * @param data A pointer to the data.
 * @param size The size of the data in bytes.
 * @return The calculated checksum.
 */
inline uint32_t calculateChecksum(const char* data, size_t size) {
    uint32_t sum = 0;
    for (size_t i = 0; i < size; ++i) {
        sum += static_cast<unsigned char>(data[i]);
    }
    return sum;
}

/**
 * @brief Verifies the integrity and validity of a packet.
 * @param header The packet header.
 * @param payload A pointer to the packet's payload data.
 * @return True if the packet is valid, false otherwise.
 */
inline bool verifyPacket(const PacketHeader& header, const char* payload) {
    if (header.startCode != PROTOCOL_START_CODE) return false;
    return header.checksum == calculateChecksum(payload, header.payloadSize);
}

// Build deterministic payload used by both client and server for verification
inline std::string buildExpectedPayload(uint32_t packetCounter, size_t payloadSize) {
    std::string payload = "Packet " + std::to_string(packetCounter);
    if (payload.size() < payloadSize) payload.resize(payloadSize, '.');
    else payload.resize(payloadSize);
    return payload;
}

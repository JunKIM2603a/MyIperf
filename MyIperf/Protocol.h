#pragma once

#include <cstdint>
#include <string>
#include "nlohmann/json.hpp"

// #define DEBUG_LOG
// #define DEBUG_PIPE

/**
 * @enum MessageType
 * @brief Defines the different types of messages that can be exchanged.
 */
enum class MessageType : uint8_t {
    // Handshake
    CONFIG_HANDSHAKE = 0, // Client -> Server: Initial configuration
    CONFIG_ACK       = 1, // Server -> Client: Acknowledgment of configuration
    FINAL_ACK        = 2, // Client -> Server: Final acknowledgment of handshake

    // Data Transfer
    DATA_PACKET      = 3, // A data packet for the performance test
    CLIENT_READY     = 4, // Client -> Server: Signals readiness for server-to-client test

    // Test Control & Stats
    TEST_FIN         = 5, // Signals completion of a test phase
    STATS_EXCHANGE   = 6, // Exchange performance statistics after the test
    STATS_ACK        = 7, // Acknowledgment of receiving statistics
    SHUTDOWN_ACK     = 8  // Client -> Server: Acknowledges final stats and confirms shutdown
};

/**
 * @struct TestStats
 * @brief Holds comprehensive statistics for a test, including sent and received data.
 */
struct TestStats {
    /** @brief Total bytes sent, including packet headers. */
    long long totalBytesSent;
    /** @brief Total number of packets sent. */
    long long totalPacketsSent;
    /** @brief Total bytes received, including packet headers. */
    long long totalBytesReceived;
    /** @brief Total number of valid packets received. */
    long long totalPacketsReceived;
    /** @brief Count of received packets that failed the checksum validation, indicating data corruption. */
    long long failedChecksumCount;
    /** @brief Count of received data packets that arrived out of order, indicating packet loss or reordering. */
    long long sequenceErrorCount;
    /** @brief Count of received data packets whose payload was corrupted in a way that the checksum did not detect. */
    long long contentMismatchCount;
    /** @brief The duration of the test phase in seconds, measured from the start of data transfer to the end. */
    double duration;
    /** 
     * @brief Calculated throughput in Megabits per second (Mbps).
     * Formula: (totalBytesReceived * 8) / duration / 1,000,000
     */
    double throughputMbps;

    /**
     * @brief Default constructor to initialize all stats to zero.
     */
    TestStats() : totalBytesSent(0), totalPacketsSent(0), totalBytesReceived(0), totalPacketsReceived(0),
                  failedChecksumCount(0), sequenceErrorCount(0), contentMismatchCount(0), duration(0.0), throughputMbps(0.0) {}
};

namespace nlohmann {
    /**
     * @brief Specialization of adl_serializer for the TestStats struct.
     *
     * This allows for automatic serialization and deserialization of TestStats
     * objects to and from JSON format using the nlohmann::json library.
     */
    template <>
    struct adl_serializer<TestStats> {
        /**
         * @brief Serializes a TestStats object to a JSON object.
         * @param j The JSON object to serialize to.
         * @param s The TestStats object to serialize.
         */
        static void to_json(json& j, const TestStats& s) {
            j = nlohmann::json{{"totalBytesSent", s.totalBytesSent},
                                 {"totalPacketsSent", s.totalPacketsSent},
                                 {"totalBytesReceived", s.totalBytesReceived},
                                 {"totalPacketsReceived", s.totalPacketsReceived},
                                 {"failedChecksumCount", s.failedChecksumCount},
                                 {"sequenceErrorCount", s.sequenceErrorCount},
                                 {"contentMismatchCount", s.contentMismatchCount},
                                 {"duration", s.duration},
                                 {"throughputMbps", s.throughputMbps}};
        }

        /**
         * @brief Deserializes a TestStats object from a JSON object.
         * @param j The JSON object to deserialize from.
         * @param s The TestStats object to deserialize into.
         */
        static void from_json(const json& j, TestStats& s) {
            j.at("totalBytesSent").get_to(s.totalBytesSent);
            j.at("totalPacketsSent").get_to(s.totalPacketsSent);
            j.at("totalBytesReceived").get_to(s.totalBytesReceived);
            j.at("totalPacketsReceived").get_to(s.totalPacketsReceived);
            j.at("failedChecksumCount").get_to(s.failedChecksumCount);
            j.at("sequenceErrorCount").get_to(s.sequenceErrorCount);
            j.at("contentMismatchCount").get_to(s.contentMismatchCount);
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
    /** @brief A fixed start code (e.g., 0xABCD) to identify the beginning of a packet. */
    uint16_t startCode;
    /** @brief The ID of the sender (e.g., 0 for server, 1 for client). */
    uint8_t  senderId;
    /** @brief The ID of the receiver. */
    uint8_t  receiverId;
    /** @brief The type of the message (see MessageType enum). */
    MessageType messageType;
    /** @brief A sequence number for the packet. */
    uint32_t packetCounter;
    /** @brief The size of the data (payload) following the header, in bytes. */
    uint32_t payloadSize;
    /** @brief A checksum calculated over the payload to verify its integrity. */
    uint32_t checksum;
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

/**
 * @brief Builds a deterministic payload for a given packet counter and size.
 *
 * This function generates a predictable payload string that can be created by both the
 * client and server for verification purposes. The payload starts with "Packet X"
 * where X is the packet counter, and is then padded with '.' characters to reach
 * the desired payload size.
 *
 * @param packetCounter The sequence number of the packet.
 * @param payloadSize The desired size of the payload in bytes.
 * @return The generated payload string.
 */
inline std::string buildExpectedPayload(uint32_t packetCounter, size_t payloadSize) {
    std::string payload = "Packet " + std::to_string(packetCounter);
    if (payload.size() < payloadSize) payload.resize(payloadSize, '.');
    else payload.resize(payloadSize);
    return payload;
}
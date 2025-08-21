#pragma once

#include "NetworkInterface.h"
#include "Protocol.h"
#include "config.h"
#include <chrono>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>

/**
 * @class PacketGenerator
 * @brief Generates and sends network packets based on a given configuration.
 *
 * This class is responsible for creating packets with the specified size and content,
 * sending them over the network interface, and continuing to do so for a configured
 * duration. It operates asynchronously to avoid blocking.
 */
class PacketGenerator {
public:
    // Defines a callback function type that is invoked when the generation process is complete.
    using CompletionCallback = std::function<void()>;

    /**
     * @brief Constructs a PacketGenerator.
     * @param netInterface A pointer to the NetworkInterface to use for sending data.
     */
    PacketGenerator(NetworkInterface* netInterface);

    /**
     * @brief Starts the packet generation and sending process.
     * @param config The configuration settings for the test.
     * @param onComplete The callback function to be called when the test duration is over.
     */
    void start(const Config& config, CompletionCallback onComplete);

    /**
     * @brief Stops the packet generation process.
     */
    void stop();

    // --- Public Getters for Statistics ---

    /**
     * @brief Retrieves the current generator statistics.
     * @return A TestStats struct containing the latest statistics. This method is thread-safe.
     */
    TestStats getStats() const;

private:
    // Private methods for internal operation

    /**
     * @brief Creates and sends the next packet in the sequence.
     */
    void sendNextPacket();

    /**
     * @brief The callback handler for when a send operation completes.
     * @param bytesSent The number of bytes that were sent.
     */
    void onPacketSent(size_t bytesSent);

    bool shouldContinueSending() const;

    // Member variables

    NetworkInterface* networkInterface;     // The network interface for sending data.
    std::atomic<bool> running;              // Flag to control the generator's running state.
    std::atomic<long long> totalBytesSent;  // Atomically updated count of total bytes sent.
    std::atomic<long long> totalPacketsSent{0}; // Successfully sent packets count.

    Config config;                          // The configuration for the current test.
    uint32_t packetCounter;                 // Counter for numbering packets.
    CompletionCallback completionCallback;  // Callback to notify completion.

    std::chrono::steady_clock::time_point m_startTime;
    std::chrono::steady_clock::time_point m_endTime;
};
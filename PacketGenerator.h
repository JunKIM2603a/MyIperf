#pragma once

#include "NetworkInterface.h"
#include "Protocol.h"
#include "Config.h"
#include <chrono>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>

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
    /** @brief Defines a callback function type that is invoked when the generation process is complete. */
    using CompletionCallback = std::function<void()>;

    /**
     * @brief Constructs a PacketGenerator.
     * @param netInterface A pointer to the NetworkInterface to use for sending data.
     */
    PacketGenerator(NetworkInterface* netInterface);

    /**
     * @brief Destroys the PacketGenerator object.
     */
    ~PacketGenerator();

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
     * @brief The main loop for the generator thread, responsible for timing the packet sends.
     */
    void generatorThreadLoop();

    /**
     * @brief The callback handler for when a send operation completes.
     * @param bytesSent The number of bytes that were sent.
     */
    void onPacketSent(size_t bytesSent);

    /**
     * @brief Determines whether the generator should continue sending packets.
     * @return True if the test duration has not been reached, false otherwise.
     */
    bool shouldContinueSending() const;

    /**
     * @brief Prepares a template for the packets to be sent.
     * This improves efficiency by avoiding repeated construction of the same packet structure.
     */
    void preparePacketTemplate();

    // Member variables

    /**< The network interface for sending data. */
    NetworkInterface* networkInterface;
    /**< Flag to control the generator's running state. */
    std::atomic<bool> running;
    /**< Atomically updated count of total bytes sent. */
    std::atomic<long long> totalBytesSent;
    /**< Successfully sent packets count. */
    std::atomic<long long> totalPacketsSent{0};

    /**< The configuration for the current test. */
    Config config;
    /**< Counter for numbering packets. */
    uint32_t packetCounter;
    /**< Callback to notify completion. */
    CompletionCallback completionCallback;
    /**< Pre-built packet template for efficiency. */
    std::vector<char> m_packetTemplate;

    /**< Thread to manage the sending loop. */
    std::thread m_generatorThread;
    /**< Mutex for thread synchronization. */
    std::mutex m_mutex;
    /**< Condition variable for thread synchronization. */
    std::condition_variable m_cv;

    /**< Timestamp for when the generator was started. */
    std::chrono::steady_clock::time_point m_startTime;
    /**< Timestamp for when the generator should stop. */
    std::chrono::steady_clock::time_point m_endTime;
};
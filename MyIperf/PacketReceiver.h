#pragma once

#include "NetworkInterface.h"
#include "Protocol.h"
#include "Config.h"
#include <chrono>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

/**
 * @brief Defines a callback function type that is invoked when a complete and valid packet is received.
 * @param header The header of the received packet.
 * @param payload The payload data of the received packet.
 */
using PacketCallback = std::function<void(const PacketHeader&, const std::vector<char>&)>;
/**
 * @brief Defines a callback function type that is invoked when the receiver completes its operation.
 */
using ReceiverCompletionCallback = std::function<void()>;

/**
 * @class PacketReceiver
 * @brief Manages receiving data from a network interface, parsing it into packets, and dispatching them.
 *
 * This class continuously receives data from the underlying network, buffers it,
 * and processes the buffer to extract complete packets based on the defined protocol.
 * It handles packet validation (start code, checksum) and dispatches valid packets
 * via a callback.
 */
class PacketReceiver {
public:
    /**
     * @brief Constructs a PacketReceiver.
     * @param netInterface A pointer to the NetworkInterface to use for receiving data.
     */
    PacketReceiver(NetworkInterface* netInterface);

    /**
     * @brief Destroys the PacketReceiver object.
     */
    ~PacketReceiver() {};

    /**
     * @brief Starts the packet receiving process.
     * @param onPacket The callback function to be called for each valid packet received.
     */
    void start(PacketCallback onPacket);

    /**
     * @brief Starts the packet receiving process with a completion callback.
     * @param onPacket The callback function to be called for each valid packet received.
     * @param onComplete The callback function to be called when the receiver stops (e.g., due to a disconnect).
     */
    void start(PacketCallback onPacket, ReceiverCompletionCallback onComplete);

    /**
     * @brief Stops the packet receiving process.
     */
    void stop();

    /**
     * @brief Retrieves the current receiver statistics.
     * @return A TestStats struct containing the latest statistics. This method is thread-safe.
     */
    TestStats getStats() const;

    /**
     * @brief Resets all statistical counters to zero.
     * This is useful for clearing stats from a previous run without re-creating the object.
     */
    void resetStats();

private:
    // Private methods for internal operation

    /**
     * @brief Initiates an asynchronous receive operation on the network interface.
     */
    void receiveNextPacket();

    /**
     * @brief The callback handler for when data is received from the network interface.
     * @param data The raw data received.
     * @param bytesReceived The number of bytes received.
     */
    void onPacketReceived(const std::vector<char>& data, size_t bytesReceived);

    /**
     * @brief Processes the internal buffer to find and validate complete packets.
     */
    void processBuffer();

    // Member variables

    /**< The network interface for data reception. */
    NetworkInterface* networkInterface;
    /**< Flag to control the receiver's running state. */
    std::atomic<bool> running;
    /**< Timestamp for when the receiver was started. */
    std::chrono::steady_clock::time_point m_startTime;
    /**< Timestamp for when the generator should stop. */
    std::chrono::steady_clock::time_point m_endTime;

    /**< Atomically updated count of total bytes received. */
    std::atomic<long long> currentBytesReceived;
    /**< Mutex to protect access to the statistics. `mutable` allows locking in const methods. */
    mutable std::mutex statsMutex;
    /**< The size of the buffer for each network receive operation. */
    int packetBufferSize;
    /**< Internal buffer to assemble packets from the incoming data stream. */
    std::vector<char> m_receiveBuffer;
    /**< The callback function to invoke for valid packets. */
    PacketCallback onPacketCallback;
    /**< Called when receiver stops due to disconnect. */
    ReceiverCompletionCallback onCompleteCallback;
    /**< Counter to track the sequence of incoming packets. */
    uint32_t expectedPacketCounter;

    // --- Statistics Counters ---
    /**< Total number of valid packets received. */
    std::atomic<long long> m_totalPacketsReceived{0};
    /**< Tracks packets that fail checksum validation, indicating data corruption. */
    std::atomic<long long> m_failedChecksumCount{0};
    /**< Tracks packets that arrive with an unexpected sequence number, indicating packet loss or reordering. */
    std::atomic<long long> m_sequenceErrorCount{0};
    /**< Tracks packets whose payload is altered during transit, indicating a subtle form of data corruption. */
    std::atomic<long long> m_contentMismatchCount{0};
};
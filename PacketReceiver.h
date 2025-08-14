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
 * @struct ReceiverStats
 * @brief Holds statistics related to the packet receiving process.
 */
struct ReceiverStats {
    long long totalBytesReceived;       // Total bytes received from the network.
    std::chrono::duration<double> duration; // Time elapsed since the receiver started.
    double throughputMbps;              // Calculated throughput in Megabits per second.
    long long totalPacketsReceived;     // Total number of valid packets processed.
    long long failedChecksumCount;      // Number of packets that failed checksum validation.
    long long sequenceErrorCount;       // Number of packets received out of sequence.

    // Default constructor to initialize all stats to zero.
    ReceiverStats() : totalBytesReceived(0), duration(0.0), throughputMbps(0.0), totalPacketsReceived(0), failedChecksumCount(0), sequenceErrorCount(0) {}
};

/**
 * @brief Defines a callback function type that is invoked when a complete and valid packet is received.
 * @param header The header of the received packet.
 * @param payload The payload data of the received packet.
 */
using PacketCallback = std::function<void(const PacketHeader&, const std::vector<char>&)>;
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
     * @brief Starts the packet receiving process.
     * @param onPacket The callback function to be called for each valid packet received.
     */
    void start(PacketCallback onPacket);
    void start(PacketCallback onPacket, ReceiverCompletionCallback onComplete);

    /**
     * @brief Stops the packet receiving process.
     */
    void stop();

    /**
     * @brief Retrieves the current receiver statistics.
     * @return A ReceiverStats struct containing the latest statistics. This method is thread-safe.
     */
    ReceiverStats getStats() const;

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

    NetworkInterface* networkInterface;     // The network interface for data reception.
    std::atomic<bool> running;              // Flag to control the receiver's running state.
    std::chrono::high_resolution_clock::time_point startTime; // Timestamp for when the receiver was started.

    std::atomic<long long> currentBytesReceived; // Atomically updated count of total bytes received.
    mutable std::mutex statsMutex;          // Mutex to protect access to the statistics. `mutable` allows locking in const methods.
    int packetBufferSize;                   // The size of the buffer for each network receive operation.
    std::vector<char> m_receiveBuffer;      // Internal buffer to assemble packets from the incoming data stream.
    PacketCallback onPacketCallback;        // The callback function to invoke for valid packets.
    ReceiverCompletionCallback onCompleteCallback; // Called when receiver stops due to disconnect.
    uint32_t expectedPacketCounter;         // Counter to track the sequence of incoming packets.

    // Stats counters
    std::atomic<long long> m_totalPacketsReceived{0};
    std::atomic<long long> m_failedChecksumCount{0};
    std::atomic<long long> m_sequenceErrorCount{0};
};
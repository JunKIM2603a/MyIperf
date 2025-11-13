#include "PacketReceiver.h"
#include "Logger.h"
#include "Protocol.h"
#include <iostream>

/**
 * @brief Constructs the PacketReceiver.
 * @param netInterface Pointer to the network interface to be used for receiving data.
 */
PacketReceiver::PacketReceiver(NetworkInterface* netInterface)
    : networkInterface(netInterface), running(false), currentBytesReceived(0), expectedPacketCounter(0) {}

/**
 * @brief Starts the packet receiving process.
 * This method initializes the receiver's state and begins listening for packets.
 * @param onPacket The callback function to execute when a valid packet is received.
 */
void PacketReceiver::start(PacketCallback onPacket) {
    start(onPacket, nullptr);
}

/**
 * @brief Starts the packet receiving process with a completion callback.
 * @param onPacket The callback function to be called for each valid packet received.
 * @param onComplete The callback function to be called when the receiver stops (e.g., due to a disconnect).
 */
void PacketReceiver::start(PacketCallback onPacket, ReceiverCompletionCallback onComplete) {
    if (running) {
        Logger::log("Info: PacketReceiver is already running.");
        return;
    }

    this->onPacketCallback = onPacket;
    this->onCompleteCallback = onComplete;
    running = true;
    currentBytesReceived = 0;
    m_receiveBuffer.clear();
    expectedPacketCounter = 0;
    m_startTime = std::chrono::steady_clock::now();
    // packetBufferSize = 8192; // A reasonable default buffer size, e.g., 8KB.
    packetBufferSize = 13000000;

    Logger::log("Info: PacketReceiver started.");
    // Start the first asynchronous receive operation.
    receiveNextPacket();
}

/**
 * @brief Stops the packet receiving process.
 * This method sets the running flag to false, which will halt the receive loop.
 */
void PacketReceiver::stop() {
    if (!running.exchange(false)) { // Atomically set running to false and get the old value.
        return; // Already stopped.
    }
    Logger::log("Info: PacketReceiver stopped.");
}

/**
 * @brief Retrieves the current receiver statistics.
 * @return A TestStats struct containing the latest statistics. This method is thread-safe.
 */
TestStats PacketReceiver::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex); // Lock to ensure thread-safe access to stats.
    TestStats stats;
    stats.totalPacketsReceived = m_totalPacketsReceived.load();
    stats.failedChecksumCount = m_failedChecksumCount.load();
    stats.sequenceErrorCount = m_sequenceErrorCount.load();
    stats.contentMismatchCount = m_contentMismatchCount.load();

    stats.totalBytesReceived = currentBytesReceived;
    if (m_endTime > m_startTime) {
        stats.duration = std::chrono::duration<double>(m_endTime - m_startTime).count();
        if (stats.duration > 0) {
            // Throughput is calculated as the total bytes received (converted to bits) divided by the
            // test duration in seconds. The result is then divided by 1,000,000 to convert from bits
            // per second to Megabits per second (Mbps).
            stats.throughputMbps = (static_cast<double>(stats.totalBytesReceived) * 8.0) / stats.duration / 1'000'000.0;
        }
    }

    // Sent stats are not applicable for receiver, so they remain 0 (default initialized)
    return stats;
}

/**
 * @brief Resets all statistical counters to zero.
 * This is useful for clearing stats from a previous run without re-creating the object.
 */
void PacketReceiver::resetStats() {
    std::lock_guard<std::mutex> lock(statsMutex);
    currentBytesReceived = 0;
    m_totalPacketsReceived = 0;
    m_failedChecksumCount = 0;
    m_sequenceErrorCount = 0;
    m_contentMismatchCount = 0;
    expectedPacketCounter = 0;
    m_startTime = std::chrono::steady_clock::now();
    m_endTime = std::chrono::steady_clock::now();
    Logger::log("Info: PacketReceiver statistics have been reset.");
}

/**
 * @brief Initiates an asynchronous receive operation.
 * If the receiver is running, it requests the network interface to receive data.
 */
void PacketReceiver::receiveNextPacket() {
#ifdef DEBUG_LOG
    Logger::log("Debug: PacketReceiver::receiveNextPacket called.");
#endif
    if (!running) {
        return;
    }
    // Asynchronously receive data. The callback `onPacketReceived` will be invoked upon completion.
    networkInterface->asyncReceive(packetBufferSize, [this](const std::vector<char>& data, size_t bytesReceived) {
        onPacketReceived(data, bytesReceived);
    });
}

/**
 * @brief Callback function that is executed when data is received from the network.
 * @param data The buffer containing the received data.
 * @param bytesReceived The number of bytes received.
 */
void PacketReceiver::onPacketReceived(const std::vector<char>& data, size_t bytesReceived) {
    if (!running) return;

    if (bytesReceived > 0) {
#ifdef DEBUG_LOG
        Logger::log("Debug: onPacketReceived - bytesReceived=" + std::to_string(bytesReceived));
#endif
        // Append the newly received data to our internal buffer.
        m_receiveBuffer.insert(m_receiveBuffer.end(), data.begin(), data.begin() + bytesReceived);
        processBuffer();
        // Immediately post the next receive request.
        receiveNextPacket();
    } else {
        // A zero-byte receive is a special condition that typically indicates the peer has gracefully
        // closed the connection from their side.
        Logger::log("Warning: 0 bytes received. The connection may have been closed.");
        // Process any data that might be lingering in the buffer before stopping.
        processBuffer();
        stop(); 
        if (onCompleteCallback) onCompleteCallback();
    }
}

/**
 * @brief Processes the receive buffer to extract and validate complete packets.
 * This method loops through the buffer, identifies packet boundaries, validates packets,
 * and dispatches them via the callback.
 */
void PacketReceiver::processBuffer() {
    while (running && !m_receiveBuffer.empty()) {
        if (m_receiveBuffer.size() < sizeof(PacketHeader)) {
            break; // Not enough data for a header, wait for more.
        }

        // Interpret the start of the buffer as a packet header.
        const PacketHeader* header = reinterpret_cast<const PacketHeader*>(m_receiveBuffer.data());

        // Sanity check on the payload size to prevent errors from corrupted data.
        if (header->payloadSize > packetBufferSize * 2) { 
            Logger::log("Error: Invalid payload size in header. Clearing buffer to resynchronize." 
                + std::to_string(header->payloadSize) + " bytes exceeds maximum allowed." + std::to_string(packetBufferSize * 2));
            m_receiveBuffer.clear();
            break;
        }

        size_t totalPacketSize = sizeof(PacketHeader) + header->payloadSize;

        // Check if the complete packet has been received.
        if (m_receiveBuffer.size() < totalPacketSize) {
#ifdef DEBUG_LOG
            Logger::log("Debug: processBuffer - incomplete packet, have=" + std::to_string(m_receiveBuffer.size()) + ", need=" + std::to_string(totalPacketSize));
#endif
            break; // Incomplete packet, wait for more data.
        }

        // A complete packet is in the buffer. Now, validate it.
        const char* payload = m_receiveBuffer.data() + sizeof(PacketHeader);
        
        // The start code is a magic number (0xABCD) that marks the beginning of a packet. If this is
        // not found, it means we are out of sync with the data stream, likely due to prior corruption
        // or packet loss. We discard one byte and try to resynchronize at the next byte.
        if (header->startCode != PROTOCOL_START_CODE) {
            Logger::log("Error: Invalid start code detected. Discarding one byte to find the next packet.");
            m_receiveBuffer.erase(m_receiveBuffer.begin());
            continue; // Retry with the rest of the buffer.
        }

        // The checksum is a simple validation to detect if the packet's payload was corrupted during
        // transit. If verifyPacket fails, it means the data has been altered.
        if (verifyPacket(*header, payload)) {
            // Packet is valid. Extract the payload and invoke the callback.
            std::vector<char> payload_vec(payload, payload + header->payloadSize);

            // This is a secondary, more rigorous check for data integrity. It rebuilds the expected
            // payload from scratch and compares it byte-for-byte against the received payload.
            // A mismatch here indicates a subtle data corruption that the simpler checksum failed to catch.
            if (header->messageType == MessageType::DATA_PACKET) {
                const std::string expected = buildExpectedPayload(header->packetCounter, header->payloadSize);
                if (expected.size() == payload_vec.size() && !std::equal(payload_vec.begin(), payload_vec.end(), expected.begin())) {
                    Logger::log("Warning: Payload content mismatch for packet " + std::to_string(header->packetCounter));
                    m_contentMismatchCount++;
                }
            }

            if (onPacketCallback) {
#ifdef DEBUG_LOG
                Logger::log("Debug: PacketReceiver::processBuffer - Dispatching packet. Message Type: " + std::to_string(static_cast<int>(header->messageType)) + ", Packet Counter: " + std::to_string(header->packetCounter)
                + ", payloadSize: " + std::to_string(header->payloadSize));
#endif
                if (header->messageType != MessageType::DATA_PACKET) {
                    Logger::log("\x1b[95mHANDSHAKE: PacketReceiver forwarding message type "
                                + std::to_string(static_cast<int>(header->messageType))
                                + " (#" + std::to_string(header->packetCounter) + ", "
                                + std::to_string(totalPacketSize) + " bytes)\x1b[0m");
                }
                if(header->messageType == MessageType::DATA_PACKET){
                    m_endTime = std::chrono::steady_clock::now(); 
                }
                onPacketCallback(*header, payload_vec);
            }
            if (header->messageType == MessageType::DATA_PACKET) {
                Logger::log("Info: PacketReceiver received DATA_PACKET " + std::to_string(header->packetCounter) + 
                            " (size: " + std::to_string(totalPacketSize) + " bytes)");
            }

            // Safely update stats under a lock.
            std::lock_guard<std::mutex> lock(statsMutex);
            // Only count data packets for total bytes and packets received
            if (header->messageType == MessageType::DATA_PACKET) {
                currentBytesReceived += totalPacketSize;
                m_totalPacketsReceived++;
            }

            // A sequence error occurs if a data packet arrives with a number different from the one
            // we expect. This implies that one or more packets were lost or significantly reordered
            // in transit.
            if (header->messageType == MessageType::DATA_PACKET) {
                if (header->packetCounter != expectedPacketCounter) {
                    m_sequenceErrorCount++;
                    // When a sequence error occurs, we resync to the new counter.
                    expectedPacketCounter = header->packetCounter;
                }
                expectedPacketCounter++;
            }
        } else {
            Logger::log("Error: Checksum validation failed. Discarding one byte to find the next packet.");
            if (header->messageType != MessageType::DATA_PACKET) {
                Logger::log("\x1b[91mHANDSHAKE: Checksum failure for message type "
                            + std::to_string(static_cast<int>(header->messageType))
                            + " (expected size " + std::to_string(totalPacketSize) + ")\x1b[0m");
            }
            m_receiveBuffer.erase(m_receiveBuffer.begin());
            m_failedChecksumCount++;
            continue;
        }

        // Remove the processed packet from the front of the buffer.
        m_receiveBuffer.erase(m_receiveBuffer.begin(), m_receiveBuffer.begin() + totalPacketSize);
    }
}
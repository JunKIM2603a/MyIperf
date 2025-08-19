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
    startTime = std::chrono::high_resolution_clock::now();
    packetBufferSize = 8192; // A reasonable default buffer size, e.g., 8KB.

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
 * @brief Retrieves the current statistics of the receiver.
 * @return A `ReceiverStats` struct containing performance metrics.
 */
ReceiverStats PacketReceiver::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex); // Lock to ensure thread-safe access to stats.
    ReceiverStats stats;
    stats.totalBytesReceived = currentBytesReceived;
    stats.duration = std::chrono::high_resolution_clock::now() - startTime;
    
    // Calculate throughput in Mbps using std::chrono durations safely
    double duration_seconds = std::chrono::duration<double>(stats.duration).count();
    if (duration_seconds > 0) {
        stats.throughputMbps = (static_cast<double>(stats.totalBytesReceived) * 8.0) / duration_seconds / 1'000'000.0;
    }
    stats.totalPacketsReceived = m_totalPacketsReceived.load();
    stats.failedChecksumCount = m_failedChecksumCount.load();
    stats.sequenceErrorCount = m_sequenceErrorCount.load();
    return stats;
}

/**
 * @brief Initiates an asynchronous receive operation.
 * If the receiver is running, it requests the network interface to receive data.
 */
void PacketReceiver::receiveNextPacket() {
    Logger::log("Debug: PacketReceiver::receiveNextPacket called.");
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
        Logger::log("Debug: onPacketReceived - bytesReceived=" + std::to_string(bytesReceived));
        // Append the newly received data to our internal buffer.
        m_receiveBuffer.insert(m_receiveBuffer.end(), data.begin(), data.begin() + bytesReceived);
        processBuffer();
        // Immediately post the next receive request.
        receiveNextPacket();
    } else {
        // A zero-byte receive often indicates that the connection has been closed by the peer.
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
            Logger::log("Error: Invalid payload size in header. Clearing buffer to resynchronize.");
            m_receiveBuffer.clear();
            break;
        }

        size_t totalPacketSize = sizeof(PacketHeader) + header->payloadSize;

        // Check if the complete packet has been received.
        if (m_receiveBuffer.size() < totalPacketSize) {
            Logger::log("Debug: processBuffer - incomplete packet, have=" + std::to_string(m_receiveBuffer.size()) + ", need=" + std::to_string(totalPacketSize));
            break; // Incomplete packet, wait for more data.
        }

        // A complete packet is in the buffer. Now, validate it.
        const char* payload = m_receiveBuffer.data() + sizeof(PacketHeader);
        
        // Verify the packet's start code to ensure it's a valid packet.
        if (header->startCode != PROTOCOL_START_CODE) {
            Logger::log("Error: Invalid start code detected. Discarding one byte to find the next packet.");
            m_receiveBuffer.erase(m_receiveBuffer.begin());
            continue; // Retry with the rest of the buffer.
        }

        // Verify the packet's integrity using a checksum.
        if (verifyPacket(*header, payload)) {
            // Packet is valid. Extract the payload and invoke the callback.
            std::vector<char> payload_vec(payload, payload + header->payloadSize);
            // Optional content verification only for DATA_PACKET
            if (header->messageType == MessageType::DATA_PACKET) {
                const std::string expected = buildExpectedPayload(header->packetCounter, header->payloadSize);
                if (expected.size() == payload_vec.size() && !std::equal(payload_vec.begin(), payload_vec.end(), expected.begin())) {
                    Logger::log("Warning: Payload content mismatch for packet " + std::to_string(header->packetCounter));
                }
            }
            if (onPacketCallback) {
                Logger::log("Debug: PacketReceiver::processBuffer - Dispatching packet. Message Type: " + std::to_string(static_cast<int>(header->messageType)) + ", Packet Counter: " + std::to_string(header->packetCounter)
                + ", payloadSize: " + std::to_string(header->payloadSize));
                onPacketCallback(*header, payload_vec);
            }
            // Safely update stats under a lock.
            std::lock_guard<std::mutex> lock(statsMutex);
            currentBytesReceived += totalPacketSize;
            m_totalPacketsReceived++;
            // Sequence checking should only apply to data packets.
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
            m_receiveBuffer.erase(m_receiveBuffer.begin());
            m_failedChecksumCount++;
            continue;
        }

        // Remove the processed packet from the front of the buffer.
        m_receiveBuffer.erase(m_receiveBuffer.begin(), m_receiveBuffer.begin() + totalPacketSize);
    }
}

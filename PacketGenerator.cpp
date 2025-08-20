#include "PacketGenerator.h"
#include "Logger.h"
#include "Protocol.h"
#include <iostream>
#include <random> // For potential future use with random data generation
#include <cstring>

// #define DEBUG_MINIMAL_PACKET // Removed for granular logging

/**
 * @brief Constructs the PacketGenerator.
 * @param netInterface Pointer to the network interface for sending packets.
 */
PacketGenerator::PacketGenerator(NetworkInterface* netInterface)
    : networkInterface(netInterface), running(false), totalBytesSent(0), packetCounter(0) {}

/**
 * @brief Starts the packet generation process.
 * @param config The test configuration.
 * @param onComplete A callback to invoke when the test duration is complete.
 */
void PacketGenerator::start(const Config& config, CompletionCallback onComplete) {
    Logger::log("Debug: PacketGenerator::start entered.");
    Logger::log("Info: Client test parameters - packetSize=" + std::to_string(config.getPacketSize()) +
               ", numPackets=" + std::to_string(config.getNumPackets()) +
               ", intervalMs=" + std::to_string(config.getSendIntervalMs()));
    if (running) {
        Logger::log("Info: PacketGenerator is already running.");
        return;
    }
    
    this->config = config;
    this->completionCallback = onComplete;

    running = true;
    totalBytesSent = 0;
    totalPacketsSent = 0;
    packetCounter = 0;
    m_startTime = std::chrono::steady_clock::now();
    
    Logger::log("Info: PacketGenerator started.");
    // Initiate the first asynchronous send operation.
    sendNextPacket();
    Logger::log("Debug: PacketGenerator::start exited.");
}

/**
 * @brief Stops the packet generation process.
 */
void PacketGenerator::stop() {
    Logger::log("Debug: PacketGenerator::stop entered.");
    if (!running.exchange(false)) { // Atomically set running to false and get the old value.
        return; // Already stopped.
    }
    m_endTime = std::chrono::steady_clock::now();
    Logger::log("Info: PacketGenerator stopped.");
    Logger::log("Debug: PacketGenerator::stop exited.");
}

/**
 * @brief Creates and sends the next packet.
 * This function is the core of the generation loop.
 */
void PacketGenerator::sendNextPacket() {
    Logger::log("Debug: PacketGenerator::sendNextPacket entered. Packet Counter: " + std::to_string(packetCounter));
    // Stop if the generator is no longer running or the test duration has been reached.
    if (!running) {
        
        Logger::log("Debug: PacketGenerator::sendNextPacket exited (stopping condition met).");
        return;
    }

    Logger::log("Debug: PacketGenerator::sendNextPacket - Preparing payload.");
    // 1. Prepare the packet payload.
    size_t payloadSize = config.getPacketSize() - sizeof(PacketHeader);
    std::vector<char> packet(config.getPacketSize());
    std::string payload_str = buildExpectedPayload(packetCounter, payloadSize);

    Logger::log("Debug: PacketGenerator::sendNextPacket - Constructing header.");
    // 2. Construct the packet header.
    PacketHeader header;
    header.startCode = PROTOCOL_START_CODE;
    header.senderId = (config.getMode() == Config::TestMode::CLIENT) ? to_underlying(Config::TestMode::CLIENT) : to_underlying(Config::TestMode::SERVER);
    header.receiverId = (config.getMode() == Config::TestMode::CLIENT) ? to_underlying(Config::TestMode::SERVER) : to_underlying(Config::TestMode::CLIENT);
    header.messageType = MessageType::DATA_PACKET;
    header.packetCounter = packetCounter++;
    header.payloadSize = static_cast<uint32_t>(payloadSize);
    header.checksum = calculateChecksum(payload_str.data(), payloadSize);

    Logger::log("Debug: PacketGenerator::sendNextPacket - Assembling packet.");
    // 3. Assemble the final packet by combining the header and payload.
    memcpy(packet.data(), &header, sizeof(PacketHeader));
    memcpy(packet.data() + sizeof(PacketHeader), payload_str.data(), payloadSize);

    Logger::log("Debug: PacketGenerator::sendNextPacket - Calling asyncSend. bytes=" + std::to_string(packet.size()));
    // 4. Asynchronously send the packet.
    networkInterface->asyncSend(packet, [this](size_t bytesSent) {
        onPacketSent(bytesSent);
    });
    Logger::log("Debug: PacketGenerator::sendNextPacket exited.");
}

/**
 * @brief Callback executed after a packet has been sent.
 * @param bytesSent The number of bytes successfully sent.
 */
void PacketGenerator::onPacketSent(size_t bytesSent) {
    Logger::log("Debug: PacketGenerator::onPacketSent entered. Bytes sent: " + std::to_string(bytesSent));
    if (bytesSent > 0) {
        totalBytesSent += bytesSent;
        totalPacketsSent++;
    } else {
        Logger::log("Warning: Send operation failed or sent 0 bytes. Stopping generator.");
        stop();
    }

    // If still running and conditions allow, continue the loop by sending the next packet.
    if (running && shouldContinueSending()) {
        if (config.getSendIntervalMs() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config.getSendIntervalMs()));
        }
        sendNextPacket();
    } else if (running && !shouldContinueSending()) {
        running = false;
        m_endTime = std::chrono::steady_clock::now();
        Logger::log("Info: PacketGenerator reached target packet count: " + std::to_string(config.getNumPackets()));
        if (completionCallback) completionCallback();
    }
    Logger::log("Debug: PacketGenerator::onPacketSent exited.");
}

bool PacketGenerator::shouldContinueSending() const {
    const int numPackets = config.getNumPackets();
    if (numPackets == 0) return true; // unlimited
    // packetCounter was incremented at header construction time
    return static_cast<int>(packetCounter) < numPackets;
}

TestStats PacketGenerator::getStats() const {
    TestStats stats;
    stats.totalBytesSent = totalBytesSent.load();
    stats.totalPacketsSent = totalPacketsSent.load();
    if (m_endTime > m_startTime) {
        stats.duration = std::chrono::duration<double>(m_endTime - m_startTime).count();
        if (stats.duration > 0) {
            // Throughput (Mbps) = (Total Bytes * 8 bits/byte) / (Duration in seconds * 1,000,000 bits/megabit)
            stats.throughputMbps = (static_cast<double>(stats.totalBytesSent) * 8.0) / stats.duration / 1'000'000.0;
        }
    }
    // Received stats, checksum errors, sequence errors are not applicable for generator, so they remain 0 (default initialized)
    return stats;
}


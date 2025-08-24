#include "PacketGenerator.h"
#include "Logger.h"
#include "Protocol.h"
#include <iostream>
#include <random> // For potential future use with random data generation
#include <cstring>
#include <type_traits> // For std::underlying_type_t

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
    
    // Prepare the reusable packet template
    preparePacketTemplate();

    Logger::log("Info: PacketGenerator started.");
    // Start the dedicated generator thread
    m_generatorThread = std::thread(&PacketGenerator::generatorThreadLoop, this);
    
}

/**
 * @brief Prepares the packet template to be reused for sending.
 * This improves performance by avoiding repeated allocation and construction.
 */
void PacketGenerator::preparePacketTemplate() {
    
    const size_t packetSize = config.getPacketSize();
    if (packetSize < sizeof(PacketHeader)) {
        Logger::log("Error: Packet size is smaller than header size. Cannot generate packets.");
        m_packetTemplate.clear();
        return;
    }

    m_packetTemplate.resize(packetSize);
    const size_t payloadSize = packetSize - sizeof(PacketHeader);
    std::string payload_str = buildExpectedPayload(0, payloadSize); // Use a dummy packet number for the template

    PacketHeader header;
    header.startCode = PROTOCOL_START_CODE;
    header.senderId = static_cast<std::underlying_type_t<Config::TestMode>>(config.getMode());
    header.receiverId = static_cast<std::underlying_type_t<Config::TestMode>>((config.getMode() == Config::TestMode::CLIENT) ? Config::TestMode::SERVER : Config::TestMode::CLIENT);
    header.messageType = MessageType::DATA_PACKET;
    header.packetCounter = 0; // Will be updated per packet
    header.payloadSize = static_cast<uint32_t>(payloadSize);
    header.checksum = calculateChecksum(payload_str.data(), payloadSize);

    memcpy(m_packetTemplate.data(), &header, sizeof(PacketHeader));
    if (payloadSize > 0) {
        memcpy(m_packetTemplate.data() + sizeof(PacketHeader), payload_str.data(), payloadSize);
    }
    
}

/**
 * @brief Stops the packet generation process.
 */
void PacketGenerator::stop() {
    
    if (running.exchange(false)) {
        m_cv.notify_one(); // Wake up the generator thread if it's sleeping
    }

    // Use the existing mutex to protect the joinable() check and the join() call
    // to prevent race conditions if stop() is called from multiple threads.
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_generatorThread.joinable()) {
        m_generatorThread.join();
    }
    m_endTime = std::chrono::steady_clock::now();
    Logger::log("Info: PacketGenerator stopped.");
    
}

/**
 * @brief Creates and sends the next packet using the pre-built template.
 * This function is the core of the generation loop.
 */
void PacketGenerator::sendNextPacket() {
    if (!running || m_packetTemplate.empty()) {
        return;
    }

    // 1. Create a working copy of the packet from the template.
    std::vector<char> packet = m_packetTemplate;

    // 2. Get a pointer to the header and update the packet counter.
    PacketHeader* header = reinterpret_cast<PacketHeader*>(packet.data());
    header->packetCounter = packetCounter++;

    // Note: The checksum is not recalculated here because it's based on the payload,
    // which doesn't change. If the header were part of the checksum, we would
    // need to update it here.

    
    // 3. Asynchronously send the packet.
    networkInterface->asyncSend(packet, [this](size_t bytesSent) {
        onPacketSent(bytesSent);
    });
}

void PacketGenerator::generatorThreadLoop() {
    
    while (running && shouldContinueSending()) {
        sendNextPacket();

        if (config.getSendIntervalMs() > 0) {
            std::unique_lock<std::mutex> lock(m_mutex);
            // Wait for the specified interval, but allow stop() to wake us up early.
            m_cv.wait_for(lock, std::chrono::milliseconds(config.getSendIntervalMs()));
        }

        // After sleeping (or being woken up), check the running flag again before looping.
        if (!running) break;
    }

    if (running) { // If we exited the loop because we finished, not because we were stopped
        running = false;
        m_endTime = std::chrono::steady_clock::now();
        Logger::log("Info: PacketGenerator reached target packet count: " + std::to_string(config.getNumPackets()));
        if (completionCallback) completionCallback();
    }
    
}

/**
 * @brief Callback executed after a packet has been sent.
 * @param bytesSent The number of bytes successfully sent.
 */
void PacketGenerator::onPacketSent(size_t bytesSent) {
    
    if (bytesSent > 0) {
        totalBytesSent += bytesSent;
        totalPacketsSent++;
    } else {
        Logger::log("Warning: Send operation failed or sent 0 bytes. Stopping generator.");
        // Directly calling stop() from this callback (which runs on a network worker thread)
        // can cause a deadlock, because stop() joins the generator thread, and the main
        // thread might be waiting on the network worker thread.
        // Instead, we just signal the generator thread to stop.
        if (running.exchange(false)) {
            m_cv.notify_one();
        }
        return;
    }
    
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

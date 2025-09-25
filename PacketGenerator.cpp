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
    memset(&m_LastStats, 0, sizeof(TestStats));

    // Prepare the reusable packet template
    preparePacketTemplate();

    Logger::log("Info: PacketGenerator started.");
    // Start the dedicated generator thread
    m_generatorThread = std::thread(&PacketGenerator::generatorThreadLoop, this);
    Logger::log("Debug: PacketGenerator::start exited.");
}

/**
 * @brief Prepares the packet template to be reused for sending.
 * This improves performance by avoiding repeated allocation and construction.
 */
void PacketGenerator::preparePacketTemplate() {
    Logger::log("Debug: PacketGenerator::preparePacketTemplate entered.");
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
    Logger::log("Debug: PacketGenerator::preparePacketTemplate exited.");
}

/**
 * @brief Stops the packet generation process.
 */
void PacketGenerator::stop() {
    Logger::log("Debug: PacketGenerator::stop entered.");
    if (!running.exchange(false)) { // Atomically set running to false and get the old value.
        return; // Already stopped.
    }
    m_cv.notify_one(); // Wake up the generator thread if it's sleeping
    if (m_generatorThread.joinable()) {
        m_generatorThread.join();
    }
    m_endTime = std::chrono::steady_clock::now();
    Logger::log("Info: PacketGenerator stopped.");
    Logger::log("Debug: PacketGenerator::stop exited.");
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

    Logger::log("Debug: PacketGenerator::sendNextPacket - Calling asyncSend. bytes=" + std::to_string(packet.size()));
    // 3. Asynchronously send the packet.
    networkInterface->asyncSend(packet, [this](size_t bytesSent) {
        onPacketSent(bytesSent);
    });
}

/**
 * @brief The main loop for the generator thread.
 *
 * This function runs on a dedicated thread and is responsible for sending packets
 * at the specified interval. It continues to run until the test is stopped or
 * the configured number of packets has been sent.
 */
void PacketGenerator::generatorThreadLoop() {
    Logger::log("Debug: PacketGenerator::generatorThreadLoop started.");
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
    Logger::log("Debug: PacketGenerator::generatorThreadLoop finished.");
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
        return;
    }
    Logger::log("Debug: PacketGenerator::onPacketSent exited.");
}

/**
 * @brief Determines whether the generator should continue sending packets.
 * @return True if the test duration has not been reached or if the number of packets to send is unlimited, false otherwise.
 */
bool PacketGenerator::shouldContinueSending() const {
    const int numPackets = config.getNumPackets();
    if (numPackets == 0) return true; // unlimited
    // packetCounter was incremented at header construction time
    return static_cast<int>(packetCounter) < numPackets;
}

/**
 * @brief Retrieves the current generator statistics.
 * @return A TestStats struct containing the latest statistics. This method is thread-safe.
 */
TestStats PacketGenerator::getStats() {
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

void PacketGenerator::saveLastStats(const TestStats& Stats) {
    memcpy_s(&m_LastStats, sizeof(TestStats), &Stats, sizeof(TestStats));
}

TestStats PacketGenerator::lastStats() const{
    return m_LastStats;
}
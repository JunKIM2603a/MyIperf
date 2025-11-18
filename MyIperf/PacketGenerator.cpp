#include "PacketGenerator.h"
#include "Logger.h"
#include "Protocol.h"
#include <iostream>
#include <random> // For potential future use with random data generation
#include <cstring>
#include <type_traits> // For std::underlying_type_t
#include <thread> // For std::this_thread::yield()

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
    
    // Core modification: Ensure previous thread is fully cleaned up before starting a new one
    if (m_generatorThread.joinable()) {
        Logger::log("Warning: Previous generator thread still joinable. Joining before restart.");
        running = false;
        m_cv.notify_one();
        m_generatorThread.join();
    }
    
    // Check running flag state and force reset if necessary
    if (running) {
        Logger::log("Warning: Generator was already running. Force resetting.");
        running = false;
    }
    
    this->config = config;
    this->completionCallback = onComplete;

    running = true;
    totalBytesSent = 0;
    totalPacketsSent = 0;
    packetCounter = 0;
    m_startTime = std::chrono::steady_clock::now();
    memset(&m_LastStats, 0, sizeof(TestStats));

    Logger::log("Info: PacketGenerator started.");
    
    // Launch the generator thread
    m_generatorThread = std::thread(&PacketGenerator::generatorThreadLoop, this);
    Logger::log("Debug: PacketGenerator::start exited. Thread created successfully.");
}

/**
 * @brief Stops the packet generation process.
 */
void PacketGenerator::stop() {
    Logger::log("Debug: PacketGenerator::stop entered.");
    
    bool wasRunning = running.exchange(false);
    if (!wasRunning) {
        Logger::log("Debug: PacketGenerator was already stopped.");
    }

    m_cv.notify_one(); // Wake up the generator thread if it's sleeping
    
    if (m_generatorThread.joinable()) {
        Logger::log("Debug: Joining generator thread...");
        m_generatorThread.join();
        Logger::log("Debug: Generator thread joined successfully.");
    } else {
        Logger::log("Debug: Generator thread was not joinable (already joined or never started).");
    }
    
    m_endTime = std::chrono::steady_clock::now();
    Logger::log("Info: PacketGenerator stopped.");
    Logger::log("Debug: PacketGenerator::stop exited.");
}

/**
 * @brief Creates and sends the next packet, building it from scratch to ensure correctness.
 * This function is the core of the generation loop.
 */
void PacketGenerator::sendNextPacket() {
    if (!running) {
        return;
    }

    const size_t packetSize = config.getPacketSize();
    if (packetSize < sizeof(PacketHeader)) {
        // This check is important to prevent buffer overflows if config is invalid.
        return;
    }

    // 1. Build the payload and calculate the checksum for the CURRENT packet.
    const size_t payloadSize = packetSize - sizeof(PacketHeader);
    std::string payload_str = buildExpectedPayload(packetCounter, payloadSize);
    uint32_t checksum = calculateChecksum(payload_str.data(), payloadSize);

    // 2. Construct the header with the correct, unique information.
    PacketHeader header;
    header.startCode = PROTOCOL_START_CODE;
    header.senderId = static_cast<std::underlying_type_t<Config::TestMode>>(config.getMode());
    header.receiverId = static_cast<std::underlying_type_t<Config::TestMode>>((config.getMode() == Config::TestMode::CLIENT) ? Config::TestMode::SERVER : Config::TestMode::CLIENT);
    header.messageType = MessageType::DATA_PACKET;
    header.packetCounter = packetCounter; // Set the correct sequence number.
    header.payloadSize = static_cast<uint32_t>(payloadSize);
    header.checksum = checksum; // Set the correct checksum.

    // 3. Assemble the final packet for sending.
    std::vector<char> packet(packetSize);
    memcpy(packet.data(), &header, sizeof(PacketHeader));
    if (payloadSize > 0) {
        memcpy(packet.data() + sizeof(PacketHeader), payload_str.data(), payloadSize);
    }

    // 4. Asynchronously send the packet.
    networkInterface->asyncSend(packet, [this](size_t bytesSent) {
        onPacketSent(bytesSent);
    });

    // 5. Increment the counter for the next packet.
    packetCounter++;
    Logger::log("Info: PacketGenerator sent packet " + std::to_string(packetCounter -1) + " (size: " + std::to_string(packetSize) + " bytes)");
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
        } else {
            // Yield to prevent 100% CPU usage while maintaining maximum throughput
            // This allows the OS to schedule other threads (e.g., network I/O completion)
            std::this_thread::yield();
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
#ifdef DEBUG_LOG
    Logger::log("Debug: PacketGenerator::onPacketSent entered. Bytes sent: " + std::to_string(bytesSent));
#endif
    if (bytesSent > 0) {
        totalBytesSent += bytesSent;
        totalPacketsSent++;
    } else {
        Logger::log("Warning: Send operation failed or sent 0 bytes. Stopping generator.");
        stop();
        return;
    }
#ifdef DEBUG_LOG
    Logger::log("Debug: PacketGenerator::onPacketSent exited.");
#endif
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
            // Throughput is calculated as the total bytes sent (converted to bits) divided by the
            // test duration in seconds. The result is then divided by 1,000,000 to convert from bits
            // per second to Megabits per second (Mbps).
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

/**
 * @brief Resets the generator's statistics for a new test phase.
 */
void PacketGenerator::resetStats() {
    Logger::log("Debug: PacketGenerator::resetStats entered.");
    totalBytesSent = 0;
    totalPacketsSent = 0;
    packetCounter = 0;
    m_startTime = std::chrono::steady_clock::now();
    m_endTime = std::chrono::steady_clock::time_point(); // Reset end time
    Logger::log("Debug: PacketGenerator::resetStats exited.");
}
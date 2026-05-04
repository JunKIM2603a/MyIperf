#include "PacketGenerator.h"
#include "myiperf/Logger.h"
#include "myiperf/Protocol.h"
#include <cstring>
#include <thread> // For std::this_thread::yield()
#include <type_traits>

/**
 * @brief Constructs the PacketGenerator.
 * @param netInterface Pointer to the network interface for sending packets.
 */
PacketGenerator::PacketGenerator(NetworkInterface* netInterface)
    : networkInterface(netInterface), running(false), totalBytesSent(0), packetCounter(0) {}

/**
 * @brief Stops the packet generation process.
 */
void PacketGenerator::stop() {
    Logger::log("Debug: PacketGenerator::stop entered.");
    
    const bool wasRunning = running.exchange(false);
    if (!wasRunning) {
        Logger::log("Debug: PacketGenerator was already stopped.");
    }
    
    m_endTime = std::chrono::steady_clock::now();
    Logger::log("Info: PacketGenerator stopped.");
    Logger::log("Debug: PacketGenerator::stop exited.");
}

std::vector<char> PacketGenerator::createPacket() {
    const size_t packetSize = config.getPacketSize();
    if (packetSize < sizeof(PacketHeader)) {
        return {}; // Invalid packet size
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
    header.packetCounter = packetCounter;
    header.payloadSize = static_cast<uint32_t>(payloadSize);
    header.checksum = checksum;

    // 3. Assemble the final packet for sending.
    std::vector<char> packet(packetSize);
    memcpy(packet.data(), &header, sizeof(PacketHeader));
    if (payloadSize > 0) {
        memcpy(packet.data() + sizeof(PacketHeader), payload_str.data(), payloadSize);
    }

    // 4. Increment the counter for the next packet.
    packetCounter++;

    return packet;
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
    std::memcpy(&m_LastStats, &Stats, sizeof(TestStats));
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

Task PacketGenerator::sendPackets(const Config& cfg) {
    Logger::log("Debug: PacketGenerator::sendPackets entered.");
    Logger::log("Info: Client test parameters - packetSize=" + std::to_string(cfg.getPacketSize()) +
               ", numPackets=" + std::to_string(cfg.getNumPackets()) +
               ", intervalMs=" + std::to_string(cfg.getSendIntervalMs()));

    this->config = cfg;
    running = true;
    totalBytesSent = 0;
    totalPacketsSent = 0;
    packetCounter = 0;
    m_startTime = std::chrono::steady_clock::now();
    memset(&m_LastStats, 0, sizeof(TestStats));

    Logger::log("Info: PacketGenerator coroutine started.");

    try {
        while (running && shouldContinueSending()) {
            // Create and send packet
            auto packet = createPacket();
            if (packet.empty()) {
                Logger::log("Error: Failed to create packet. Stopping generator.");
                break;
            }

            // Send packet using coroutine
            size_t bytesSent = co_await networkInterface->send(packet);

            if (bytesSent > 0) {
                totalBytesSent += bytesSent;
                totalPacketsSent++;
                Logger::log("Info: PacketGenerator sent packet " + std::to_string(packetCounter - 1) +
                           " (size: " + std::to_string(packet.size()) + " bytes)");
            } else {
                Logger::log("Warning: Send operation failed or sent 0 bytes. Stopping generator.");
                break;
            }

            // Wait for interval if specified
            if (cfg.getSendIntervalMs() > 0) {
                co_await delay(std::chrono::milliseconds(cfg.getSendIntervalMs()));
            } else {
                // Yield to prevent 100% CPU usage while maintaining maximum throughput.
                // Do not suspend the coroutine here; no external event would resume it.
                std::this_thread::yield();
            }

            // Check running flag again after waiting
            if (!running) break;
        }
    } catch (const std::exception& e) {
        Logger::log("Error in sendPackets coroutine: " + std::string(e.what()));
    }

    if (running) { // If we exited the loop because we finished, not because we were stopped
        running = false;
        m_endTime = std::chrono::steady_clock::now();
        Logger::log("Info: PacketGenerator reached target packet count: " + std::to_string(cfg.getNumPackets()));
    }

    Logger::log("Debug: PacketGenerator::sendPackets exited.");
}

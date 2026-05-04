#include "ControlProtocol.h"

#include "myiperf/Logger.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace ControlProtocol {

const char* messageTypeToString(MessageType type) {
  switch (type) {
  case MessageType::CONFIG_HANDSHAKE:
    return "CONFIG_HANDSHAKE";
  case MessageType::CONFIG_ACK:
    return "CONFIG_ACK";
  case MessageType::DATA_PACKET:
    return "DATA_PACKET";
  case MessageType::STATS_EXCHANGE:
    return "STATS_EXCHANGE";
  case MessageType::STATS_ACK:
    return "STATS_ACK";
  case MessageType::TEST_FIN:
    return "TEST_FIN";
  case MessageType::CLIENT_READY:
    return "CLIENT_READY";
  case MessageType::SHUTDOWN_ACK:
    return "SHUTDOWN_ACK";
  default:
    return "UNKNOWN";
  }
}

nlohmann::json parseJsonPayload(const std::vector<char>& payload) {
  std::string text(payload.begin(), payload.end());
  return nlohmann::json::parse(text);
}

TestStats parseStatsPayload(const std::vector<char>& payload) {
  return parseJsonPayload(payload).get<TestStats>();
}

std::vector<char> statsToPayload(const TestStats& stats) {
  std::string text = nlohmann::json(stats).dump();
  return std::vector<char>(text.begin(), text.end());
}

std::string formatStatsForLogging(const TestStats& stats) {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(2);
  ss << "\n    - Total Bytes Sent:     " << stats.totalBytesSent
     << "\n    - Total Packets Sent:   " << stats.totalPacketsSent
     << "\n    - Total Bytes Received: " << stats.totalBytesReceived
     << "\n    - Total Packets Received: " << stats.totalPacketsReceived
     << "\n    - Duration:             " << stats.duration << " s"
     << "\n    - Throughput:           " << stats.throughputMbps << " Mbps"
     << "\n    - Sequence Errors:      " << stats.sequenceErrorCount
     << "\n    - Failed Checksums:     " << stats.failedChecksumCount
     << "\n    - Content Mismatches:   " << stats.contentMismatchCount;
  return ss.str();
}

void logPhaseSummary(const std::string& title,
                     const std::string& firstLabel,
                     const TestStats& firstStats,
                     const std::string& secondLabel,
                     const TestStats& secondStats) {
  Logger::log("--- " + title + " ---");
  Logger::log(firstLabel + ":" + formatStatsForLogging(firstStats));
  Logger::log(secondLabel + ":" + formatStatsForLogging(secondStats));
  Logger::log("----------------------------");
}

Task sendControlPacket(NetworkInterface& net,
                       MessageType type,
                       std::vector<char> payload) {
  PacketHeader header{};
  header.startCode = PROTOCOL_START_CODE;
  header.messageType = type;
  header.payloadSize = static_cast<uint32_t>(payload.size());
  header.checksum = calculateChecksum(payload.data(), payload.size());

  std::vector<char> packet(sizeof(PacketHeader) + payload.size());
  std::memcpy(packet.data(), &header, sizeof(PacketHeader));
  if (!payload.empty()) {
    std::memcpy(packet.data() + sizeof(PacketHeader), payload.data(),
                payload.size());
  }

  co_await net.send(packet);
}

} // namespace ControlProtocol

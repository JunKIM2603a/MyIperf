#pragma once

#include "myiperf/CoroutineSupport.h"
#include "myiperf/NetworkInterface.h"
#include "myiperf/Protocol.h"

#include <string>
#include <vector>

namespace ControlProtocol {

const char* messageTypeToString(MessageType type);

nlohmann::json parseJsonPayload(const std::vector<char>& payload);
TestStats parseStatsPayload(const std::vector<char>& payload);
std::vector<char> statsToPayload(const TestStats& stats);

std::string formatStatsForLogging(const TestStats& stats);
void logPhaseSummary(const std::string& title,
                     const std::string& firstLabel,
                     const TestStats& firstStats,
                     const std::string& secondLabel,
                     const TestStats& secondStats);

Task sendControlPacket(NetworkInterface& net,
                       MessageType type,
                       std::vector<char> payload = {});

} // namespace ControlProtocol

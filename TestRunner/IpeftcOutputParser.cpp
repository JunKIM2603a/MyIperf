#include "IpeftcOutputParser.h"

#include <algorithm>
#include <regex>
#include <string>

namespace TestRunner {

namespace {

size_t FindNextMarker(const std::string &text, size_t start,
                      const std::string &marker) {
  size_t pos = text.find(marker, start);
  return pos == std::string::npos ? text.size() : pos;
}

std::string SlicePhase(const std::string &output, const std::string &phase) {
  size_t begin = output.find(phase);
  if (begin == std::string::npos) {
    return {};
  }

  size_t nextPhase = output.find("Test Phase ", begin + phase.size());
  if (nextPhase == std::string::npos) {
    nextPhase = output.size();
  }
  return output.substr(begin, nextPhase - begin);
}

std::string SliceLabelBlock(const std::string &phaseText,
                            const std::string &label) {
  size_t begin = phaseText.find(label);
  if (begin == std::string::npos) {
    return {};
  }

  size_t searchFrom = begin + label.size();
  size_t end = phaseText.size();
  end = std::min(end, FindNextMarker(phaseText, searchFrom, "Client-side"));
  end = std::min(end, FindNextMarker(phaseText, searchFrom, "Server-side"));
  end = std::min(end, FindNextMarker(phaseText, searchFrom,
                                     "----------------------------"));
  return phaseText.substr(begin, end - begin);
}

bool ExtractDouble(const std::string &text, const std::string &label,
                   double &value) {
  std::regex pattern(label + R"(:\s*([0-9]+(?:\.[0-9]+)?))");
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) {
    return false;
  }

  value = std::stod(match[1]);
  return true;
}

bool ExtractLongLong(const std::string &text, const std::string &label,
                     long long &value) {
  std::regex pattern(label + R"(:\s*([0-9]+))");
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) {
    return false;
  }

  value = std::stoll(match[1]);
  return true;
}

bool FillStatsFromBlock(const std::string &block, bool preferSent,
                        TestResult &result) {
  if (block.empty()) {
    return false;
  }

  long long bytesReceived = 0;
  long long bytesSent = 0;
  long long packetsReceived = 0;
  long long packetsSent = 0;
  bool hasReceivedBytes =
      ExtractLongLong(block, "Total Bytes Received", bytesReceived);
  bool hasSentBytes = ExtractLongLong(block, "Total Bytes Sent", bytesSent);
  bool hasReceivedPackets =
      ExtractLongLong(block, "Total Packets Received", packetsReceived);
  bool hasSentPackets = ExtractLongLong(block, "Total Packets Sent", packetsSent);

  if (preferSent) {
    result.totalBytes = hasSentBytes ? bytesSent : bytesReceived;
    result.totalPackets = hasSentPackets ? packetsSent : packetsReceived;
  } else {
    result.totalBytes = hasReceivedBytes ? bytesReceived : bytesSent;
    result.totalPackets = hasReceivedPackets ? packetsReceived : packetsSent;
  }

  ExtractDouble(block, "Duration", result.duration);
  ExtractDouble(block, "Throughput", result.throughput);
  ExtractLongLong(block, "Sequence Errors", result.sequenceErrors);
  ExtractLongLong(block, "Failed Checksums", result.checksumErrors);
  ExtractLongLong(block, "Content Mismatches", result.contentMismatches);

  return (hasReceivedBytes || hasSentBytes) &&
         (hasReceivedPackets || hasSentPackets);
}

bool TryParsePreferredBlock(const std::string &output,
                            const std::string &phaseTitle,
                            const std::string &label, bool preferSent,
                            TestResult &result) {
  std::string phase = SlicePhase(output, phaseTitle);
  std::string block = SliceLabelBlock(phase, label);
  return FillStatsFromBlock(block, preferSent, result);
}

} // namespace

TestResult IpeftcOutputParser::Parse(const std::string &output,
                                     const std::string &role, int port) {
  TestResult result;
  result.role = role;
  result.port = port;

  bool parsed = false;
  if (role == "Client") {
    parsed = TryParsePreferredBlock(output, "Test Phase 2 Summary",
                                    "Client-side", false, result);
    if (!parsed) {
      parsed = TryParsePreferredBlock(output, "Test Phase 1 Summary",
                                      "Client-side", true, result);
    }
  } else {
    parsed = TryParsePreferredBlock(output, "Test Phase 1 Summary",
                                    "Server-side", false, result);
    if (!parsed) {
      parsed = TryParsePreferredBlock(output, "Test Phase 2 Summary",
                                      "Server-side", true, result);
    }
  }

  if (!parsed) {
    result.success = false;
    result.failureReason =
        "Could not parse MyIperf phase summary for role " + role;
    return result;
  }

  if (result.throughput == 0.0 && result.duration > 0 &&
      result.totalBytes > 0) {
    result.throughput =
        (result.totalBytes * 8.0) / (result.duration * 1000000.0);
  }

  result.success = result.totalPackets > 0;
  if (!result.success) {
    result.failureReason = "Parsed result contains zero packets";
  }
  return result;
}

} // namespace TestRunner

#include "Message.h"
#include "../nlohmann/json.hpp"
#include <iostream>

using json = nlohmann::json;

namespace TestRunner2 {

// Helper to convert TestConfig to JSON
void to_json(json &j, const TestConfig &c) {
  j = json{{"mode", c.mode},
           {"configPath", c.configPath},
           {"targetIP", c.targetIP},
           {"port", c.port},
           {"packetSize", c.packetSize},
           {"numPackets", c.numPackets},
           {"sendIntervalMs", c.sendIntervalMs},
           {"saveLogs", c.saveLogs},
           {"protocol", c.protocol}};
}

// Helper to convert JSON to TestConfig
void from_json(const json &j, TestConfig &c) {
  j.at("mode").get_to(c.mode);
  j.at("configPath").get_to(c.configPath);
  j.at("targetIP").get_to(c.targetIP);
  j.at("port").get_to(c.port);
  j.at("packetSize").get_to(c.packetSize);
  j.at("numPackets").get_to(c.numPackets);
  j.at("sendIntervalMs").get_to(c.sendIntervalMs);
  j.at("saveLogs").get_to(c.saveLogs);
  j.at("protocol").get_to(c.protocol);
}

// Helper to convert TestResult to JSON
void to_json(json &j, const TestResult &r) {
  j = json{{"role", r.role},
           {"port", r.port},
           {"duration", r.duration},
           {"throughput", r.throughput},
           {"hostTotalBytes", r.hostTotalBytes},
           {"totalBytes", r.totalBytes},
           {"totalPackets", r.totalPackets},
           {"expectedBytes", r.expectedBytes},
           {"expectedPackets", r.expectedPackets},
           {"sequenceErrors", r.sequenceErrors},
           {"checksumErrors", r.checksumErrors},
           {"contentMismatches", r.contentMismatches},
           {"failureReason", r.failureReason},
           {"success", r.success}};
}

// Helper to convert JSON to TestResult
void from_json(const json &j, TestResult &r) {
  j.at("role").get_to(r.role);
  j.at("port").get_to(r.port);
  j.at("duration").get_to(r.duration);
  j.at("throughput").get_to(r.throughput);
  if (j.contains("hostTotalBytes"))
    j.at("hostTotalBytes").get_to(r.hostTotalBytes);
  j.at("totalBytes").get_to(r.totalBytes);
  j.at("totalPackets").get_to(r.totalPackets);
  j.at("expectedBytes").get_to(r.expectedBytes);
  j.at("expectedPackets").get_to(r.expectedPackets);
  j.at("sequenceErrors").get_to(r.sequenceErrors);
  j.at("checksumErrors").get_to(r.checksumErrors);
  j.at("contentMismatches").get_to(r.contentMismatches);
  j.at("failureReason").get_to(r.failureReason);
  j.at("success").get_to(r.success);
}

// Serialization implementations
std::string SerializeMessage(const Message &msg) {
  json j;
  j["messageType"] = MessageTypeToString(msg.type);
  if (!msg.errorMessage.empty()) {
    j["errorMessage"] = msg.errorMessage;
  }
  return j.dump();
}

std::string SerializeConfigRequest(const ConfigRequestMessage &msg) {
  json j;
  j["messageType"] = MessageTypeToString(msg.type);
  j["testConfig"] = msg.config;
  return j.dump();
}

std::string SerializeServerReady(const ServerReadyMessage &msg) {
  json j;
  j["messageType"] = MessageTypeToString(msg.type);
  j["port"] = msg.port;
  j["serverIP"] = msg.serverIP;
  return j.dump();
}

std::string SerializeTestComplete(const TestCompleteMessage &msg) {
  json j;
  j["messageType"] = MessageTypeToString(msg.type);
  j["port"] = msg.port;
  j["success"] = msg.success;
  return j.dump();
}

std::string SerializeResultsRequest(const ResultsRequestMessage &msg) {
  json j;
  j["messageType"] = MessageTypeToString(msg.type);
  j["port"] = msg.port;
  j["clientResult"] = msg.clientResult;
  return j.dump();
}

std::string SerializeResultsResponse(const ResultsResponseMessage &msg) {
  json j;
  j["messageType"] = MessageTypeToString(msg.type);
  j["serverResult"] = msg.serverResult;
  return j.dump();
}

std::string SerializeError(const ErrorMessage &msg) {
  json j;
  j["messageType"] = MessageTypeToString(msg.type);
  j["error"] = msg.error;
  return j.dump();
}

std::string SerializeServerShutdown(const ServerShutdownMessage &msg) {
  json j;
  j["messageType"] = MessageTypeToString(msg.type);
  return j.dump();
}

// Deserialization implementations
MessageType GetMessageType(const std::string &jsonStr) {
  try {
    auto j = json::parse(jsonStr);
    return StringToMessageType(j.at("messageType").get<std::string>());
  } catch (const std::exception &e) {
    throw std::runtime_error("Failed to parse message type: " +
                             std::string(e.what()));
  }
}

ConfigRequestMessage DeserializeConfigRequest(const std::string &jsonStr) {
  auto j = json::parse(jsonStr);
  ConfigRequestMessage msg;
  msg.config = j.at("testConfig").get<TestConfig>();
  return msg;
}

ServerReadyMessage DeserializeServerReady(const std::string &jsonStr) {
  auto j = json::parse(jsonStr);
  ServerReadyMessage msg;
  msg.port = j.at("port").get<int>();
  msg.serverIP = j.at("serverIP").get<std::string>();
  return msg;
}

TestCompleteMessage DeserializeTestComplete(const std::string &jsonStr) {
  auto j = json::parse(jsonStr);
  TestCompleteMessage msg;
  msg.port = j.at("port").get<int>();
  msg.success = j.at("success").get<bool>();
  return msg;
}

ResultsRequestMessage DeserializeResultsRequest(const std::string &jsonStr) {
  auto j = json::parse(jsonStr);
  ResultsRequestMessage msg;
  msg.port = j.at("port").get<int>();
  msg.clientResult = j.at("clientResult").get<TestResult>();
  return msg;
}

ResultsResponseMessage DeserializeResultsResponse(const std::string &jsonStr) {
  auto j = json::parse(jsonStr);
  ResultsResponseMessage msg;
  msg.serverResult = j.at("serverResult").get<TestResult>();
  return msg;
}

ErrorMessage DeserializeError(const std::string &jsonStr) {
  auto j = json::parse(jsonStr);
  ErrorMessage msg;
  msg.error = j.at("error").get<std::string>();
  return msg;
}

ServerShutdownMessage DeserializeServerShutdown(const std::string &jsonStr) {
  return ServerShutdownMessage();
}

void AnalyzeTestResult(TestResult &result, long long expectedPackets,
                       long long expectedBytes) {
  result.expectedPackets = expectedPackets;
  result.expectedBytes = expectedBytes;

  bool passed = true;
  std::string reason;

  if (result.totalPackets != expectedPackets) {
    passed = false;
    reason +=
        "Packet count mismatch (Rx: " + std::to_string(result.totalPackets) +
        ", Exp: " + std::to_string(expectedPackets) + "); ";
  }

  if (result.sequenceErrors > 0) {
    passed = false;
    reason += "Sequence errors detected (" +
              std::to_string(result.sequenceErrors) + "); ";
  }

  if (result.checksumErrors > 0) {
    passed = false;
    reason += "Checksum errors detected (" +
              std::to_string(result.checksumErrors) + "); ";
  }

  if (result.contentMismatches > 0) {
    passed = false;
    reason += "Content mismatches detected (" +
              std::to_string(result.contentMismatches) + "); ";
  }

  result.success = passed;
  result.failureReason = reason;

  if (passed && result.failureReason.empty()) {
    result.failureReason = "Test passed successfully";
  }
}

} // namespace TestRunner2

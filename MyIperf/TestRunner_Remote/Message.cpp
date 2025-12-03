#include "Message.h"
#include "../nlohmann/json.hpp"
#include <stdexcept>

using json = nlohmann::json;

namespace TestRunner2 {

// Helper to create base JSON with message type
json CreateBaseJson(MessageType type) {
    json j;
    j["messageType"] = MessageTypeToString(type);
    return j;
}

std::string SerializeConfigRequest(const ConfigRequestMessage& msg) {
    json j = CreateBaseJson(MessageType::CONFIG_REQUEST);
    j["testConfig"] = {
        {"port", msg.config.port},
        {"packetSize", msg.config.packetSize},
        {"numPackets", msg.config.numPackets},
        {"sendIntervalMs", msg.config.sendIntervalMs},
        {"protocol", msg.config.protocol},
        {"saveLogs", msg.config.saveLogs}
    };
    return j.dump();
}

std::string SerializeServerReady(const ServerReadyMessage& msg) {
    json j = CreateBaseJson(MessageType::SERVER_READY);
    j["port"] = msg.port;
    j["serverIP"] = msg.serverIP;
    return j.dump();
}

std::string SerializeTestComplete(const TestCompleteMessage& msg) {
    json j = CreateBaseJson(MessageType::TEST_COMPLETE);
    j["port"] = msg.port;
    j["success"] = msg.success;
    return j.dump();
}

std::string SerializeResultsRequest(const ResultsRequestMessage& msg) {
    json j = CreateBaseJson(MessageType::RESULTS_REQUEST);
    j["port"] = msg.port;
    j["clientResult"] = {
        {"role", msg.clientResult.role},
        {"port", msg.clientResult.port},
        {"duration", msg.clientResult.duration},
        {"throughput", msg.clientResult.throughput},
        {"totalBytes", msg.clientResult.totalBytes},
        {"totalPackets", msg.clientResult.totalPackets},
        {"expectedBytes", msg.clientResult.expectedBytes},
        {"expectedPackets", msg.clientResult.expectedPackets},
        {"sequenceErrors", msg.clientResult.sequenceErrors},
        {"checksumErrors", msg.clientResult.checksumErrors},
        {"contentMismatches", msg.clientResult.contentMismatches},
        {"failureReason", msg.clientResult.failureReason},
        {"success", msg.clientResult.success}
    };
    return j.dump();
}

std::string SerializeResultsResponse(const ResultsResponseMessage& msg) {
    json j = CreateBaseJson(MessageType::RESULTS_RESPONSE);
    j["serverResult"] = {
        {"role", msg.serverResult.role},
        {"port", msg.serverResult.port},
        {"duration", msg.serverResult.duration},
        {"throughput", msg.serverResult.throughput},
        {"totalBytes", msg.serverResult.totalBytes},
        {"totalPackets", msg.serverResult.totalPackets},
        {"expectedBytes", msg.serverResult.expectedBytes},
        {"expectedPackets", msg.serverResult.expectedPackets},
        {"sequenceErrors", msg.serverResult.sequenceErrors},
        {"checksumErrors", msg.serverResult.checksumErrors},
        {"contentMismatches", msg.serverResult.contentMismatches},
        {"failureReason", msg.serverResult.failureReason},
        {"success", msg.serverResult.success}
    };
    return j.dump();
}

std::string SerializeError(const ErrorMessage& msg) {
    json j = CreateBaseJson(MessageType::ERROR_MESSAGE);
    j["error"] = msg.error;
    return j.dump();
}

MessageType GetMessageType(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        if (!j.contains("messageType")) {
            throw std::runtime_error("Missing messageType field");
        }
        return StringToMessageType(j["messageType"].get<std::string>());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse message type: ") + e.what());
    }
}

ConfigRequestMessage DeserializeConfigRequest(const std::string& jsonStr) {
    ConfigRequestMessage msg;
    try {
        json j = json::parse(jsonStr);
        if (j.contains("testConfig")) {
            auto& cfg = j["testConfig"];
            msg.config.port = cfg.value("port", Protocol::DEFAULT_TEST_PORT);
            msg.config.packetSize = cfg.value("packetSize", 8192);
            msg.config.numPackets = cfg.value("numPackets", 10000LL);
            msg.config.sendIntervalMs = cfg.value("sendIntervalMs", 0);
            msg.config.protocol = cfg.value("protocol", "TCP");
            msg.config.saveLogs = cfg.value("saveLogs", true);
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to deserialize CONFIG_REQUEST: ") + e.what());
    }
    return msg;
}

ServerReadyMessage DeserializeServerReady(const std::string& jsonStr) {
    ServerReadyMessage msg;
    try {
        json j = json::parse(jsonStr);
        msg.port = j.value("port", 0);
        msg.serverIP = j.value("serverIP", "");
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to deserialize SERVER_READY: ") + e.what());
    }
    return msg;
}

TestCompleteMessage DeserializeTestComplete(const std::string& jsonStr) {
    TestCompleteMessage msg;
    try {
        json j = json::parse(jsonStr);
        msg.port = j.value("port", 0);
        msg.success = j.value("success", false);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to deserialize TEST_COMPLETE: ") + e.what());
    }
    return msg;
}

ResultsRequestMessage DeserializeResultsRequest(const std::string& jsonStr) {
    ResultsRequestMessage msg;
    try {
        json j = json::parse(jsonStr);
        msg.port = j.value("port", 0);
        
        if (j.contains("clientResult")) {
            auto& res = j["clientResult"];
            msg.clientResult.role = res.value("role", "");
            msg.clientResult.port = res.value("port", 0);
            msg.clientResult.duration = res.value("duration", 0.0);
            msg.clientResult.throughput = res.value("throughput", 0.0);
            msg.clientResult.totalBytes = res.value("totalBytes", 0LL);
            msg.clientResult.totalPackets = res.value("totalPackets", 0LL);
            msg.clientResult.expectedBytes = res.value("expectedBytes", 0LL);
            msg.clientResult.expectedPackets = res.value("expectedPackets", 0LL);
            msg.clientResult.sequenceErrors = res.value("sequenceErrors", 0LL);
            msg.clientResult.checksumErrors = res.value("checksumErrors", 0LL);
            msg.clientResult.contentMismatches = res.value("contentMismatches", 0LL);
            msg.clientResult.failureReason = res.value("failureReason", "");
            msg.clientResult.success = res.value("success", false);
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to deserialize RESULTS_REQUEST: ") + e.what());
    }
    return msg;
}

ResultsResponseMessage DeserializeResultsResponse(const std::string& jsonStr) {
    ResultsResponseMessage msg;
    try {
        json j = json::parse(jsonStr);
        if (j.contains("serverResult")) {
            auto& res = j["serverResult"];
            msg.serverResult.role = res.value("role", "");
            msg.serverResult.port = res.value("port", 0);
            msg.serverResult.duration = res.value("duration", 0.0);
            msg.serverResult.throughput = res.value("throughput", 0.0);
            msg.serverResult.totalBytes = res.value("totalBytes", 0LL);
            msg.serverResult.totalPackets = res.value("totalPackets", 0LL);
            msg.serverResult.expectedBytes = res.value("expectedBytes", 0LL);
            msg.serverResult.expectedPackets = res.value("expectedPackets", 0LL);
            msg.serverResult.sequenceErrors = res.value("sequenceErrors", 0LL);
            msg.serverResult.checksumErrors = res.value("checksumErrors", 0LL);
            msg.serverResult.contentMismatches = res.value("contentMismatches", 0LL);
            msg.serverResult.failureReason = res.value("failureReason", "");
            msg.serverResult.success = res.value("success", false);
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to deserialize RESULTS_RESPONSE: ") + e.what());
    }
    return msg;
}

ErrorMessage DeserializeError(const std::string& jsonStr) {
    ErrorMessage msg;
    try {
        json j = json::parse(jsonStr);
        msg.error = j.value("error", "Unknown error");
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to deserialize ERROR_MESSAGE: ") + e.what());
    }
    return msg;
}

} // namespace TestRunner2


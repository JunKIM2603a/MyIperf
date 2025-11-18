#pragma once

#include "Protocol.h"
#include <string>
#include <vector>

namespace TestRunner2 {

// Test configuration structure
struct TestConfig {
    int port = Protocol::DEFAULT_TEST_PORT;
    int packetSize = 8192;
    long long numPackets = 10000;
    int sendIntervalMs = 0;
    std::string protocol = "TCP";
    bool saveLogs = true;
    
    TestConfig() = default;
};

// Test result structure
struct TestResult {
    std::string role;  // "Server" or "Client"
    int port = 0;
    double duration = 0.0;
    double throughput = 0.0;
    long long totalBytes = 0;
    long long totalPackets = 0;
    long long expectedBytes = 0;
    long long expectedPackets = 0;
    long long sequenceErrors = 0;
    long long checksumErrors = 0;
    long long contentMismatches = 0;
    std::string failureReason;
    bool success = false;
    
    TestResult() = default;
};

// Base message structure
struct Message {
    MessageType type;
    std::string errorMessage;
    
    Message() : type(MessageType::HEARTBEAT) {}
    explicit Message(MessageType t) : type(t) {}
    virtual ~Message() = default;
};

// CONFIG_REQUEST message
struct ConfigRequestMessage : public Message {
    TestConfig config;
    
    ConfigRequestMessage() : Message(MessageType::CONFIG_REQUEST) {}
};

// SERVER_READY message
struct ServerReadyMessage : public Message {
    int port = 0;
    std::string serverIP;
    
    ServerReadyMessage() : Message(MessageType::SERVER_READY) {}
};

// TEST_COMPLETE message
struct TestCompleteMessage : public Message {
    int port = 0;
    bool success = false;
    
    TestCompleteMessage() : Message(MessageType::TEST_COMPLETE) {}
};

// RESULTS_REQUEST message
struct ResultsRequestMessage : public Message {
    int port = 0;
    TestResult clientResult;  // Client's test result
    
    ResultsRequestMessage() : Message(MessageType::RESULTS_REQUEST) {}
};

// RESULTS_RESPONSE message
struct ResultsResponseMessage : public Message {
    TestResult serverResult;
    
    ResultsResponseMessage() : Message(MessageType::RESULTS_RESPONSE) {}
};

// ERROR_MESSAGE
struct ErrorMessage : public Message {
    std::string error;
    
    ErrorMessage() : Message(MessageType::ERROR_MESSAGE) {}
    explicit ErrorMessage(const std::string& err) : Message(MessageType::ERROR_MESSAGE), error(err) {}
};

// Message serialization/deserialization functions
std::string SerializeMessage(const Message& msg);
std::string SerializeConfigRequest(const ConfigRequestMessage& msg);
std::string SerializeServerReady(const ServerReadyMessage& msg);
std::string SerializeTestComplete(const TestCompleteMessage& msg);
std::string SerializeResultsRequest(const ResultsRequestMessage& msg);
std::string SerializeResultsResponse(const ResultsResponseMessage& msg);
std::string SerializeError(const ErrorMessage& msg);

// Deserialize based on message type
MessageType GetMessageType(const std::string& json);
ConfigRequestMessage DeserializeConfigRequest(const std::string& json);
ServerReadyMessage DeserializeServerReady(const std::string& json);
TestCompleteMessage DeserializeTestComplete(const std::string& json);
ResultsRequestMessage DeserializeResultsRequest(const std::string& json);
ResultsResponseMessage DeserializeResultsResponse(const std::string& json);
ErrorMessage DeserializeError(const std::string& json);

} // namespace TestRunner2


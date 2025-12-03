#pragma once

#include <string>
#include <stdexcept>

namespace TestRunner2 {

// Message types for control protocol
enum class MessageType {
    CONFIG_REQUEST,      // Client -> Server: Test configuration
    SERVER_READY,        // Server -> Client: IPEFTC server is ready
    TEST_COMPLETE,       // Server -> Client: Test has completed
    RESULTS_REQUEST,     // Client -> Server: Request test results
    RESULTS_RESPONSE,    // Server -> Client: Test results
    ERROR_MESSAGE,       // Bidirectional: Error notification
    HEARTBEAT            // Bidirectional: Connection keep-alive
};

// Convert MessageType to string
inline std::string MessageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::CONFIG_REQUEST: return "CONFIG_REQUEST";
        case MessageType::SERVER_READY: return "SERVER_READY";
        case MessageType::TEST_COMPLETE: return "TEST_COMPLETE";
        case MessageType::RESULTS_REQUEST: return "RESULTS_REQUEST";
        case MessageType::RESULTS_RESPONSE: return "RESULTS_RESPONSE";
        case MessageType::ERROR_MESSAGE: return "ERROR_MESSAGE";
        case MessageType::HEARTBEAT: return "HEARTBEAT";
        default: return "UNKNOWN";
    }
}

// Convert string to MessageType
inline MessageType StringToMessageType(const std::string& str) {
    if (str == "CONFIG_REQUEST") return MessageType::CONFIG_REQUEST;
    if (str == "SERVER_READY") return MessageType::SERVER_READY;
    if (str == "TEST_COMPLETE") return MessageType::TEST_COMPLETE;
    if (str == "RESULTS_REQUEST") return MessageType::RESULTS_REQUEST;
    if (str == "RESULTS_RESPONSE") return MessageType::RESULTS_RESPONSE;
    if (str == "ERROR_MESSAGE") return MessageType::ERROR_MESSAGE;
    if (str == "HEARTBEAT") return MessageType::HEARTBEAT;
    throw std::runtime_error("Unknown message type: " + str);
}

// Protocol constants
namespace Protocol {
    constexpr int DEFAULT_CONTROL_PORT = 9000;
    constexpr int DEFAULT_TEST_PORT = 60000;
    constexpr int MAX_MESSAGE_SIZE = 65536;  // 64KB
    constexpr int CONNECT_TIMEOUT_MS = 10000;
    constexpr int CONFIG_TIMEOUT_MS = 15000;
    constexpr int SERVER_START_TIMEOUT_MS = 20000;
    constexpr int HEARTBEAT_INTERVAL_MS = 5000;
    constexpr int RECV_TIMEOUT_MS = 30000;
}

// Session states
enum class SessionState {
    IDLE,
    CONFIG_RECEIVED,
    SERVER_STARTING,
    SERVER_READY,
    TESTING,
    TEST_COMPLETE,
    RESULTS_SENT,
    ERROR_STATE
};

inline std::string SessionStateToString(SessionState state) {
    switch (state) {
        case SessionState::IDLE: return "IDLE";
        case SessionState::CONFIG_RECEIVED: return "CONFIG_RECEIVED";
        case SessionState::SERVER_STARTING: return "SERVER_STARTING";
        case SessionState::SERVER_READY: return "SERVER_READY";
        case SessionState::TESTING: return "TESTING";
        case SessionState::TEST_COMPLETE: return "TEST_COMPLETE";
        case SessionState::RESULTS_SENT: return "RESULTS_SENT";
        case SessionState::ERROR_STATE: return "ERROR_STATE";
        default: return "UNKNOWN";
    }
}

} // namespace TestRunner2


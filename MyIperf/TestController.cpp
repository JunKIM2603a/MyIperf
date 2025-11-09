#include "TestController.h"
#include "Protocol.h" // For TestStats and json serialization
#include "ConfigParser.h"
#ifdef _WIN32
#include "WinIOCPNetworkInterface.h"
#include <winsock2.h> // For WSAEADDRINUSE
#else
#include "LinuxAsyncNetworkInterface.h"
#endif
#include "nlohmann/json.hpp"
#include <cstring>
#include <thread>
#include <string>
#include <memory>
#include <sstream>
#include <iomanip>

// Helper function to format stats for logging
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

/**
 * @brief Converts a State enum to its string representation for logging.
 * @param state The state to convert.
 * @return The string representation of the state.
 */
const char* stateToString(TestController::State state) {
    switch (state) {
        case TestController::State::IDLE: return "IDLE";
        case TestController::State::INITIALIZING: return "INITIALIZING";
        case TestController::State::CONNECTING: return "CONNECTING";
        case TestController::State::SENDING_CONFIG: return "SENDING_CONFIG";
        case TestController::State::WAITING_FOR_ACK: return "WAITING_FOR_ACK";
        case TestController::State::ACCEPTING: return "ACCEPTING";
        case TestController::State::WAITING_FOR_CONFIG: return "WAITING_FOR_CONFIG";
        case TestController::State::RUNNING_TEST: return "RUNNING_TEST";
        case TestController::State::FINISHING: return "FINISHING";
        case TestController::State::EXCHANGING_STATS: return "EXCHANGING_STATS";
        case TestController::State::WAITING_FOR_CLIENT_READY: return "WAITING_FOR_CLIENT_READY";
        case TestController::State::RUNNING_SERVER_TEST: return "RUNNING_SERVER_TEST";
        case TestController::State::WAITING_FOR_SERVER_FIN: return "WAITING_FOR_SERVER_FIN";
        case TestController::State::SERVER_TEST_FINISHING: return "SERVER_TEST_FINISHING";
        case TestController::State::EXCHANGING_SERVER_STATS: return "EXCHANGING_SERVER_STATS";
        case TestController::State::FINISHED: return "FINISHED";
        case TestController::State::ERRORED: return "ERRORED";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Converts a MessageType enum to its string representation for logging.
 * @param type The message type to convert.
 * @return The string representation of the message type.
 */
const char* MessageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::CONFIG_HANDSHAKE: return "CONFIG_HANDSHAKE";
        case MessageType::CONFIG_ACK: return "CONFIG_ACK";
        case MessageType::DATA_PACKET: return "DATA_PACKET";
        case MessageType::STATS_EXCHANGE: return "STATS_EXCHANGE";
        case MessageType::STATS_ACK: return "STATS_ACK";
        case MessageType::TEST_FIN: return "TEST_FIN";
        case MessageType::CLIENT_READY: return "CLIENT_READY";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Constructs the TestController.
 * Initializes network interface, generator, receiver, and the state timeout timer.
 */
TestController::TestController() {
    std::cerr << "DEBUG: Entering TestController::TestController()\n";
    // Select the appropriate network interface based on the operating system.
#ifdef _WIN32
    networkInterface = std::make_unique<WinIOCPNetworkInterface>();
#else
    networkInterface = std::make_unique<LinuxAsyncNetworkInterface>();
#endif
    // Initialize the core components.
    packetGenerator = std::make_unique<PacketGenerator>(networkInterface.get());
    packetReceiver = std::make_unique<PacketReceiver>(networkInterface.get());
    
    reset(); // Set initial state
    std::cerr << "DEBUG: TestController::TestController() - Finished.\n";
}

/**
 * @brief Destructor for the TestController.
 * Ensures that the test and the timer thread are stopped cleanly.
 */
TestController::~TestController() {
    stopTest();
}

/**
 * @brief Resets all member variables to their initial state for a new test.
 */
void TestController::reset() {
    currentState = State::IDLE;
    m_stopped = false;
    m_expectedDataPacketCounter = 0;
    testCompletionPromise_set = false;
    m_cliBlockFlag = false;
    m_contentMismatchCount = 0;

    currentConfig = Config(); // Reset to default config
    m_remoteStats = {};
    m_clientStatsPhase1 = {};
    m_serverStatsPhase1 = {};
    m_clientStatsPhase2 = {};
    m_serverStatsPhase2 = {};

    if (packetGenerator) {
        packetGenerator->resetStats();
    }
    if (packetReceiver) {
        packetReceiver->resetStats();
    }
    
    // Re-create the promise for the next test run
    testCompletionPromise = std::promise<void>();
}


nlohmann::json TestController::parseStats(const std::vector<char>& payload) const {
    std::string stats_str(payload.begin(), payload.end());
    return nlohmann::json::parse(stats_str);
}

/**
 * @brief Starts a new test with the given configuration.
 * @param config The configuration for the test.
 */
void TestController::startTest(const Config& config) {
    std::cerr << "DEBUG: Entering TestController::startTest()\n";
    
    reset(); // Reset all state variables for a clean test run.

    this->currentConfig = config; // Store the configuration for this session.

    std::string logMessage = "Info: Starting test in ";
    logMessage += (config.getMode() == Config::TestMode::CLIENT ? "CLIENT" : "SERVER");
    logMessage += " mode.";
    Logger::log(logMessage);

    if (config.getMode() == Config::TestMode::SERVER) {
        if (!networkInterface->initialize(config.getTargetIP(), config.getPort())) {
            Logger::log("Error: Server network interface initialization failed.");
            transitionTo(State::ERRORED);
            return;
        }
        // Setup the listening socket before trying to accept connections.
#ifdef _WIN32
        WinIOCPNetworkInterface* winInterface = static_cast<WinIOCPNetworkInterface*>(networkInterface.get());
        if (!winInterface->setupListeningSocket(config.getTargetIP(), config.getPort())) {
            if (WSAGetLastError() == WSAEADDRINUSE) {
                Logger::log("Error: Failed to set up listening socket. The port " + std::to_string(config.getPort()) + " is already in use.");
            } else {
                Logger::log("Error: Failed to set up listening socket.");
            }
            transitionTo(State::ERRORED);
            return;
        }
#endif
        transitionTo(State::ACCEPTING);
    } else { // Client mode
        if (!networkInterface->initialize("0.0.0.0", 0)) { // Client binds to any available port.
            Logger::log("Error: Client network interface initialization failed.");
            transitionTo(State::ERRORED);
            return;
        }
        transitionTo(State::CONNECTING);
    }
}


/**
 * @brief Stops the current test and closes network resources.
 */
void TestController::stopTest() {
    Logger::log("Debug: TestController::stopTest() called.");
    if (m_stopped.exchange(true)) {
        Logger::log("Debug: TestController::stopTest() already stopped, returning.");
        return; // Already stopped
    }
    Logger::log("Info: Stopping the test components.");
    Logger::log("Debug: Calling packetGenerator->stop().");
    packetGenerator->stop();
    Logger::log("Debug: packetGenerator->stop() completed.");
    Logger::log("Debug: Calling packetReceiver->stop().");
    packetReceiver->stop();
    Logger::log("Debug: packetReceiver->stop() completed.");
    Logger::log("Debug: Calling networkInterface->close().");
    networkInterface->close();
    Logger::log("Debug: networkInterface->close() completed.");
    Logger::log("Debug: TestController::stopTest() finished.");
}

/**
 * @brief Transitions the state machine to a new state.
 * This is the central function controlling the test flow.
 * @param newState The state to transition to.
 */
void TestController::transitionTo(State newState) {
    std::lock_guard<std::mutex> lock(m_stateMachineMutex);
    transitionTo_nolock(newState);
}

/**
 * @brief The actual implementation of the state transition.
 * This function is NOT thread-safe and must be called only when the state machine mutex is already held.
 * @param newState The state to transition to.
 */
void TestController::transitionTo_nolock(State newState) {
    currentState = newState;
    Logger::log("Info: Transitioning to state: " + std::string(stateToString(newState)));
    switch (newState) {
        case State::CONNECTING: {
            Logger::log("Info: Client attempting to connect to " + currentConfig.getTargetIP() + ":" + std::to_string(currentConfig.getPort()));
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            networkInterface->asyncConnect(currentConfig.getTargetIP(), currentConfig.getPort(), [this](bool success) {
                if (success) {
                    Logger::log("Info: Client connected successfully. Starting packet receiver.");
                    packetReceiver->start(
                        [this](const PacketHeader& header, const std::vector<char>& payload) {
                            onPacket(header, payload);
                        },
                        [this]() {
                            Logger::log("Info: Client receiver completed (server disconnected). Finishing test.");
                            if (currentState != State::FINISHED && currentState != State::ERRORED) {
                                transitionTo(State::FINISHED);
                            }
                        }
                    );
                    transitionTo(State::SENDING_CONFIG);
                } else {
                    Logger::log("Error: Client failed to connect to the server.");
                    transitionTo(State::ERRORED);
                }
            });
            break;
        }
        case State::ACCEPTING: {
            Logger::log("Info: Server waiting for a client connection on " + currentConfig.getTargetIP() + ":" + std::to_string(currentConfig.getPort()));
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            networkInterface->asyncAccept([this](bool success, const std::string& clientIP, int clientPort) {
                if (success) {
                    Logger::log("Info: Server accepted a client from " + clientIP + ":" + std::to_string(clientPort));
                    // Start receiver immediately to handle config handshake
                    packetReceiver->start(
                        [this](const PacketHeader& header, const std::vector<char>& payload) {
                            onPacket(header, payload);
                        },
                        [this]() {
                            Logger::log("Info: Server receiver completed (client disconnected). Finishing test.");
                            transitionTo(State::FINISHED);
                        }
                    );
                    transitionTo(State::WAITING_FOR_CONFIG);
                } else {
                    Logger::log("Error: Server failed to accept a client connection.");
                    transitionTo(State::ERRORED);
                }
            });
            break;
        }
        case State::SENDING_CONFIG: {
            Logger::log("Info: Client sending configuration packet.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            std::string configStr = currentConfig.toJson().dump();
            std::vector<char> configData(configStr.begin(), configStr.end());

            PacketHeader header{};
            header.startCode = PROTOCOL_START_CODE;
            header.messageType = MessageType::CONFIG_HANDSHAKE;
            header.payloadSize = static_cast<uint32_t>(configData.size());
            header.checksum = calculateChecksum(configData.data(), configData.size());

            std::vector<char> packet(sizeof(PacketHeader) + configData.size());
            std::memcpy(packet.data(), &header, sizeof(PacketHeader));
            std::memcpy(packet.data() + sizeof(PacketHeader), configData.data(), configData.size());

            networkInterface->asyncSend(packet, [this](size_t bytesSent) {
                if (bytesSent > 0) {
                    Logger::log("Info: Client sent config packet successfully (" + std::to_string(bytesSent) + " bytes).");
                    transitionTo(State::WAITING_FOR_ACK);
                } else {
                    Logger::log("Error: Client failed to send config packet.");
                    transitionTo(State::ERRORED);
                }
            });
            break;
        }
        case State::WAITING_FOR_ACK: {
            Logger::log("Info: Client waiting for server acknowledgment.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            break;
        }
        case State::WAITING_FOR_CONFIG: {
            Logger::log("Info: Server waiting for client configuration packet.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            break;
        }
        case State::RUNNING_TEST: {
            Logger::log("Info: Handshake complete. Starting data transmission test.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            m_testStartTime = std::chrono::steady_clock::now();
            if (currentConfig.getMode() == Config::TestMode::CLIENT) {
                packetGenerator->start(currentConfig, [this]() {
                    Logger::log("Info: Client generator completed.");
                    onTestCompleted();
                });
            } else { // SERVER
                // Reset stats here to only measure the data transfer phase
                packetReceiver->resetStats();
            }
            break;
        }
        case State::FINISHING: {
            Logger::log("Info: Initiating test completion handshake.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));

            PacketHeader header{};
            header.startCode = PROTOCOL_START_CODE;
            header.messageType = MessageType::TEST_FIN;
            header.payloadSize = 0;
            header.checksum = 0;

            std::vector<char> packet(sizeof(PacketHeader));
            std::memcpy(packet.data(), &header, sizeof(PacketHeader));

            networkInterface->asyncSend(packet, [this](size_t bytesSent) {
                if (bytesSent > 0) {
                    Logger::log("Info: Sent TEST_FIN successfully.");
                } else {
                    Logger::log("Error: Failed to send TEST_FIN.");
                    transitionTo_nolock(State::ERRORED);
                }
            });
            break;
        }
        case State::SERVER_TEST_FINISHING: {
            Logger::log("Info: Server finishing server-to-client test.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));

            PacketHeader header{};
            header.startCode = PROTOCOL_START_CODE;
            header.messageType = MessageType::TEST_FIN;
            header.payloadSize = 0;
            header.checksum = 0;

            std::vector<char> packet(sizeof(PacketHeader));
            std::memcpy(packet.data(), &header, sizeof(PacketHeader));

            networkInterface->asyncSend(packet, [this](size_t bytesSent) {
                if (bytesSent > 0) {
                    Logger::log("Info: Server sent TEST_FIN for server-to-client test.");
                    // Now server waits for the client to send its stats
                } else {
                    Logger::log("Error: Failed to send TEST_FIN for server-to-client test.");
                    transitionTo_nolock(State::ERRORED);
                }
            });
            break;
        }
        case State::EXCHANGING_STATS: {
            Logger::log("Info: Client initiating statistics exchange.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            sendClientStatsAndAwaitAck();
            break;
        }
        case State::WAITING_FOR_CLIENT_READY: {
            Logger::log("Info: Server waiting for client to be ready for phase 2.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            break;
        }
        case State::RUNNING_SERVER_TEST: {
            Logger::log("Info: Server starting data transmission to client.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            m_testStartTime = std::chrono::steady_clock::now();
            // Server is already configured to send from the previous state
            break;
        }
        case State::WAITING_FOR_SERVER_FIN: {
            Logger::log("Info: Client waiting for server to finish sending data.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            // Client resets its receiver stats to measure the server-to-client test
            packetReceiver->resetStats();
            break;
        }
        case State::EXCHANGING_SERVER_STATS: {
            Logger::log("Info: Client waiting for final stats from server.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            break;
        }
        case State::FINISHED: {
            Logger::log("Info: Test finished successfully. Shutting down.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));

            Logger::log("\n=============== FINAL TEST SUMMARY ===============");
            Logger::log("\n--- Phase 1: Client to Server ---");
            Logger::log("Client Sent:" + formatStatsForLogging(m_clientStatsPhase1));
            Logger::log("Server Received:" + formatStatsForLogging(m_serverStatsPhase1));
            Logger::log("\n--- Phase 2: Server to Client ---");
            Logger::log("Server Sent:" + formatStatsForLogging(m_serverStatsPhase2));
            Logger::log("Client Received:" + formatStatsForLogging(m_clientStatsPhase2));
            Logger::log("================================================\n");

            if (!testCompletionPromise_set) {
                testCompletionPromise.set_value();
                testCompletionPromise_set = true;
            }
            // Notify CLIHandler to unblock
            {
                std::lock_guard<std::mutex> lock(m_cliBlockMutex);
                m_cliBlockFlag = true;
            }
            m_cliBlockCv.notify_all();
            break;
        }
        case State::ERRORED: {
            Logger::log("Error: An unrecoverable error occurred. Shutting down.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            if (currentConfig.getMode() == Config::TestMode::CLIENT) {
                TestStats localStats = packetGenerator->getStats();
                Logger::writeFinalReport("CLIENT", localStats, m_remoteStats);
            } else { // SERVER
                TestStats localStats = packetReceiver->getStats();
                Logger::writeFinalReport("SERVER", localStats, m_remoteStats);
            }
            // The call to stopTest() is removed from here to prevent deadlocks.
            // The worker thread, which calls this transition, cannot join itself.
            // The main thread, after being unblocked by the promise, is now responsible
            // for calling stopTest() to clean up resources.
            // stopTest();
            if (!testCompletionPromise_set) {
                testCompletionPromise.set_value();
                testCompletionPromise_set = true;
            }
            // Notify CLIHandler to unblock
            {
                std::lock_guard<std::mutex> lock(m_cliBlockMutex);
                m_cliBlockFlag = true;
            }
            m_cliBlockCv.notify_all();
            break;
        }
        default:
            Logger::log("Warning: Unhandled state transition: " + std::string(stateToString(newState)));
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            break;
}
}

/**
 * @brief Handles incoming packets from the PacketReceiver.
 * @param header The packet header.
 * @param payload The packet payload.
 */
void TestController::onPacket(const PacketHeader& header, const std::vector<char>& payload) {
    std::lock_guard<std::mutex> lock(m_stateMachineMutex);

    if (currentConfig.getMode() == Config::TestMode::SERVER) {
        switch (header.messageType) {
            case MessageType::CONFIG_HANDSHAKE:
                DebugPause(string_format("[%s:%d] MessageType::%s", __FUNCTION__, __LINE__, MessageTypeToString(header.messageType)));
                if (currentState == State::WAITING_FOR_CONFIG) {
                    Logger::log("Info: Server received config packet.");
                    try {
                        std::string configStr(payload.begin(), payload.end());
                        Config receivedConfig = Config::fromJson(nlohmann::json::parse(configStr));
                        receivedConfig.setMode(Config::TestMode::SERVER); // Ensure mode is server
                        this->currentConfig = receivedConfig;

                        PacketHeader ackHeader{};
                        ackHeader.startCode = PROTOCOL_START_CODE;
                        ackHeader.messageType = MessageType::CONFIG_ACK;
                        std::vector<char> ackPacket(sizeof(PacketHeader));
                        memcpy(ackPacket.data(), &ackHeader, sizeof(PacketHeader));

                        networkInterface->asyncSend(ackPacket, [this](size_t bytesSent) {
                            if (bytesSent > 0) {
                                Logger::log("Info: Server sent config ACK.");
                                transitionTo(State::RUNNING_TEST);
                            } else {
                                Logger::log("Error: Server failed to send config ACK.");
                                transitionTo(State::ERRORED);
                            }
                        });
                    } catch (const std::exception& e) {
                        Logger::log("Error: Failed to process config packet: " + std::string(e.what()));
                        transitionTo(State::ERRORED);
                    }
                }
                break;
            case MessageType::CLIENT_READY:
                DebugPause(string_format("[%s:%d] MessageType::%s", __FUNCTION__, __LINE__, MessageTypeToString(header.messageType)));
                if (currentState == State::WAITING_FOR_CLIENT_READY) {
                    Logger::log("Info: Server received CLIENT_READY. Starting server-to-client test.");
                    packetGenerator->resetStats();
                    packetGenerator->start(currentConfig, [this]() {
                        Logger::log("Info: Server generator completed sending data.");
                        onTestCompleted();
                    });
                    transitionTo_nolock(State::RUNNING_SERVER_TEST);
                }
                break;
            case MessageType::TEST_FIN:
                DebugPause(string_format("[%s:%d] MessageType::%s", __FUNCTION__, __LINE__, MessageTypeToString(header.messageType)));
                if (currentState == State::RUNNING_TEST) {
                    Logger::log("Info: Server received TEST_FIN from client. Replying and finishing.");
                    transitionTo_nolock(State::FINISHING);
                }
                break;
            case MessageType::STATS_EXCHANGE: {
                DebugPause(string_format("[%s:%d] MessageType::%s", __FUNCTION__, __LINE__, MessageTypeToString(header.messageType)));
                if (currentState == State::FINISHING) {
                    Logger::log("Info: Server received STATS_EXCHANGE from client.");
                    try {
                        nlohmann::json client_stats_payload = parseStats(payload);
                        m_clientStatsPhase1 = client_stats_payload.get<TestStats>();
                        m_serverStatsPhase1 = packetReceiver->getStats();

                        Logger::log("--- Test Phase 1 Summary ---");
                        Logger::log("Client-side (sent):" + formatStatsForLogging(m_clientStatsPhase1));
                        Logger::log("Server-side (received):" + formatStatsForLogging(m_serverStatsPhase1));
                        Logger::log("----------------------------");

                    } catch (const std::exception& e) {
                        Logger::log("Warning: Could not process phase 1 client stats: " + std::string(e.what()));
                    }

                    // Send server stats back to client in the ACK
                    nlohmann::json ack_payload = m_serverStatsPhase1;
                    std::string ack_payload_str = ack_payload.dump();
                    std::vector<char> payload_data(ack_payload_str.begin(), ack_payload_str.end());

                    PacketHeader ackHeader{};
                    ackHeader.startCode = PROTOCOL_START_CODE;
                    ackHeader.messageType = MessageType::STATS_ACK;
                    ackHeader.payloadSize = static_cast<uint32_t>(payload_data.size());
                    ackHeader.checksum = calculateChecksum(payload_data.data(), payload_data.size());

                    std::vector<char> packet(sizeof(PacketHeader) + payload_data.size());
                    std::memcpy(packet.data(), &ackHeader, sizeof(PacketHeader));
                    if (!payload_data.empty()) {
                        std::memcpy(packet.data() + sizeof(PacketHeader), payload_data.data(), payload_data.size());
                    }

                    networkInterface->asyncSend(packet, [this](size_t) {
                        Logger::log("Info: Server sent STATS_ACK with its stats.");
                        transitionTo(State::WAITING_FOR_CLIENT_READY);
                    });
                } else if (currentState == State::SERVER_TEST_FINISHING) {
                    Logger::log("Info: Server received STATS_EXCHANGE from client for server-to-client test.");
                    try {
                        nlohmann::json client_stats_payload = parseStats(payload);
                        m_clientStatsPhase2 = client_stats_payload.get<TestStats>();
                        m_serverStatsPhase2 = packetGenerator->getStats();

                        Logger::log("--- Test Phase 2 Summary ---");
                        Logger::log("Server-side (sent):" + formatStatsForLogging(m_serverStatsPhase2));
                        Logger::log("Client-side (received):" + formatStatsForLogging(m_clientStatsPhase2));
                        Logger::log("----------------------------");

                    } catch (const std::exception& e) {
                        Logger::log("Warning: Could not process phase 2 client stats: " + std::string(e.what()));
                    }

                    // Send server's generator stats back to client
                    nlohmann::json ack_payload = m_serverStatsPhase2;
                    std::string ack_payload_str = ack_payload.dump();
                    std::vector<char> payload_data(ack_payload_str.begin(), ack_payload_str.end());

                    PacketHeader ackHeader{};
                    ackHeader.startCode = PROTOCOL_START_CODE;
                    ackHeader.messageType = MessageType::STATS_ACK;
                    ackHeader.payloadSize = static_cast<uint32_t>(payload_data.size());
                    ackHeader.checksum = calculateChecksum(payload_data.data(), payload_data.size());

                    std::vector<char> packet(sizeof(PacketHeader) + payload_data.size());
                    std::memcpy(packet.data(), &ackHeader, sizeof(PacketHeader));
                    if (!payload_data.empty()) {
                        std::memcpy(packet.data() + sizeof(PacketHeader), payload_data.data(), payload_data.size());
                    }

                    networkInterface->asyncSend(packet, [this](size_t) {
                        Logger::log("Info: Server sent final STATS_ACK with its generator stats.");
                        transitionTo(State::FINISHED);
                    });
                } else {
                    Logger::log("Warning: Received STATS_EXCHANGE in unexpected state: " + std::string(stateToString(currentState)));
                }
                break;
            }
            default:
                DebugPause(string_format("[%s:%d] MessageType::%s", __FUNCTION__, __LINE__, MessageTypeToString(header.messageType)));
                break;
        }
    } else { // CLIENT
        switch (header.messageType) {
            case MessageType::CONFIG_ACK:
                DebugPause(string_format("[%s:%d] MessageType::%s", __FUNCTION__, __LINE__, MessageTypeToString(header.messageType)));
                if (currentState == State::WAITING_FOR_ACK) {
                    Logger::log("Info: Client received server ACK. Starting test.");
                    transitionTo_nolock(State::RUNNING_TEST);
                }
                break;
            case MessageType::TEST_FIN:
                DebugPause(string_format("[%s:%d] MessageType::%s", __FUNCTION__, __LINE__, MessageTypeToString(header.messageType)));
                if (currentState == State::FINISHING) {
                    Logger::log("Info: Client received TEST_FIN from server. Handshake complete.");
                    transitionTo_nolock(State::EXCHANGING_STATS);
                } else if (currentState == State::WAITING_FOR_SERVER_FIN) {
                    Logger::log("Info: Client received TEST_FIN from server, concluding server-to-client test.");
                    TestStats clientReceiverStats = packetReceiver->getStats();
                    m_clientStatsPhase2 = clientReceiverStats; // Store the stats

                    nlohmann::json stats_payload = clientReceiverStats;
                    std::string payload_str = stats_payload.dump();
                    std::vector<char> payload_data(payload_str.begin(), payload_str.end());

                    PacketHeader statsHeader{};
                    statsHeader.startCode = PROTOCOL_START_CODE;
                    statsHeader.messageType = MessageType::STATS_EXCHANGE;
                    statsHeader.payloadSize = static_cast<uint32_t>(payload_data.size());
                    statsHeader.checksum = calculateChecksum(payload_data.data(), payload_data.size());

                    std::vector<char> packet(sizeof(PacketHeader) + payload_data.size());
                    std::memcpy(packet.data(), &statsHeader, sizeof(PacketHeader));
                    if (!payload_data.empty()) {
                        std::memcpy(packet.data() + sizeof(PacketHeader), payload_data.data(), payload_data.size());
                    }

                    networkInterface->asyncSend(packet, [this, payload_str](size_t bytesSent) {
                        if (bytesSent > 0) {
                            Logger::log("Info: Client sent STATS_EXCHANGE for server-to-client test.");
                            Logger::log("Info: Client Stats (Phase 2): " + payload_str);
                            transitionTo_nolock(State::EXCHANGING_SERVER_STATS);
                        } else {
                            Logger::log("Error: Client failed to send STATS_EXCHANGE for server-to-client test.");
                            transitionTo_nolock(State::ERRORED);
                        }
                    });
                }
                break;
            case MessageType::STATS_ACK:
                if (currentState == State::EXCHANGING_STATS) {
                    Logger::log("Info: Client received STATS_ACK. Preparing for server-to-client test.");
                    try {
                        nlohmann::json server_stats_payload = parseStats(payload);
                        m_serverStatsPhase1 = server_stats_payload.get<TestStats>();
                        m_clientStatsPhase1 = packetGenerator->lastStats();

                        Logger::log("--- Test Phase 1 Summary ---");
                        Logger::log("Client-side (sent):" + formatStatsForLogging(m_clientStatsPhase1));
                        Logger::log("Server-side (received):" + formatStatsForLogging(m_serverStatsPhase1));
                        Logger::log("----------------------------");

                    } catch (const std::exception& e) {
                        Logger::log("Warning: Could not process phase 1 server stats: " + std::string(e.what()));
                    }

                    // Signal server that client is ready for the next phase
                    PacketHeader readyHeader{};
                    readyHeader.startCode = PROTOCOL_START_CODE;
                    readyHeader.messageType = MessageType::CLIENT_READY;
                    std::vector<char> readyPacket(sizeof(PacketHeader));
                    memcpy(readyPacket.data(), &readyHeader, sizeof(PacketHeader));

                    networkInterface->asyncSend(readyPacket, [this](size_t bytesSent) {
                        if (bytesSent > 0) {
                            Logger::log("Info: Client sent CLIENT_READY.");
                            transitionTo_nolock(State::WAITING_FOR_SERVER_FIN);
                        } else {
                            Logger::log("Error: Client failed to send CLIENT_READY.");
                            transitionTo_nolock(State::ERRORED);
                        }
                    });
                } else if (currentState == State::EXCHANGING_SERVER_STATS) {
                    Logger::log("Info: Client received final STATS_ACK from server.");
                    try {
                        nlohmann::json server_stats_payload = parseStats(payload);
                        m_serverStatsPhase2 = server_stats_payload.get<TestStats>();

                        Logger::log("--- Test Phase 2 Summary ---");
                        Logger::log("Server-side (sent):" + formatStatsForLogging(m_serverStatsPhase2));
                        Logger::log("Client-side (received):" + formatStatsForLogging(m_clientStatsPhase2));
                        Logger::log("----------------------------");

                    } catch (const std::exception& e) {
                        Logger::log("Warning: Could not parse final server stats: " + std::string(e.what()));
                    }
                    transitionTo_nolock(State::FINISHED);
                }
                break;
            case MessageType::DATA_PACKET:
                // Data packets are handled by the PacketReceiver for statistical purposes.
                // The TestController's state machine does not need to act on them, so we simply ignore them.
                break;
            default:
                Logger::log("Warning: Client received an unexpected message type: " + std::to_string((int)header.messageType));
                DebugPause(string_format("[%s:%d] MessageType::%s", __FUNCTION__, __LINE__, MessageTypeToString(header.messageType)));
                break;
        }
    }
}

/**
 * @brief Callback for when the PacketGenerator has completed its sending duration.
 * This function is called on the client side when the test duration is over.
 */
void TestController::onTestCompleted() {
    Logger::log("Info: Data transmission completed.");
    if (currentConfig.getMode() == Config::TestMode::CLIENT) {
        // Client finished sending data in the first phase.
        transitionTo(State::FINISHING);
    } else { // SERVER
        // Server finished sending data in the second phase.
        transitionTo(State::SERVER_TEST_FINISHING);
    }
}

/**
 * @brief Sends client-side statistics to the server and waits for acknowledgment.
 *
 * This function is called on the client side after the main data transfer is complete.
 * It sends a final statistics packet to the server and then waits for a corresponding
 * acknowledgment from the server to ensure the stats were received.
 */
void TestController::sendClientStatsAndAwaitAck() {
    using json = nlohmann::json;
    TestStats clientStats = packetGenerator->getStats();
    packetGenerator->saveLastStats(clientStats);
    json stats_payload = clientStats; // Use the to_json function implicitly
    std::string payload_str = stats_payload.dump();

    std::vector<char> payload_data(payload_str.begin(), payload_str.end());

    PacketHeader header{};
    header.startCode = PROTOCOL_START_CODE;
    header.messageType = MessageType::STATS_EXCHANGE;
    header.payloadSize = static_cast<uint32_t>(payload_data.size());
    header.checksum = calculateChecksum(payload_data.data(), payload_data.size());

    std::vector<char> packet(sizeof(PacketHeader) + payload_data.size());
    std::memcpy(packet.data(), &header, sizeof(PacketHeader));
    if (!payload_data.empty()) {
        std::memcpy(packet.data() + sizeof(PacketHeader), payload_data.data(), payload_data.size());
    }

    networkInterface->asyncSend(packet, [this, payload_data](size_t bytesSent) {
        if (bytesSent > 0) {
            Logger::log("Info: Client sent STATS_EXCHANGE successfully.");
            nlohmann::json stats_payload = parseStats(payload_data);
            Logger::log("Info: Client Stats (sent): " + stats_payload.dump());
            // Now we wait for STATS_ACK in onPacket
        } else {
            Logger::log("Error: Client failed to send STATS_EXCHANGE.");
            transitionTo(State::ERRORED);
        }
    });
}

void TestController::cancelTimer() {
    // No-op placeholder for now
}
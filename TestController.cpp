#include "TestController.h"
#include "Protocol.h" // For TestStats and json serialization
#include "ConfigParser.h"
#ifdef _WIN32
#include "WinIOCPNetworkInterface.h"
#else
#include "LinuxAsyncNetworkInterface.h"
#endif
#include "nlohmann/json.hpp"
#include <cstring>
#include <thread>

// Helper function to convert State enum to string for logging
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
        case TestController::State::EXCHANGING_STATS: return "EXCHANGING_STATS";
        case TestController::State::FINISHED: return "FINISHED";
        case TestController::State::ERRORED: return "ERRORED";
        default: return "UNKNOWN";
    }
}

// Helper function to convert MessageType enum to string for logging
const char* MessageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::CONFIG_HANDSHAKE: return "CONFIG_HANDSHAKE";
        case MessageType::CONFIG_ACK: return "CONFIG_ACK";
        case MessageType::DATA_PACKET: return "DATA_PACKET";
        case MessageType::STATS_EXCHANGE: return "STATS_EXCHANGE";
        case MessageType::STATS_ACK: return "STATS_ACK";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Constructs the TestController.
 * Initializes network interface, generator, receiver, and the state timeout timer.
 */
TestController::TestController() : currentState(State::IDLE), m_expectedDataPacketCounter(0), testCompletionPromise_set(false), m_cliBlockFlag(false) {
    std::cerr << "DEBUG: Entering TestController::TestController()\n";
    std::cerr << "DEBUG: TestController::TestController() - Before networkInterface creation.\n"; // NEW LOG
    // Select the appropriate network interface based on the operating system.
#ifdef _WIN32
    networkInterface = std::make_unique<WinIOCPNetworkInterface>();
#else
    networkInterface = std::make_unique<LinuxAsyncNetworkInterface>();
#endif
    std::cerr << "DEBUG: TestController::TestController() - After networkInterface creation.\n"; // NEW LOG
    // Initialize the core components.
    packetGenerator = std::make_unique<PacketGenerator>(networkInterface.get());
    packetReceiver = std::make_unique<PacketReceiver>(networkInterface.get());
    std::cerr << "DEBUG: TestController::TestController() - Finished.\n"; // NEW LOG
}

/**
 * @brief Destructor for the TestController.
 * Ensures that the test and the timer thread are stopped cleanly.
 */
TestController::~TestController() {
    stopTest();
}

/**
 * @brief Starts a new test with the given configuration.
 * @param config The configuration for the test.
 */
void TestController::startTest(const Config& config) {
    std::cerr << "DEBUG: Entering TestController::startTest()\n";
    testCompletionPromise = std::promise<void>(); // Reset the completion promise for the new test.
    testCompletionPromise_set = false; // Reset flag for new test.
    m_cliBlockFlag = false; // Reset for new test.
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
            Logger::log("Error: Failed to set up listening socket.");
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
        case State::EXCHANGING_STATS: {
            Logger::log("Info: Client initiating statistics exchange.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            sendClientStatsAndAwaitAck();
            break;
        }
        case State::FINISHED: {
            Logger::log("Info: Test finished successfully. Shutting down.");
            DebugPause(string_format("[%s:%d] State::%s",  __FUNCTION__, __LINE__,stateToString(newState)));
            if (currentConfig.getMode() == Config::TestMode::CLIENT) {
                TestStats localStats = packetGenerator->getStats();
                Logger::writeFinalReport("CLIENT", localStats, m_remoteStats);
            } else { // SERVER
                TestStats localStats = packetReceiver->getStats();
                Logger::writeFinalReport("SERVER", localStats, m_remoteStats);
            }
            stopTest(); // Stop components before signaling completion
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
            stopTest(); // Stop components before signaling completion
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
                DebugPause(string_format("[%s:%d] MessageType::%s",  __FUNCTION__, __LINE__,MessageTypeToString(header.messageType)));
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
            case MessageType::STATS_EXCHANGE: {
                Logger::log("Info: Server received STATS_EXCHANGE from client.");
                DebugPause(string_format("[%s:%d] MessageType::%s",  __FUNCTION__, __LINE__,MessageTypeToString(header.messageType)));
                
                // Parse client's full stats and store them as remote stats
                try {
                    using json = nlohmann::json;
                    std::string stats_str(payload.begin(), payload.end());
                    json stats_payload = json::parse(stats_str);
                    m_remoteStats = stats_payload.get<TestStats>();
                    Logger::log("Info: Client Stats: " + stats_payload.dump());
                } catch(const std::exception& e) {
                    Logger::log("Warning: Could not parse client stats: " + std::string(e.what()));
                }

                // Send server stats back to client
                TestStats serverStats = packetReceiver->getStats();
                using json = nlohmann::json;
                json ack_payload = serverStats; // Use the to_json function implicitly
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

                networkInterface->asyncSend(packet, [this](size_t){
                    Logger::log("Info: Server sent STATS_ACK with its stats.");
                    transitionTo(State::FINISHED);
                });
                break;
            }
            default:
                DebugPause(string_format("[%s:%d] MessageType::%s",  __FUNCTION__, __LINE__,MessageTypeToString(header.messageType)));
                // Data packets are handled by the PacketReceiver directly, not here.
                break;
        }
    } else { // CLIENT
        switch (header.messageType) {
            case MessageType::CONFIG_ACK:
                DebugPause(string_format("[%s:%d] MessageType::%s",  __FUNCTION__, __LINE__,MessageTypeToString(header.messageType)));
                if (currentState == State::WAITING_FOR_ACK) {
                    Logger::log("Info: Client received server ACK. Starting test.");
                    transitionTo_nolock(State::RUNNING_TEST);
                }
                break;
            case MessageType::STATS_ACK:
                DebugPause(string_format("[%s:%d] MessageType::%s",  __FUNCTION__, __LINE__,MessageTypeToString(header.messageType)));
                if (currentState == State::EXCHANGING_STATS) {
                    Logger::log("Info: Client received STATS_ACK. Finalizing test.");
                    // Parse and store server stats
                    try {
                        using json = nlohmann::json;
                        std::string stats_str(payload.begin(), payload.end());
                        json stats_payload = json::parse(stats_str);
                        m_remoteStats = stats_payload.get<TestStats>(); // Use from_json implicitly
                        Logger::log("Info: Server Stats: " + stats_payload.dump());
                    } catch (const std::exception& e) {
                        Logger::log("Warning: Could not parse server stats: " + std::string(e.what()));
                    }
                    transitionTo_nolock(State::FINISHED);
                }
                break;
            default:
                Logger::log("Warning: Client received an unexpected message type: " + std::to_string((int)header.messageType));
                DebugPause(string_format("[%s:%d] MessageType::%s",  __FUNCTION__, __LINE__,MessageTypeToString(header.messageType)));
                break;
        }
    }
}

void TestController::onTestCompleted() {
    Logger::log("Info: Data transmission completed.");
    transitionTo(State::EXCHANGING_STATS);
}

void TestController::sendClientStatsAndAwaitAck() {
    using json = nlohmann::json;
    TestStats clientStats = packetGenerator->getStats();
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

    networkInterface->asyncSend(packet, [this](size_t bytesSent) {
        if (bytesSent > 0) {
            Logger::log("Info: Client sent STATS_EXCHANGE successfully.");
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


#include "TestController.h"
#include "NetworkAwaiters.h" // Awaiters for network operations
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
        case TestController::State::WAITING_FOR_SHUTDOWN_ACK: return "WAITING_FOR_SHUTDOWN_ACK";
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
        case MessageType::SHUTDOWN_ACK: return "SHUTDOWN_ACK";
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
    cancelHandshakeWatchdog();
}

/**
 * @brief Resets all member variables to their initial state for a new test.
 */
void TestController::reset() {
    cancelHandshakeWatchdog();
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
    
    {
        std::lock_guard<std::mutex> lock(m_awaiterMutex);
        m_waitingCoroutines.clear();
        m_messageBuffer.clear(); // Clear buffered messages
    }
    m_generatorCompletionAwaiter = nullptr;
    m_receiverCompletionAwaiter = nullptr;

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

    // Start the main coroutine
    mainTestTask = runTestCoroutine();
    mainTestTask.start();
}


/**
 * @brief Stops the current test and closes network resources.
 */
void TestController::stopTest() {
    Logger::log("Debug: TestController::stopTest() called.");
    cancelHandshakeWatchdog();
    if (m_stopped.exchange(true)) {
        Logger::log("Debug: TestController::stopTest() already stopped, returning.");
        return; // Already stopped
    }
    Logger::log("Info: Stopping the test components.");

    // Resume any waiting coroutines to let them exit/fail
    {
        std::lock_guard<std::mutex> lock(m_awaiterMutex);
        for (auto& [type, awaiter] : m_waitingCoroutines) {
             if (awaiter && awaiter->handle && !awaiter->handle.done()) {
                 awaiter->cancelled = true;
                 awaiter->handle.resume(); // Resume them, they should check running state
             }
        }
        m_waitingCoroutines.clear();
    }

    if (m_generatorCompletionAwaiter) m_generatorCompletionAwaiter->resume();
    if (m_receiverCompletionAwaiter) m_receiverCompletionAwaiter->resume();

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
    // Legacy switch statement removed - logic is now in runTestCoroutine
}

/**
 * @brief Handles incoming packets from the PacketReceiver.
 * @param header The packet header.
 * @param payload The packet payload.
 */
void TestController::onPacket(const PacketHeader& header, const std::vector<char>& payload) {
    if (header.messageType == MessageType::DATA_PACKET) {
        return; // Data packets are handled by PacketReceiver stats, ignored here
    }

    std::lock_guard<std::mutex> lock(m_awaiterMutex);
    auto it = m_waitingCoroutines.find(header.messageType);
    if (it != m_waitingCoroutines.end() && it->second) {
        // Found a coroutine waiting for this message type
        it->second->header = header;
        it->second->payload = payload;
        it->second->completed = true;
        if (it->second->handle && !it->second->handle.done()) {
            it->second->handle.resume();
        }
        // Remove from map, the awaiter is now complete
        m_waitingCoroutines.erase(it);
    } else {
        // No coroutine waiting yet, buffer it!
        // Logger::log("Debug: Buffering control packet: " + std::string(MessageTypeToString(header.messageType)));
        m_messageBuffer[header.messageType].push({header, payload});
    }
}

/**
 * @brief Callback for when the PacketGenerator has completed its sending duration.
 * This function is called on the client side when the test duration is over.
 */
void TestController::onTestCompleted() {
    Logger::log("Info: Data transmission completed.");
    if (m_generatorCompletionAwaiter) {
        m_generatorCompletionAwaiter->completed = true;
        m_generatorCompletionAwaiter->resume();
    }
}

void TestController::startHandshakeWatchdog() {
    // Replaced by coroutine logic or can remain as auxiliary safety
}

void TestController::cancelHandshakeWatchdog() {
    if (m_handshakeWatchdog.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_handshakeWatchdogMutex);
            m_handshakeWatchdogCancel = true;
        }
        m_handshakeWatchdogCv.notify_all();
        if (std::this_thread::get_id() == m_handshakeWatchdog.get_id()) {
            m_handshakeWatchdog.detach();
        } else {
            m_handshakeWatchdog.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_handshakeWatchdogMutex);
        m_handshakeWatchdogCancel = false;
    }
    m_handshakeWatchdogArmed = false;
}

void TestController::cancelTimer() {
    // No-op placeholder for now
}

// --- Coroutine Implementation ---

auto TestController::waitForMessage(MessageType type, int timeoutMs) {
    struct Awaiter : public MessageAwaiter {
        TestController* controller;
        int timeout;
        std::thread timerThread;
        std::atomic<bool> cancelledTimer{false};

        Awaiter(TestController* c, MessageType t, int to) : controller(c), timeout(to) {
            this->type = t;
        }

        ~Awaiter() {
            cancelledTimer = true;
            if (timerThread.joinable()) timerThread.join();
        }

        bool await_ready() {
            // Check buffer first!
            std::lock_guard<std::mutex> lock(controller->m_awaiterMutex);
            auto& queue = controller->m_messageBuffer[type];
            if (!queue.empty()) {
                auto& msg = queue.front();
                this->header = msg.header;
                this->payload = msg.payload;
                this->completed = true;
                queue.pop();
                return true; // Resume immediately
            }
            return false;
        }

        void await_suspend(std::coroutine_handle<> h) {
            this->handle = h;
            {
                std::lock_guard<std::mutex> lock(controller->m_awaiterMutex);
                controller->m_waitingCoroutines[type] = this;
            }

            // Start timeout timer
            if (timeout > 0) {
                 // Capture local copy of type to avoid issues if 'this' is accessed after destruction (though thread should join first)
                 MessageType capturedType = this->type;
                 timerThread = std::thread([this, h, capturedType]() {
                     // Non-blocking wait loop
                     auto start = std::chrono::steady_clock::now();
                     while (!cancelledTimer) {
                         auto now = std::chrono::steady_clock::now();
                         if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= timeout) {
                             // Timeout occurred
                             std::lock_guard<std::mutex> lock(controller->m_awaiterMutex);
                             // If still in map, it means not yet received
                             auto it = controller->m_waitingCoroutines.find(capturedType);
                             if (it != controller->m_waitingCoroutines.end() && it->second == this) {
                                 this->timedOut = true;
                                 this->completed = true; // Mark as done (via timeout)
                                 controller->m_waitingCoroutines.erase(it);
                                 if (!h.done()) h.resume();
                             }
                             break;
                         }
                         std::this_thread::sleep_for(std::chrono::milliseconds(10));
                     }
                 });
            }
        }

        std::pair<PacketHeader, std::vector<char>> await_resume() {
             if (cancelled) {
                 throw std::runtime_error("Operation cancelled.");
             }
             if (timedOut) {
                 throw std::runtime_error("Timeout waiting for message: " + std::string(MessageTypeToString(type)));
             }
             return {header, payload};
        }
    };
    return Awaiter(this, type, timeoutMs);
}

auto TestController::waitForGenerator() {
    struct Awaiter : public CompletionAwaiter {
        TestController* controller;
        Awaiter(TestController* c) : controller(c) {}
        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> h) {
            this->handle = h;
            controller->m_generatorCompletionAwaiter = this;
        }
        void await_resume() {
            controller->m_generatorCompletionAwaiter = nullptr;
        }
    };
    return Awaiter(this);
}

// Simple helper to send a control packet
Task sendControlPacket(NetworkInterface* net, MessageType type, const std::vector<char>& payload = {}) {
    PacketHeader header{};
    header.startCode = PROTOCOL_START_CODE;
    header.messageType = type;
    header.payloadSize = static_cast<uint32_t>(payload.size());
    header.checksum = calculateChecksum(payload.data(), payload.size()); // Assuming calculateChecksum handles empty data gracefully or we pass nullptr

    std::vector<char> packet(sizeof(PacketHeader) + payload.size());
    std::memcpy(packet.data(), &header, sizeof(PacketHeader));
    if (!payload.empty()) {
        std::memcpy(packet.data() + sizeof(PacketHeader), payload.data(), payload.size());
    }

    co_await co_send(net, packet);
}

Task TestController::runTestCoroutine() {
    Logger::log("Coroutine: Starting test coroutine.");

    try {
        if (currentConfig.getMode() == Config::TestMode::CLIENT) {
            co_await runClientLogic();
        } else {
            co_await runServerLogic();
        }
    } catch (const std::exception& e) {
        Logger::log(std::string("Coroutine Error: ") + e.what());
        transitionTo(State::ERRORED);
    }

    // Cleanup and Signal Finish
    if (!testCompletionPromise_set) {
        testCompletionPromise.set_value();
        testCompletionPromise_set = true;
    }
    {
        std::lock_guard<std::mutex> lock(m_cliBlockMutex);
        m_cliBlockFlag = true;
    }
    m_cliBlockCv.notify_all();

    Logger::log("Coroutine: Test coroutine finished.");
}

Task TestController::runClientLogic() {
    Logger::log("Coroutine: Running Client Logic");
    auto* net = networkInterface.get();

    // 1. Connect
    transitionTo(State::CONNECTING);
    if (!net->initialize("0.0.0.0", 0)) { // Bind client
        Logger::log("Error: Client init failed");
        transitionTo(State::ERRORED);
        co_return;
    }

    bool connected = co_await co_connect(net, currentConfig.getTargetIP(), currentConfig.getPort());
    if (!connected) {
        Logger::log("Error: Failed to connect to server");
        transitionTo(State::ERRORED);
        co_return;
    }
    Logger::log("Info: Client connected.");

    // Start Receiver (for control packets)
    packetReceiver->start([this](const PacketHeader& h, const std::vector<char>& p) {
        onPacket(h, p);
    }, [this]() {
        Logger::log("Info: Client receiver stopped (server disconnected).");
    });

    // 2. Send Config (Handshake)
    transitionTo(State::SENDING_CONFIG);
    std::string configStr = currentConfig.toJson().dump();
    std::vector<char> configData(configStr.begin(), configStr.end());

    // Explicit send to manage transition properly
    co_await sendControlPacket(net, MessageType::CONFIG_HANDSHAKE, configData);

    // 3. Wait for ACK
    transitionTo(State::WAITING_FOR_ACK);
    // Timeout set to configured value
    auto [ackHeader, ackPayload] = co_await waitForMessage(MessageType::CONFIG_ACK, currentConfig.getHandshakeTimeoutMs());
    Logger::log("Info: Received CONFIG_ACK.");

    // 4. Run Test (Client -> Server)
    transitionTo(State::RUNNING_TEST);
    m_testStartTime = std::chrono::steady_clock::now();
    packetGenerator->start(currentConfig, [this]() {
        onTestCompleted();
    });

    // Wait for generator to finish
    co_await waitForGenerator();
    Logger::log("Info: Client generator finished.");

    // 5. Send FIN
    transitionTo(State::FINISHING);
    co_await sendControlPacket(net, MessageType::TEST_FIN);
    Logger::log("Info: Sent TEST_FIN.");

    // 6. Exchange Stats (Phase 1)
    transitionTo(State::EXCHANGING_STATS);

    auto [finHeader, finPayload] = co_await waitForMessage(MessageType::TEST_FIN);
    Logger::log("Info: Received TEST_FIN from server.");

    transitionTo(State::EXCHANGING_STATS);

    // Send Stats
    TestStats clientStats = packetGenerator->getStats();
    packetGenerator->saveLastStats(clientStats);
    std::string statsStr = nlohmann::json(clientStats).dump();
    std::vector<char> statsData(statsStr.begin(), statsStr.end());

    co_await sendControlPacket(net, MessageType::STATS_EXCHANGE, statsData);
    Logger::log("Info: Sent STATS_EXCHANGE.");

    // Wait for STATS_ACK (containing server stats)
    auto [statsAckHeader, statsAckPayload] = co_await waitForMessage(MessageType::STATS_ACK);
    Logger::log("Info: Received STATS_ACK.");

    m_serverStatsPhase1 = parseStats(statsAckPayload).get<TestStats>();
    m_clientStatsPhase1 = packetGenerator->lastStats();

    Logger::log("--- Test Phase 1 Summary ---");
    Logger::log("Client-side (sent):" + formatStatsForLogging(m_clientStatsPhase1));
    Logger::log("Server-side (received):" + formatStatsForLogging(m_serverStatsPhase1));
    Logger::log("----------------------------");

    // 7. Prepare for Phase 2 (Server -> Client)
    // Send CLIENT_READY
    co_await sendControlPacket(net, MessageType::CLIENT_READY);
    transitionTo(State::WAITING_FOR_SERVER_FIN);

    // Reset receiver stats for Phase 2
    packetReceiver->resetStats();

    // 8. Wait for Server to Finish Phase 2
    auto [serverFinHeader, serverFinPayload] = co_await waitForMessage(MessageType::TEST_FIN);
    Logger::log("Info: Received TEST_FIN for Phase 2.");

    // 9. Exchange Stats (Phase 2)
    transitionTo(State::EXCHANGING_SERVER_STATS);
    TestStats clientReceiverStats = packetReceiver->getStats();
    m_clientStatsPhase2 = clientReceiverStats;

    std::string stats2Str = nlohmann::json(clientReceiverStats).dump();
    std::vector<char> stats2Data(stats2Str.begin(), stats2Str.end());

    co_await sendControlPacket(net, MessageType::STATS_EXCHANGE, stats2Data);

    // Wait for final STATS_ACK
    auto [finalAckHeader, finalAckPayload] = co_await waitForMessage(MessageType::STATS_ACK);
    m_serverStatsPhase2 = parseStats(finalAckPayload).get<TestStats>();

    Logger::log("--- Test Phase 2 Summary ---");
    Logger::log("Server-side (sent):" + formatStatsForLogging(m_serverStatsPhase2));
    Logger::log("Client-side (received):" + formatStatsForLogging(m_clientStatsPhase2));
    Logger::log("----------------------------");

    // 10. Send Shutdown ACK
    co_await sendControlPacket(net, MessageType::SHUTDOWN_ACK);

    transitionTo(State::FINISHED);
}

Task TestController::runServerLogic() {
    Logger::log("Coroutine: Running Server Logic");
    auto* net = networkInterface.get();

    // 1. Initialize and Accept
    if (!net->initialize(currentConfig.getTargetIP(), currentConfig.getPort())) {
         Logger::log("Error: Server init failed");
         transitionTo(State::ERRORED);
         co_return;
    }

    // Setup listening socket (Windows specific logic handled inside impl if needed, but here we invoke setup via initialize or setupListeningSocket if needed)
    #ifdef _WIN32
    WinIOCPNetworkInterface* winInterface = static_cast<WinIOCPNetworkInterface*>(networkInterface.get());
    if (!winInterface->setupListeningSocket(currentConfig.getTargetIP(), currentConfig.getPort())) {
        Logger::log("Error: Failed to setup listening socket");
        transitionTo(State::ERRORED);
        co_return;
    }
    #endif

    transitionTo(State::ACCEPTING);
    auto acceptRes = co_await co_accept(net);
    if (!acceptRes.success) {
        Logger::log("Error: Accept failed");
        transitionTo(State::ERRORED);
        co_return;
    }
    Logger::log("Info: Client connected from " + acceptRes.clientIP);

    // Start Receiver
    packetReceiver->start([this](const PacketHeader& h, const std::vector<char>& p) {
        onPacket(h, p);
    }, [this]() {
        Logger::log("Info: Server receiver stopped.");
    });

    // 2. Wait for Config
    transitionTo(State::WAITING_FOR_CONFIG);
    auto [configHeader, configPayload] = co_await waitForMessage(MessageType::CONFIG_HANDSHAKE);

    std::string configStr(configPayload.begin(), configPayload.end());
    Config receivedConfig = Config::fromJson(nlohmann::json::parse(configStr));
    receivedConfig.setMode(Config::TestMode::SERVER);
    this->currentConfig = receivedConfig;
    Logger::log("Info: Received Config.");

    // 3. Send ACK
    co_await sendControlPacket(net, MessageType::CONFIG_ACK);
    Logger::log("Info: Sent CONFIG_ACK.");

    // 4. Run Test (Client -> Server)
    transitionTo(State::RUNNING_TEST);
    packetReceiver->resetStats();

    // Wait for Client to send FIN
    auto [finHeader, finPayload] = co_await waitForMessage(MessageType::TEST_FIN);
    Logger::log("Info: Received TEST_FIN.");

    // 5. Send FIN
    transitionTo(State::FINISHING);
    co_await sendControlPacket(net, MessageType::TEST_FIN);

    // 6. Wait for Stats (Phase 1)
    auto [statsHeader, statsPayload] = co_await waitForMessage(MessageType::STATS_EXCHANGE);
    m_clientStatsPhase1 = parseStats(statsPayload).get<TestStats>();
    m_serverStatsPhase1 = packetReceiver->getStats();

    Logger::log("--- Test Phase 1 Summary ---");
    Logger::log("Client-side (sent):" + formatStatsForLogging(m_clientStatsPhase1));
    Logger::log("Server-side (received):" + formatStatsForLogging(m_serverStatsPhase1));
    Logger::log("----------------------------");

    // Send ACK with server stats
    nlohmann::json ackJson = m_serverStatsPhase1;
    std::string ackStr = ackJson.dump();
    std::vector<char> ackData(ackStr.begin(), ackStr.end());

    transitionTo(State::WAITING_FOR_CLIENT_READY);
    co_await sendControlPacket(net, MessageType::STATS_ACK, ackData);

    // 7. Wait for Client Ready for Phase 2
    auto [readyHeader, readyPayload] = co_await waitForMessage(MessageType::CLIENT_READY);
    Logger::log("Info: Received CLIENT_READY. Starting Phase 2.");

    // 8. Run Test Phase 2 (Server -> Client)
    transitionTo(State::RUNNING_SERVER_TEST);
    packetGenerator->resetStats();
    packetGenerator->start(currentConfig, [this]() {
        onTestCompleted();
    });

    co_await waitForGenerator();
    Logger::log("Info: Server generator finished.");

    // 9. Send FIN (Phase 2)
    transitionTo(State::SERVER_TEST_FINISHING);
    co_await sendControlPacket(net, MessageType::TEST_FIN);

    // 10. Wait for Stats (Phase 2)
    auto [stats2Header, stats2Payload] = co_await waitForMessage(MessageType::STATS_EXCHANGE);
    m_clientStatsPhase2 = parseStats(stats2Payload).get<TestStats>();
    m_serverStatsPhase2 = packetGenerator->getStats();

    Logger::log("--- Test Phase 2 Summary ---");
    Logger::log("Server-side (sent):" + formatStatsForLogging(m_serverStatsPhase2));
    Logger::log("Client-side (received):" + formatStatsForLogging(m_clientStatsPhase2));
    Logger::log("----------------------------");

    // Send Final ACK with stats
    nlohmann::json finalAckJson = m_serverStatsPhase2;
    std::string finalAckStr = finalAckJson.dump();
    std::vector<char> finalAckData(finalAckStr.begin(), finalAckStr.end());

    transitionTo(State::WAITING_FOR_SHUTDOWN_ACK);
    co_await sendControlPacket(net, MessageType::STATS_ACK, finalAckData);

    // 11. Wait for Shutdown ACK
    auto [shutdownHeader, shutdownPayload] = co_await waitForMessage(MessageType::SHUTDOWN_ACK);
    Logger::log("Info: Received SHUTDOWN_ACK.");

    transitionTo(State::FINISHED);
}

#pragma once

#include "Config.h"
#include "NetworkInterface.h"
#include "PacketGenerator.h"
#include "PacketReceiver.h"
#include "Logger.h"
#include <memory>
#include <chrono>
#include <future>
#include <atomic>
#include <mutex>
#include <string>

/**
 * @class TestController
 * @brief Manages the overall test lifecycle, state machine, and coordination of components.
 *
 * This class acts as the central coordinator for the network test. It manages the
 * state of the test (e.g., connecting, running, finished), initializes the network
 * interface, packet generator, and receiver, and handles the flow of the test
 * from start to finish.
 */
class TestController {
public:
    /**
     * @enum State
     * @brief Defines the states in the test lifecycle.
     */
    enum class State {
        IDLE,               // Initial state before any test is started.
        INITIALIZING,       // Setting up resources for a new test.
        // Client-specific states
        CONNECTING,         // Client is attempting to connect to the server.
        SENDING_CONFIG,     // Client is sending its configuration to the server.
        WAITING_FOR_ACK,    // Client is waiting for the server's acknowledgment.
        // Server-specific states
        ACCEPTING,          // Server is waiting for a client to connect.
        WAITING_FOR_CONFIG, // Server is waiting for the client's configuration.
        // Common states for both client and server
        RUNNING_TEST,       // The data transfer phase of the test is active.
        EXCHANGING_STATS,   // Exchanging final statistics
        FINISHED,           // The test has completed successfully.
        ERRORED             // An unrecoverable error occurred.
    };

    TestController();
    ~TestController();

    /**
     * @brief Starts a new test with the given configuration.
     * @param config The configuration for the test.
     */
    void startTest(const Config& config);

    /**
     * @brief Stops the currently running test.
     */
    void stopTest();

    /**
     * @brief Gets a future that will be ready when the test is complete.
     * This is used by the main thread to wait for the test to finish.
     * @return A std::future<void> object.
     */
    std::future<void> getTestCompletionFuture() { return testCompletionPromise.get_future(); }

private:
    friend class CLIHandler;
    // --- Core Components ---
    std::unique_ptr<NetworkInterface> networkInterface;
    std::unique_ptr<PacketGenerator> packetGenerator;
    std::unique_ptr<PacketReceiver> packetReceiver;
    
    // --- State Machine and Synchronization ---
    std::atomic<State> currentState;        // The current state of the test.
    std::mutex m_stateMachineMutex;         // Mutex to protect the state machine logic.
    Config currentConfig;                   // The configuration for the current test.
    std::promise<void> testCompletionPromise; // Promise to signal test completion.
    std::atomic<bool> testCompletionPromise_set; // Flag to ensure set_value is called only once.
    uint32_t m_expectedDataPacketCounter;   // Counter for validating packet sequence.
    std::atomic<long long> m_contentMismatchCount{0}; // Payload content mismatches counted on server side.
    std::chrono::steady_clock::time_point m_testStartTime; // Timestamp when RUNNING_TEST started
    TestStats m_remoteStats; // Added for remote stats

    std::mutex m_cliBlockMutex;
    std::condition_variable m_cliBlockCv;
    std::atomic<bool> m_cliBlockFlag; // Set to true when test is finished/errored
    std::atomic<bool> m_stopped; // Flag to ensure stopTest is only called once.

    

    /**
     * @brief Callback for handling packets received from the PacketReceiver.
     * @param header The header of the received packet.
     * @param payload The payload of the received packet.
     */
    void onPacket(const PacketHeader& header, const std::vector<char>& payload);

    /**
     * @brief Callback for when the PacketGenerator has completed its sending duration.
     */
    void onTestCompleted();

    void sendClientStatsAndAwaitAck();

    /**
     * @brief Transitions the internal state machine to a new state.
     * This function is thread-safe and acquires a lock.
     */
    void transitionTo(State newState);

    /**
     * @brief The actual implementation of the state transition.
     * This function is NOT thread-safe and must be called only when the state machine mutex is already held.
     */
    void transitionTo_nolock(State newState);

    /**
     * @brief Cancels any outstanding state timers (no-op placeholder).
     */
    void cancelTimer();
};
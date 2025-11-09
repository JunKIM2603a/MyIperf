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
        FINISHING,          // Handshake to confirm test completion before exchanging stats.
        EXCHANGING_STATS,   // Exchanging final statistics

        // States for Server-to-Client Test
        WAITING_FOR_CLIENT_READY, // Server waits for client ready signal after first test
        RUNNING_SERVER_TEST,      // Server is sending data to the client
        WAITING_FOR_SERVER_FIN,   // Client is waiting for server to finish
        SERVER_TEST_FINISHING,    // Server-to-client test is wrapping up
        EXCHANGING_SERVER_STATS,  // Final stats exchange initiated by server
        WAITING_FOR_SHUTDOWN_ACK, // Server waits for client's final ACK before closing

        FINISHED,           // The test has completed successfully.
        ERRORED             // An unrecoverable error occurred.
    };

    /**
     * @brief Constructs a TestController object.
     */
    TestController();

    /**
     * @brief Destroys the TestController object.
     */
    ~TestController();

    nlohmann::json parseStats(const std::vector<char>& payload) const;

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
    /** @brief The network interface for sending and receiving packets. */
    std::unique_ptr<NetworkInterface> networkInterface;
    /** @brief The packet generator for creating packets to be sent. */
    std::unique_ptr<PacketGenerator> packetGenerator;
    /** @brief The packet receiver for processing incoming packets. */
    std::unique_ptr<PacketReceiver> packetReceiver;
    
    // --- State Machine and Synchronization ---
    /** @brief The current state of the test. */
    std::atomic<State> currentState;
    /** @brief Mutex to protect the state machine logic. */
    std::mutex m_stateMachineMutex;
    /** @brief The configuration for the current test. */
    Config currentConfig;
    /** @brief Promise to signal test completion to the main thread. */
    std::promise<void> testCompletionPromise;
    /** @brief Flag to ensure the test completion promise is set only once. */
    std::atomic<bool> testCompletionPromise_set;
    /** @brief Counter for validating the sequence of received data packets. */
    uint32_t m_expectedDataPacketCounter;
    /** @brief Counter for payload content mismatches, used on the server side. */
    std::atomic<long long> m_contentMismatchCount{0};
    /** @brief Timestamp marking the start of the RUNNING_TEST state. */
    std::chrono::steady_clock::time_point m_testStartTime;
    /** @brief Stores statistics received from the remote peer. */
    TestStats m_remoteStats;

    // --- Statistics Storage for Final Report ---
    TestStats m_clientStatsPhase1;
    TestStats m_serverStatsPhase1;
    TestStats m_clientStatsPhase2;
    TestStats m_serverStatsPhase2;

    // --- CLI Synchronization ---
    /** @brief Mutex for blocking the CLI thread while a test is running. */
    std::mutex m_cliBlockMutex;
    /** @brief Condition variable to signal the CLI thread to unblock. */
    std::condition_variable m_cliBlockCv;
    /** @brief Flag to indicate that the CLI thread should unblock. */
    std::atomic<bool> m_cliBlockFlag;
    /** @brief Flag to ensure stopTest() logic is executed only once. */
    std::atomic<bool> m_stopped;

    

    /**
     * @brief Resets the controller to a clean state for a new test.
     */
    void reset();

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

    /**
     * @brief Sends client-side statistics to the server and waits for acknowledgment.
     *
     * This function is called on the client side after the main data transfer is complete.
     * It sends a final statistics packet to the server and then waits for a corresponding
     * acknowledgment from the server to ensure the stats were received.
     */
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
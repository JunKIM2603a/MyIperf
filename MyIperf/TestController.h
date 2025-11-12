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
        SENDING_FINAL_ACK,  // Client is sending the final handshake acknowledgment.
        // Server-specific states
        ACCEPTING,          // Server is waiting for a client to connect.
        WAITING_FOR_CONFIG, // Server is waiting for the client's configuration.
        WAITING_FOR_FINAL_ACK, // Server is waiting for the client's final acknowledgment.
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
        SENDING_SHUTDOWN_ACK, // Client sends the final shutdown acknowledgment

        WAITING_FOR_REPLY,  // Waiting for a reply to a message, with retry logic

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
     * @brief Periodically called to check for timeouts in the state machine.
    */
    void update();

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

    // --- Handshake Retry and Timeout ---
    /** @brief Counter for handshake message retries. */
    int retryCount = 0;
    /** @brief Timestamp of the last handshake packet sent to check for timeouts. */
    std::chrono::steady_clock::time_point lastPacketSentTime;
    /** @brief Timestamp for when the server enters an initial waiting state. */
    std::chrono::steady_clock::time_point m_initialStateTime;
    /** @brief Maximum number of times to retry sending a handshake message. */
    static const int MAX_RETRIES = 5;
    /** @brief Timeout duration for waiting for a handshake reply. */
    static const std::chrono::seconds HANDSHAKE_TIMEOUT;
    /** @brief Timeout for server initial states (ACCEPTING, WAITING_FOR_CONFIG). */
    static const std::chrono::seconds INITIAL_TIMEOUT;

    // --- Packet and Statistics Tracking ---
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

    // --- Retry and Timeout Mechanism ---
    int m_maxRetries = 3; // Maximum number of retries
    std::chrono::seconds m_retryDelay{5}; // Delay between retries
    int m_retryCount = 0; // Current retry count

    std::thread m_timerThread; // Thread for handling timeouts
    std::mutex m_timerMutex;
    std::condition_variable m_timerCv;
    bool m_stopTimer = false;

    std::vector<char> m_lastPacket; // The last packet sent, for retries
    State m_nextState; // The state to transition to upon successful acknowledgment
    MessageType m_expectedReply; // The expected message type for the reply

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
     * @brief Sends a packet and sets up for a retry if no acknowledgment is received.
     * @param packet The packet to send.
     * @param nextState The state to transition to on success.
     * @param expectedReply The expected reply message type.
     */
    void sendMessageWithRetry(const std::vector<char>& packet, State nextState, MessageType expectedReply);

    /**
     * @brief The timer function that handles timeouts and retries.
     */
    void handleTimeout();

    /**
     * @brief Starts the timeout timer.
     */
    void startTimer();

    /**
     * @brief Stops the timeout timer.
     */
    void stopTimer();

    /**
     * @brief Cancels any outstanding state timers (no-op placeholder).
     */
    void cancelTimer();
};
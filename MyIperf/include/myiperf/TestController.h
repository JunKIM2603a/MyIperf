#pragma once

#include "myiperf/Config.h"
#include "myiperf/CoroutineSupport.h"
#include "myiperf/Protocol.h"
#include "myiperf/RunOptions.h"
#include "myiperf/TestRunResult.h"

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class ControlChannel;
class ControlMessageBus;
class NetworkInterface;
class PacketGenerator;
class PacketReceiver;
class ResultEventSink;

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
    void startTest(const Config& config, const RunOptions& options);
    std::future<TestRunResult> runTestAsync(const Config& config, const RunOptions& options = RunOptions{});
    TestRunResult getLastResult() const;

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

	    State getFinalState() const { return currentState.load(); }
	    bool hasError() const { return currentState.load() == State::ERRORED; }
	    bool completedSuccessfully() const;

private:
    friend class CLIHandler;
    // --- Core Components ---
    /** @brief The network interface for sending and receiving packets. */
    std::unique_ptr<NetworkInterface> networkInterface;
    /** @brief The packet generator for creating packets to be sent. */
    std::unique_ptr<PacketGenerator> packetGenerator;
    /** @brief The packet receiver for processing incoming packets. */
    std::unique_ptr<PacketReceiver> packetReceiver;
    /** @brief Routes non-data protocol messages to waiting coroutines. */
    std::unique_ptr<ControlMessageBus> controlMessages;
    /** @brief User-facing control protocol API for sessions. */
    std::unique_ptr<ControlChannel> controlChannel;
    
    // --- State Management ---
    /** @brief The current state of the test. */
    std::atomic<State> currentState;
    /** @brief Mutex to protect the state machine logic. */
    std::mutex m_stateMachineMutex;
    /** @brief The configuration for the current test. */
    Config currentConfig;
    RunOptions currentRunOptions;
    /** @brief Promise to signal test completion to the main thread. */
    std::promise<void> testCompletionPromise;
    /** @brief Flag to ensure the test completion promise is set only once. */
    std::atomic<bool> testCompletionPromise_set;

    // --- Statistics Storage for Final Report ---
    TestStats m_clientStatsPhase1;
    TestStats m_serverStatsPhase1;
    TestStats m_clientStatsPhase2;
    TestStats m_serverStatsPhase2;

    mutable std::mutex m_resultMutex;
    TestRunResult m_lastResult;
    std::unique_ptr<ResultEventSink> resultEventSink;
    std::atomic<bool> m_resultFinalized;
    std::atomic<bool> m_testStarted;
    std::atomic<bool> m_phase1EventPublished;
    std::atomic<bool> m_phase2EventPublished;
    std::string m_startedAt;
    std::string m_resultExportWarning;

    // --- CLI Synchronization ---
    /** @brief Mutex for blocking the CLI thread while a test is running. */
    std::mutex m_cliBlockMutex;
    /** @brief Condition variable to signal the CLI thread to unblock. */
    std::condition_variable m_cliBlockCv;
    /** @brief Flag to indicate that the CLI thread should unblock. */
    std::atomic<bool> m_cliBlockFlag;
    /** @brief Flag to ensure stopTest() logic is executed only once. */
    std::atomic<bool> m_stopped;

    // --- Coroutine Support ---
    Task mainTestTask{nullptr}; // Holds the handle to the main coroutine

    /**
     * @brief Resets the controller to a clean state for a new test.
     */
    void reset();

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

    Task runTestCoroutine();
    void signalCompletion();
    void finalizeResultOnce(const std::string& failureReason = "");
    TestRunResult buildCurrentResult(const std::string& failureReason) const;
    std::string exportResult(const TestRunResult& result);
    void publishRunStarted();
    void publishPhaseResult(int phaseNumber);
};

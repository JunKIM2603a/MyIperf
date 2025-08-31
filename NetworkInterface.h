// NetworkInterface.h
#pragma once

#include <string>
#include <vector>
#include <functional>

// --- Callback Type Definitions for Asynchronous Operations ---

/**
 * @brief Callback for when data is received.
 * @param data The buffer containing the received data.
 * @param bytesReceived The number of bytes actually received.
 */
using RecvCallback = std::function<void(const std::vector<char>& data, size_t bytesReceived)>;

/**
 * @brief Callback for when data has been sent.
 * @param bytesSent The number of bytes successfully sent.
 */
using SendCallback = std::function<void(size_t bytesSent)>;

/**
 * @brief Callback for when a connection attempt is completed.
 * @param success True if the connection was successful, false otherwise.
 */
using ConnectCallback = std::function<void(bool success)>;

/**
 * @brief Callback for when a new client connection is accepted by a server.
 * @param success True if a client was accepted, false on error.
 * @param clientIP The IP address of the connected client.
 * @param clientPort The port of the connected client.
 */
using AcceptCallback = std::function<void(bool success, const std::string& clientIP, int clientPort)>;

/**
 * @class NetworkInterface
 * @brief An abstract base class defining the interface for network operations.
 *
 * This class provides a pure virtual interface for platform-specific network
 * implementations (e.g., WinIOCP, Linux epoll). It defines a set of asynchronous
 * and blocking methods for network communication, ensuring that the core application
 * logic remains independent of the underlying network API.
 */
class NetworkInterface {
public:
    virtual ~NetworkInterface() = default;

    // --- Core Network Management ---

    /**
     * @brief Initializes the network interface.
     * @param ip The IP address to bind to (for servers) or connect to (for clients).
     * @param port The port number.
     * @return True if initialization is successful, false otherwise.
     */
    virtual bool initialize(const std::string& ip, int port) = 0;

    /**
     * @brief Closes the network connection and cleans up resources.
     */
    virtual void close() = 0;

    // --- Asynchronous Operations ---

    /**
     * @brief Asynchronously connects to a server.
     * @param ip The IP address of the server.
     * @param port The port of the server.
     * @param callback The function to call upon completion.
     */
    virtual void asyncConnect(const std::string& ip, int port, ConnectCallback callback) = 0;

    /**
     * @brief Asynchronously accepts a new client connection.
     * @param callback The function to call upon completion.
     */
    virtual void asyncAccept(AcceptCallback callback) = 0;

    /**
     * @brief Asynchronously sends data over the network.
     * @param data The vector of characters to send.
     * @param callback The function to call upon completion.
     */
    virtual void asyncSend(const std::vector<char>& data, SendCallback callback) = 0;

    /**
     * @brief Asynchronously receives data from the network.
     * @param bufferSize The maximum number of bytes to receive.
     * @param callback The function to call upon completion.
     */
    virtual void asyncReceive(size_t bufferSize, RecvCallback callback) = 0;

    // --- Blocking Operations (for simple, non-performance-critical tasks) ---

    /**
     * @brief Sends data and blocks until the operation is complete.
     * @param data The vector of characters to send.
     * @return The number of bytes sent, or -1 on error.
     */
    virtual int blockingSend(const std::vector<char>& data) = 0;

    /**
     * @brief Receives data and blocks until data is available.
     * @param bufferSize The maximum number of bytes to receive.
     * @return A vector containing the received data.
     */
    virtual std::vector<char> blockingReceive(size_t bufferSize) = 0;
};
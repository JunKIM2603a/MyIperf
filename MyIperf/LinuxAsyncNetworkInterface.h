// LinuxAsyncNetworkInterface.h
#pragma once

#ifndef _WIN32 // Guard for Linux-only compilation
#include "NetworkInterface.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <vector>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include <functional>

/**
 * @enum LinuxOperationType
 * @brief Defines the type of asynchronous I/O operation for epoll.
 */
enum class LinuxOperationType {
    Recv,    /**< A receive operation. */
    Send,    /**< A send operation. */
    Accept,  /**< An accept operation (server-side). */
    Connect  /**< A connect operation (client-side). */
};

/**
 * @struct SocketData
 * @brief Contains all necessary data associated with a file descriptor being monitored by epoll.
 */
struct SocketData {
    /**< The file descriptor. */
    int fd;
    /**< The current operation associated with this FD. */
    LinuxOperationType operationType;
    /**< Buffer for receive operations. */
    std::vector<char> buffer;
    /**< Buffer for data to be sent. */
    std::vector<char> sendData;
    
    // Callbacks for asynchronous operations
    /**< Callback function to be invoked upon completion of a receive operation. */
    RecvCallback recvCallback;
    /**< Callback function to be invoked upon completion of a send operation. */
    SendCallback sendCallback;
    /**< Callback function to be invoked upon completion of a connect operation. */
    ConnectCallback connectCallback;
    /**< Callback function to be invoked upon completion of an accept operation. */
    AcceptCallback acceptCallback;
};

/**
 * @class LinuxAsyncNetworkInterface
 * @brief A Linux-specific implementation of the NetworkInterface using epoll.
 *
 * This class provides a high-performance, scalable network backend for Linux platforms.
 * It uses epoll to handle multiple asynchronous I/O events efficiently with a single worker thread.
 */
class LinuxAsyncNetworkInterface : public NetworkInterface {
public:
    /**
     * @brief Constructs a LinuxAsyncNetworkInterface object.
     */
    LinuxAsyncNetworkInterface();

    /**
     * @brief Destroys the LinuxAsyncNetworkInterface object, ensuring all resources are released.
     */
    ~LinuxAsyncNetworkInterface();

    // --- Overridden NetworkInterface methods ---
    /**
     * @brief Initializes the network interface and starts the epoll worker thread.
     * @param ip The IP address to bind to (for servers) or connect to (for clients).
     * @param port The port to listen on (for servers) or connect to (for clients).
     * @return True if initialization is successful, false otherwise.
     * @override
     */
    bool initialize(const std::string& ip, int port) override;

    /**
     * @brief Closes all sockets and stops the epoll worker thread.
     * @override
     */
    void close() override;

    /**
     * @brief Asynchronously connects to a server.
     * @param ip The IP address of the server.
     * @param port The port of the server.
     * @param callback The function to call upon completion of the connect operation.
     * @override
     */
    void asyncConnect(const std::string& ip, int port, ConnectCallback callback) override;

    /**
     * @brief Asynchronously accepts an incoming client connection.
     * @param callback The function to call upon completion of the accept operation.
     * @override
     */
    void asyncAccept(AcceptCallback callback) override;

    /**
     * @brief Asynchronously sends data over the socket.
     * @param data The data to send.
     * @param callback The function to call upon completion of the send operation.
     * @override
     */
    void asyncSend(const std::vector<char>& data, SendCallback callback) override;

    /**
     * @brief Asynchronously receives data from the socket.
     * @param bufferSize The size of the buffer to use for receiving data.
     * @param callback The function to call upon completion of the receive operation.
     * @override
     */
    void asyncReceive(size_t bufferSize, RecvCallback callback) override;

    /**
     * @brief Synchronously sends data over the socket.
     * @param data The data to send.
     * @return The number of bytes sent, or -1 on error.
     * @override
     */
    int blockingSend(const std::vector<char>& data) override;

    /**
     * @brief Synchronously receives data from the socket.
     * @param bufferSize The maximum number of bytes to receive.
     * @return A vector containing the received data.
     * @override
     */
    std::vector<char> blockingReceive(size_t bufferSize) override;

private:
    // --- Epoll and Socket Management ---
    /**< Listening file descriptor for server mode. */
    int listenFd;
    /**< File descriptor for the client or an accepted connection. */
    int clientFd;
    /**< The epoll instance file descriptor. */
    int epollFd;
    /**< The single worker thread for processing epoll events. */
    std::thread epollThread;
    /**< Flag to control the running state of the worker thread. */
    std::atomic<bool> running;
    /**< Mutex to protect access to the socket data map. */
    std::mutex socketDataMutex;
    /**< Maps a file descriptor to its associated data. */
    std::map<int, std::unique_ptr<SocketData>> socketDataMap;

    /**
     * @brief The main function for the epoll worker thread.
     * This function waits for I/O events and dispatches them.
     */
    void epollWorkerThread();

    /**
     * @brief Adds or modifies a file descriptor in the epoll set.
     * @param fd The file descriptor to add/modify.
     * @param events The events to monitor (e.g., EPOLLIN, EPOLLOUT).
     * @param data A pointer to the SocketData associated with the FD.
     */
    void addFdToEpoll(int fd, uint32_t events, SocketData* data);

    /**
     * @brief Removes a file descriptor from the epoll set.
     * @param fd The file descriptor to remove.
     */
    void removeFdFromEpoll(int fd);
};

#endif // !_WIN32
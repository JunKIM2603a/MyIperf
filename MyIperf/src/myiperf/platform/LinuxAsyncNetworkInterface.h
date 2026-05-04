// LinuxAsyncNetworkInterface.h
#pragma once

#ifndef _WIN32 // Guard for Linux-only compilation
#include "myiperf/NetworkInterface.h"
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

    /**< The current events mask being monitored by epoll for this socket. */
    uint32_t currentEvents = 0;
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
     * @brief Initializes epoll and creates the listening socket for server mode.
     * @param ip The IP address to bind to.
     * @param port The port to listen on.
     * @return True if server setup is successful, false otherwise.
     * @override
     */
    bool prepareServer(const std::string& ip, int port) override;

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
    void doAsyncConnect(const std::string& ip, int port, ConnectCallback callback) override;

    /**
     * @brief Asynchronously accepts an incoming client connection.
     * @param callback The function to call upon completion of the accept operation.
     * @override
     */
    void doAsyncAccept(AcceptCallback callback) override;

    /**
     * @brief Asynchronously sends data over the socket.
     * @param data The data to send.
     * @param callback The function to call upon completion of the send operation.
     * @override
     */
    void doAsyncSend(const std::vector<char>& data, SendCallback callback) override;

    /**
     * @brief Asynchronously receives data from the socket.
     * @param bufferSize The size of the buffer to use for receiving data.
     * @param callback The function to call upon completion of the receive operation.
     * @override
     */
    void doAsyncReceive(size_t bufferSize, RecvCallback callback) override;

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

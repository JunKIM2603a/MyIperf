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
    Recv,
    Send,
    Accept,
    Connect
};

/**
 * @struct SocketData
 * @brief Contains all necessary data associated with a file descriptor being monitored by epoll.
 */
struct SocketData {
    int fd;                         // The file descriptor.
    LinuxOperationType operationType; // The current operation associated with this FD.
    std::vector<char> buffer;       // Buffer for receive operations.
    std::vector<char> sendData;     // Buffer for data to be sent.
    
    // Callbacks for asynchronous operations
    RecvCallback recvCallback;
    SendCallback sendCallback;
    ConnectCallback connectCallback;
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
    LinuxAsyncNetworkInterface();
    ~LinuxAsyncNetworkInterface();

    // --- Overridden NetworkInterface methods ---
    bool initialize(const std::string& ip, int port) override;
    void close() override;

    void asyncConnect(const std::string& ip, int port, ConnectCallback callback) override;
    void asyncAccept(AcceptCallback callback) override;
    void asyncSend(const std::vector<char>& data, SendCallback callback) override;
    void asyncReceive(size_t bufferSize, RecvCallback callback) override;

    int blockingSend(const std::vector<char>& data) override;
    std::vector<char> blockingReceive(size_t bufferSize) override;

private:
    // --- Epoll and Socket Management ---
    int listenFd;       // Listening file descriptor for server mode.
    int clientFd;       // File descriptor for the client or an accepted connection.
    int epollFd;        // The epoll instance file descriptor.
    std::thread epollThread; // The single worker thread for processing epoll events.
    std::atomic<bool> running; // Flag to control the running state of the worker thread.
    std::mutex socketDataMutex; // Mutex to protect access to the socket data map.
    std::map<int, std::unique_ptr<SocketData>> socketDataMap; // Maps a file descriptor to its associated data.

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

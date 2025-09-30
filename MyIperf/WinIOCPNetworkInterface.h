#pragma once

#ifdef _WIN32
#include "NetworkInterface.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h> // Required for AcceptEx, ConnectEx
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <deque>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

/**
 * @enum OperationType
 * @brief Defines the type of asynchronous I/O operation for IOCP.
 */
enum class OperationType {
    Recv,    // A receive operation.
    Send,    // A send operation.
    Accept,  // An accept operation (server-side).
    Connect  // A connect operation (client-side).
};

/**
 * @struct IO_DATA
 * @brief Contains all necessary data for an overlapped I/O operation.
 * This structure is passed to IOCP and retrieved upon completion.
 */
struct IO_DATA {
    OVERLAPPED overlapped;      // The Windows OVERLAPPED structure, must be the first member.
    WSABUF wsaBuf;              // The buffer information for WSASend/WSARecv.
    char buffer[4096];          // A general-purpose buffer for receive operations.
    OperationType operationType;// The type of I/O operation this structure represents.
    SOCKET clientSocket;        // Holds the socket for a newly accepted client.
    std::vector<char> sendData; // Buffer for data to be sent.
    
    // Callbacks for asynchronous operations
    /** @brief Callback function to be invoked upon completion of a receive operation. */
    RecvCallback recvCallback;
    /** @brief Callback function to be invoked upon completion of a send operation. */
    SendCallback sendCallback;
    /** @brief Callback function to be invoked upon completion of a connect operation. */
    ConnectCallback connectCallback;
    /** @brief Callback function to be invoked upon completion of an accept operation. */
    AcceptCallback acceptCallback;
};

/**
 * @class WinIOCPNetworkInterface
 * @brief A Windows-specific implementation of the NetworkInterface using I/O Completion Ports (IOCP).
 *
 * This class provides a high-performance, scalable network backend for Windows platforms.
 * It uses IOCP to handle multiple asynchronous I/O requests efficiently with a pool of worker threads.
 */
class WinIOCPNetworkInterface : public NetworkInterface {
public:
    /**
     * @brief Constructs a WinIOCPNetworkInterface object.
     */
    WinIOCPNetworkInterface();

    /**
     * @brief Destroys the WinIOCPNetworkInterface object, ensuring all resources are released.
     */
    ~WinIOCPNetworkInterface();

    // --- Overridden NetworkInterface methods ---
    /**
     * @brief Initializes the network interface and starts the IOCP worker threads.
     * @param ip The IP address to bind to (for servers) or connect to (for clients).
     * @param port The port to listen on (for servers) or connect to (for clients).
     * @return True if initialization is successful, false otherwise.
     * @override
     */
    bool initialize(const std::string& ip, int port) override;

    /**
     * @brief Closes all sockets and stops the IOCP worker threads.
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

    /**
     * @brief Sets up a listening socket for the server.
     * @param ip The IP address to bind to.
     * @param port The port to listen on.
     * @return True if the socket is set up successfully, false otherwise.
     */
    bool setupListeningSocket(const std::string& ip, int port);

private:
    // --- IOCP and Socket Management ---
    /** @brief The socket used for listening for incoming connections in server mode. */
    SOCKET listenSocket;
    /** @brief The socket used for communication with the client (in server mode) or server (in client mode). */
    SOCKET clientSocket;
    /** @brief The handle to the I/O Completion Port. */
    HANDLE iocpHandle;
    /** @brief A pool of worker threads to process IOCP events. */
    std::vector<std::thread> workerThreads;
    /** @brief An atomic flag to control the running state of the worker threads. */
    std::atomic<bool> running;
    
    /**
     * @brief The main function for the IOCP worker threads.
     * This function waits for completed I/O events and dispatches them.
     */
    void iocpWorkerThread();
};

#endif // _WIN32
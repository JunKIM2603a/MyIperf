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
    RecvCallback recvCallback;
    SendCallback sendCallback;
    ConnectCallback connectCallback;
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
    WinIOCPNetworkInterface();
    ~WinIOCPNetworkInterface();

    // --- Overridden NetworkInterface methods ---
    bool initialize(const std::string& ip, int port) override;
    void close() override;

    void asyncConnect(const std::string& ip, int port, ConnectCallback callback) override;
    void asyncAccept(AcceptCallback callback) override;
    void asyncSend(const std::vector<char>& data, SendCallback callback) override;
    void asyncReceive(size_t bufferSize, RecvCallback callback) override;

    int blockingSend(const std::vector<char>& data) override;
    std::vector<char> blockingReceive(size_t bufferSize) override;

    bool setupListeningSocket(const std::string& ip, int port);

private:
    // --- IOCP and Socket Management ---
    SOCKET listenSocket; // Socket for listening in server mode.
    SOCKET clientSocket; // Socket for client mode or the accepted client in server mode.
    HANDLE iocpHandle;   // Handle to the I/O Completion Port.
    std::vector<std::thread> workerThreads; // Pool of threads to process IOCP events.
    std::atomic<bool> running; // Flag to control the running state of the worker threads.
    
    /**
     * @brief The main function for the IOCP worker threads.
     * This function waits for completed I/O events and dispatches them.
     */
    void iocpWorkerThread();
};

#endif // _WIN32

#ifdef _WIN32
#include "WinIOCPNetworkInterface.h"
#include "Logger.h"
#include "Protocol.h"
#include <stdexcept>
#include <chrono>
#include <ws2tcpip.h> // For InetPton, InetNtop
#include <string>
#include <sstream>
#include <Mswsock.h> // For CancelIoEx

#pragma comment(lib, "Mswsock.lib")

/**
 * @brief Retrieves the error message string from a Windows error code.
 * @param errorCode The Windows error code.
 * @return The error message string.
 */
std::string getErrorMessage(DWORD errorCode) {
    if (errorCode == 0) {
        return "No error.";
    }

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    std::string message(messageBuffer, size);

    // Free the buffer allocated by FormatMessage.
    LocalFree(messageBuffer);

    // Trim trailing newline characters.
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
    }

    return message;
}


/**
 * @brief Constructs the WinIOCPNetworkInterface.
 * Initializes Winsock.
 */
WinIOCPNetworkInterface::WinIOCPNetworkInterface() 
    : listenSocket(INVALID_SOCKET), clientSocket(INVALID_SOCKET), iocpHandle(NULL), running(false) {
    WSADATA wsaData;
    int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res != 0) {
        throw std::runtime_error("Fatal: WSAStartup failed with error: " + std::to_string(res) + " - " + getErrorMessage(res));
    }
}

/**
 * @brief Destructor.
 * Cleans up resources and shuts down Winsock.
 */
WinIOCPNetworkInterface::~WinIOCPNetworkInterface() {
    close();
    WSACleanup();
}

/**
 * @brief Initializes the IOCP and network sockets.
 * @param ip The IP address to bind to (for server) or ignored (for client).
 * @param port The port to listen on (for server) or ignored (for client).
 * @return True on success, false on failure.
 */
bool WinIOCPNetworkInterface::initialize(const std::string& ip, int port) {
    iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iocpHandle == NULL) {
        DWORD errorCode = GetLastError();
        Logger::log("Error: CreateIoCompletionPort failed with error: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        return false;
    }

    // The socket is created and bound/connected in asyncConnect/asyncAccept.
    // This method just sets up the IOCP and worker threads.

    running = true;
    unsigned int threadCount = std::thread::hardware_concurrency();
    for (unsigned int i = 0; i < threadCount; ++i) {
        workerThreads.emplace_back(&WinIOCPNetworkInterface::iocpWorkerThread, this);
    }
    Logger::log("Info: IOCP network interface initialized with " + std::to_string(threadCount) + " worker threads.");

    return true;
}

/**
 * @brief Shuts down the interface, closes handles, and stops threads gracefully.
 */
void WinIOCPNetworkInterface::close() {
    if (!running.exchange(false)) {
        return; // Already closed or closing
    }
    Logger::log("Debug: WinIOCPNetworkInterface::close() started.");

    // 1. Cancel any pending I/O operations on the sockets.
    // This helps unblock the worker threads from GetQueuedCompletionStatus.
    if (listenSocket != INVALID_SOCKET) {
        CancelIoEx((HANDLE)listenSocket, NULL);
    }
    if (clientSocket != INVALID_SOCKET) {
        CancelIoEx((HANDLE)clientSocket, NULL);
    }
    Logger::log("Debug: Canceled pending I/O operations.");

    // 2. Gracefully shut down the sockets.
    // This signals the other side of the connection that we are closing.
    if (clientSocket != INVALID_SOCKET) {
        shutdown(clientSocket, SD_BOTH);
    }
    Logger::log("Debug: Shutdown client socket.");

    // 3. Post completion statuses to wake up worker threads.
    // This ensures they exit their loop if they are waiting on an empty queue.
    for (size_t i = 0; i < workerThreads.size(); ++i) {
        PostQueuedCompletionStatus(iocpHandle, 0, 0, NULL);
    }
    Logger::log("Debug: Posted shutdown messages to worker threads.");

    // 4. Wait for all worker threads to terminate.
    for (auto& t : workerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    workerThreads.clear();
    Logger::log("Debug: All worker threads have joined.");

    // 5. Now it's safe to close the sockets and the IOCP handle.
    if (listenSocket != INVALID_SOCKET) {
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        Logger::log("Debug: Closed listen socket.");
    }
    if (clientSocket != INVALID_SOCKET) {
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
        Logger::log("Debug: Closed client socket.");
    }
    if (iocpHandle != NULL) {
        CloseHandle(iocpHandle);
        iocpHandle = NULL;
        Logger::log("Debug: Closed IOCP handle.");
    }

    Logger::log("Info: Network interface closed successfully.");
}

/**
 * @brief Asynchronously connects to a server using ConnectEx.
 * @param ip The server's IP address.
 * @param port The server's port.
 * @param callback The function to call upon completion.
 */
void WinIOCPNetworkInterface::asyncConnect(const std::string& ip, int port, ConnectCallback callback) {
    clientSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (clientSocket == INVALID_SOCKET) {
        int errorCode = WSAGetLastError();
        Logger::log("Error: WSASocket creation failed with error: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        callback(false);
        return;
    }

    // Associate the client socket with the IOCP handle.
    if (CreateIoCompletionPort((HANDLE)clientSocket, iocpHandle, (ULONG_PTR)clientSocket, 0) == NULL) {
        DWORD errorCode = GetLastError();
        Logger::log("Error: CreateIoCompletionPort for client socket failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
        callback(false);
        return;
    }

    // Bind the socket to a local address before calling ConnectEx.
    sockaddr_in localAddr;
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    localAddr.sin_port = 0; // System will choose a port.
    if (bind(clientSocket, (SOCKADDR*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        int errorCode = WSAGetLastError();
        Logger::log("Error: bind for ConnectEx failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
        callback(false);
        return;
    }

    // Get a pointer to the ConnectEx function.
    LPFN_CONNECTEX ConnectExPtr = NULL;
    GUID guidConnectEx = WSAID_CONNECTEX;
    DWORD bytes = 0;
    if (WSAIoctl(clientSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidConnectEx, sizeof(guidConnectEx), &ConnectExPtr, sizeof(ConnectExPtr), &bytes, NULL, NULL) != 0) {
        int errorCode = WSAGetLastError();
        Logger::log("Error: WSAIoctl to get ConnectEx pointer failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
        callback(false);
        return;
    }

    IO_DATA* ioData = new IO_DATA();
    memset(&ioData->overlapped, 0, sizeof(ioData->overlapped));
    ioData->operationType = OperationType::Connect;
    ioData->connectCallback = callback;

    sockaddr_in service;
    service.sin_family = AF_INET;
    InetPtonA(AF_INET, ip.c_str(), &service.sin_addr);
    service.sin_port = htons(port);

    // Initiate the asynchronous connection.
    if (!ConnectExPtr(clientSocket, (SOCKADDR*)&service, sizeof(service), NULL, 0, NULL, &ioData->overlapped)) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            Logger::log("Error: ConnectEx failed with error: " + std::to_string(error) + " - " + getErrorMessage(error));
            delete ioData;
            callback(false);
            return;
        }
    }
}

/**
 * @brief Asynchronously accepts a client connection using AcceptEx.
 * @param callback The function to call upon completion.
 */
void WinIOCPNetworkInterface::asyncAccept(AcceptCallback callback) {
    if (listenSocket == INVALID_SOCKET) {
        Logger::log("Error: asyncAccept called with an invalid listen socket.");
        callback(false, "", 0);
        return;
    }

    IO_DATA* ioData = new IO_DATA();
    memset(&ioData->overlapped, 0, sizeof(ioData->overlapped));
    ioData->operationType = OperationType::Accept;
    ioData->acceptCallback = callback;

    // Create a new socket for the accepted connection directly into the IO_DATA structure.
    ioData->clientSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (ioData->clientSocket == INVALID_SOCKET) {
        int errorCode = WSAGetLastError();
        Logger::log("Error: WSASocket for accepted connection failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        delete ioData;
        callback(false, "", 0);
        return;
    }

    // Get a pointer to the AcceptEx function.
    LPFN_ACCEPTEX AcceptExPtr = NULL;
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytes = 0;
    if (WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &AcceptExPtr, sizeof(AcceptExPtr), &bytes, NULL, NULL) != 0) {
        int errorCode = WSAGetLastError();
        Logger::log("Error: WSAIoctl to get AcceptEx pointer failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        closesocket(ioData->clientSocket);
        delete ioData;
        callback(false, "", 0);
        return;
    }

    DWORD dwBytesReceived = 0;
    // Initiate the asynchronous accept.
    // The buffer is for receiving the first chunk of data along with the accept, which we are not doing here (dwBufferLength = 0).
    if (!AcceptExPtr(listenSocket, ioData->clientSocket, ioData->buffer, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &dwBytesReceived, &ioData->overlapped)) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            Logger::log("Error: AcceptEx failed with error: " + std::to_string(error) + " - " + getErrorMessage(error));
            closesocket(ioData->clientSocket);
            delete ioData;
            callback(false, "", 0);
            return;
        }
    }
}

/**
 * @brief Asynchronously sends data.
 * @param data The data to send.
 * @param callback The function to call upon completion.
 */
void WinIOCPNetworkInterface::asyncSend(const std::vector<char>& data, SendCallback callback) {
#ifdef DEBUG_LOG    
    Logger::log("Debug: asyncSend called. Data size: " + std::to_string(data.size()));
#endif
    if (clientSocket == INVALID_SOCKET) {
        Logger::log("Error: asyncSend called on an invalid socket.");
        callback(0);
        return;
    }

    IO_DATA* ioData = new IO_DATA();
    memset(&ioData->overlapped, 0, sizeof(ioData->overlapped));
    ioData->operationType = OperationType::Send;
    ioData->sendCallback = callback;
    ioData->sendData = data; // Copy data to be sent.

    ioData->wsaBuf.buf = ioData->sendData.data();
    ioData->wsaBuf.len = static_cast<ULONG>(ioData->sendData.size());

    DWORD bytesSent = 0;
    if (WSASend(clientSocket, &ioData->wsaBuf, 1, &bytesSent, 0, &ioData->overlapped, NULL) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            Logger::log("Error: WSASend failed with error: " + std::to_string(error) + " - " + getErrorMessage(error));
            delete ioData;
            callback(0);
        } else {
#ifdef DEBUG_LOG
            Logger::log("Debug: WSASend pending for " + std::to_string(ioData->sendData.size()) + " bytes.");
#endif
        }
    }
}

/**
 * @brief Asynchronously receives data.
 * @param bufferSize The size of the buffer to use (currently ignored, uses internal buffer).
 * @param callback The function to call upon completion.
 */
void WinIOCPNetworkInterface::asyncReceive(size_t bufferSize, RecvCallback callback) {
#ifdef DEBUG_LOG
    Logger::log("Debug: asyncReceive called. Buffer size: " + std::to_string(bufferSize));
#endif
    if (clientSocket == INVALID_SOCKET) {
        Logger::log("Error: asyncReceive called on an invalid socket.");
        callback({}, 0);
        return;
    }

    IO_DATA* ioData = new IO_DATA();
    memset(&ioData->overlapped, 0, sizeof(ioData->overlapped));
    ioData->operationType = OperationType::Recv;
    ioData->recvCallback = callback;
    ioData->wsaBuf.buf = ioData->buffer;
    ioData->wsaBuf.len = static_cast<ULONG>(sizeof(ioData->buffer));

    DWORD bytesReceived = 0;
    DWORD flags = 0;
    if (WSARecv(clientSocket, &ioData->wsaBuf, 1, &bytesReceived, &flags, &ioData->overlapped, NULL) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            Logger::log("Error: WSARecv failed with error: " + std::to_string(error) + " - " + getErrorMessage(error));
            if (ioData->recvCallback) {
                ioData->recvCallback({}, 0); // Signal failure to PacketReceiver
            }
            delete ioData;
            return;
        }
    }
}

// --- Blocking methods (less preferred, for simple cases) ---

/**
 * @brief Sends data synchronously.
 * @param data The data to send.
 * @return The number of bytes sent, or -1 on error.
 */
int WinIOCPNetworkInterface::blockingSend(const std::vector<char>& data) {
    if (clientSocket == INVALID_SOCKET) return -1;
    int bytesSent = send(clientSocket, data.data(), static_cast<int>(data.size()), 0);
    if (bytesSent == SOCKET_ERROR) {
        int errorCode = WSAGetLastError();
        Logger::log("Error: blockingSend failed with error: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
    }
    return bytesSent;
}

/**
 * @brief Receives data synchronously.
 * @param bufferSize The size of the buffer to receive into.
 * @return A vector containing the received data. An empty vector indicates an error or closed connection.
 */
std::vector<char> WinIOCPNetworkInterface::blockingReceive(size_t bufferSize) {
    if (clientSocket == INVALID_SOCKET) return {};
    std::vector<char> buffer(bufferSize);
    int bytesReceived = recv(clientSocket, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (bytesReceived <= 0) {
        int errorCode = WSAGetLastError();
        Logger::log("Error: blockingReceive failed or connection closed. Error: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        return {};
    }
    buffer.resize(bytesReceived);
    return buffer;
}

/**
 * @brief The main worker thread function for processing IOCP events.
 */
void WinIOCPNetworkInterface::iocpWorkerThread() {
    Logger::log("Info: IOCP worker thread starting.");
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED lpOverlapped = NULL;

    while (running) {
        BOOL success = GetQueuedCompletionStatus(iocpHandle, &bytesTransferred, &completionKey, &lpOverlapped, INFINITE);

        if (!running) break;

        IO_DATA* ioData = (IO_DATA*)lpOverlapped;

        if (ioData == NULL) { // Shutdown signal
            Logger::log("Info: IOCP worker thread received shutdown signal.");
            continue;
        }

        if (!success || (bytesTransferred == 0 && ioData->operationType != OperationType::Accept && ioData->operationType != OperationType::Connect)) {
            // Graceful shutdown is not an error. It's indicated by a successful receive operation of 0 bytes.
            if (success && bytesTransferred == 0 && ioData->operationType == OperationType::Recv) {
                Logger::log("Info: Connection gracefully closed by peer.");
            } else {
                DWORD errorCode = GetLastError();
                // ERROR_OPERATION_ABORTED (995) is expected when we call CancelIoEx during shutdown.
                // Don't log it as a warning.
                if (errorCode != ERROR_OPERATION_ABORTED) {
                    Logger::log("Warning: I/O operation failed or connection closed. Error: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
                }
            }

            if (ioData->operationType == OperationType::Recv) ioData->recvCallback({}, 0);
            else if (ioData->operationType == OperationType::Send) ioData->sendCallback(0);
            else if (ioData->operationType == OperationType::Accept) ioData->acceptCallback(false, "", 0);
            else if (ioData->operationType == OperationType::Connect) ioData->connectCallback(false);
            delete ioData;
            continue;
        }

        switch (ioData->operationType) {
            case OperationType::Recv: {
#ifdef DEBUG_LOG
                Logger::log("Debug: Receive operation completed. Bytes transferred: " + std::to_string(bytesTransferred));
#endif
                if (bytesTransferred >= sizeof(PacketHeader)) {
                    const PacketHeader* header = reinterpret_cast<const PacketHeader*>(ioData->buffer);
                    if (header->messageType != MessageType::DATA_PACKET) {
#ifdef DEBUG_LOG
                        std::ostringstream handshakeLog;
                        handshakeLog << "\x1b[95mHANDSHAKE: IOCP received message type "
                                     << static_cast<int>(header->messageType)
                                     << " (" << bytesTransferred << " bytes)\x1b[0m";
                        Logger::log(handshakeLog.str());
#endif
                    }
                } else if (bytesTransferred > 0) {
                    std::ostringstream handshakeLog;
                    handshakeLog << "\x1b[95mHANDSHAKE: IOCP received partial header ("
                                 << bytesTransferred << " bytes)\x1b[0m";
                    Logger::log(handshakeLog.str());
                }

                std::vector<char> receivedData(ioData->buffer, ioData->buffer + bytesTransferred);
                ioData->recvCallback(receivedData, bytesTransferred);
                delete ioData;
                break;
            }
            case OperationType::Send: {
#ifdef DEBUG_LOG
                Logger::log("Debug: Send operation completed. Bytes transferred: " + std::to_string(bytesTransferred));
#endif
                ioData->sendCallback(bytesTransferred);
                delete ioData;
                break;
            }
            case OperationType::Accept: {
                if (setsockopt(ioData->clientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listenSocket, sizeof(listenSocket)) == SOCKET_ERROR) {
                    int errorCode = WSAGetLastError();
                    Logger::log("Error: setsockopt SO_UPDATE_ACCEPT_CONTEXT failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
                    ioData->acceptCallback(false, "", 0);
                    closesocket(ioData->clientSocket);
                } else {
                    this->clientSocket = ioData->clientSocket;
                    if (CreateIoCompletionPort((HANDLE)this->clientSocket, iocpHandle, (ULONG_PTR)this->clientSocket, 0) == NULL) {
                        DWORD errorCode = GetLastError();
                        Logger::log("Error: CreateIoCompletionPort for accepted socket failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
                        ioData->acceptCallback(false, "", 0);
                        closesocket(this->clientSocket);
                    } else {
                        sockaddr_in addr;
                        int addrLen = sizeof(addr);
                        getpeername(ioData->clientSocket, (SOCKADDR*)&addr, &addrLen);
                        char clientIp[INET_ADDRSTRLEN];
                        InetNtopA(AF_INET, &addr.sin_addr, clientIp, INET_ADDRSTRLEN);
                        Logger::log("Info: Client connection accepted from " + std::string(clientIp) + ":" + std::to_string(ntohs(addr.sin_port)));
                        ioData->acceptCallback(true, clientIp, ntohs(addr.sin_port));
                    }
                }
                delete ioData;
                break;
            }
            case OperationType::Connect: {
                if (setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == SOCKET_ERROR) {
                    int errorCode = WSAGetLastError();
                    Logger::log("Error: setsockopt SO_UPDATE_CONNECT_CONTEXT failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
                    ioData->connectCallback(false);
                } else {
                    Logger::log("Info: Successfully connected to the server.");
                    ioData->connectCallback(true);
                }
                delete ioData;
                break;
            }
        }
    }
    Logger::log("Info: IOCP worker thread stopping.");
}

/**
 * @brief Sets up the listening socket for the server.
 * @param ip The IP address to bind to.
 * @param port The port to listen on.
 * @return True if successful, false otherwise.
 */
bool WinIOCPNetworkInterface::setupListeningSocket(const std::string& ip, int port) {
    listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (listenSocket == INVALID_SOCKET) {
        int errorCode = WSAGetLastError();
        Logger::log("Error: WSASocket for listening failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        return false;
    }

    sockaddr_in service;
    service.sin_family = AF_INET;
    InetPtonA(AF_INET, ip.c_str(), &service.sin_addr);
    service.sin_port = htons(port);

    if (bind(listenSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        int errorCode = WSAGetLastError();
        Logger::log("Error: bind for listening failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        int errorCode = WSAGetLastError();
        Logger::log("Error: listen failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }

    // Associate the listening socket with the IOCP handle.
    if (CreateIoCompletionPort((HANDLE)listenSocket, iocpHandle, (ULONG_PTR)listenSocket, 0) == NULL) {
        DWORD errorCode = GetLastError();
        Logger::log("Error: CreateIoCompletionPort for listening socket failed: " + std::to_string(errorCode) + " - " + getErrorMessage(errorCode));
        closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
        return false;
    }

    Logger::log("Info: Listening socket set up on " + ip + ":" + std::to_string(port));
    return true;
}

#endif // _WIN32
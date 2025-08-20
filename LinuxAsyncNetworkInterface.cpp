// LinuxAsyncNetworkInterface.cpp
#ifndef _WIN32
#include "LinuxAsyncNetworkInterface.h"
#include "Logger.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h> // For strerror

/**
 * @brief Helper function to set a socket to non-blocking mode.
 * @param fd The file descriptor of the socket.
 * @return True on success, false on failure.
 */
static bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        Logger::log("Error: fcntl(F_GETFL) failed: " + std::string(strerror(errno)));
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        Logger::log("Error: fcntl(F_SETFL, O_NONBLOCK) failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

/**
 * @brief Constructs the LinuxAsyncNetworkInterface.
 */
LinuxAsyncNetworkInterface::LinuxAsyncNetworkInterface() 
    : listenFd(-1), clientFd(-1), epollFd(-1), running(false) {}

/**
 * @brief Destructor.
 * Ensures all resources are closed.
 */
LinuxAsyncNetworkInterface::~LinuxAsyncNetworkInterface() {
    close();
}

/**
 * @brief Initializes the epoll instance and the worker thread.
 * @param ip The IP address to use (not used in the current client setup).
 * @param port The port to use (not used in the current client setup).
 * @return True on success, false on failure.
 */
bool LinuxAsyncNetworkInterface::initialize(const std::string& ip, int port) {
    epollFd = epoll_create1(0);
    if (epollFd == -1) {
        Logger::log("Error: epoll_create1 failed: " + std::string(strerror(errno)));
        return false;
    }

    running = true;
    epollThread = std::thread(&LinuxAsyncNetworkInterface::epollWorkerThread, this);
    Logger::log("Info: Epoll network interface initialized.");

    return true;
}

/**
 * @brief Shuts down the interface, closes file descriptors, and stops the thread.
 */
void LinuxAsyncNetworkInterface::close() {
    if (!running.exchange(false)) {
        return; // Already closed
    }

    // Closing the epollFd will cause the epoll_wait in the worker thread to unblock.
    if (epollFd != -1) {
        ::close(epollFd);
        epollFd = -1;
    }

    if (epollThread.joinable()) {
        epollThread.join();
    }

    if (listenFd != -1) {
        ::close(listenFd);
        listenFd = -1;
    }
    if (clientFd != -1) {
        ::close(clientFd);
        clientFd = -1;
    }
    
    Logger::log("Info: Network interface closed.");
}

/**
 * @brief Asynchronously connects to a server.
 * @param ip The server's IP address.
 * @param port The server's port.
 * @param callback The function to call upon completion.
 */
void LinuxAsyncNetworkInterface::asyncConnect(const std::string& ip, int port, ConnectCallback callback) {
    clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd == -1) {
        Logger::log("Error: Socket creation for connect failed: " + std::string(strerror(errno)));
        callback(false);
        return;
    }

    if (!setNonBlocking(clientFd)) {
        ::close(clientFd);
        clientFd = -1;
        callback(false);
        return;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);
    serverAddr.sin_port = htons(port);

    int res = connect(clientFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (res == -1 && errno != EINPROGRESS) {
        Logger::log("Error: connect failed immediately: " + std::string(strerror(errno)));
        ::close(clientFd);
        clientFd = -1;
        callback(false);
        return;
    }

    // Connection is in progress. Add to epoll to monitor for writability (EPOLLOUT).
    auto clientData = std::make_unique<SocketData>();
    clientData->fd = clientFd;
    clientData->operationType = LinuxOperationType::Connect;
    clientData->connectCallback = callback;
    
    addFdToEpoll(clientFd, EPOLLOUT | EPOLLET, clientData.get());
    
    std::lock_guard<std::mutex> lock(socketDataMutex);
    socketDataMap[clientFd] = std::move(clientData);
}

/**
 * @brief Asynchronously accepts a client connection.
 * @param callback The function to call upon completion.
 */
void LinuxAsyncNetworkInterface::asyncAccept(AcceptCallback callback) {
    // In this design, accept is not a one-shot call but is handled continuously
    // by the epoll worker when the listen socket has an EPOLLIN event.
    // We store the callback in the listen socket's data.
    if (listenFd != -1) {
        std::lock_guard<std::mutex> lock(socketDataMutex);
        if (socketDataMap.count(listenFd)) {
            socketDataMap[listenFd]->acceptCallback = callback;
        }
    } else {
        Logger::log("Error: asyncAccept called but no listen socket is configured.");
    }
}

/**
 * @brief Asynchronously sends data.
 * @param data The data to send.
 * @param callback The function to call upon completion.
 */
void LinuxAsyncNetworkInterface::asyncSend(const std::vector<char>& data, SendCallback callback) {
    if (clientFd == -1) {
        Logger::log("Error: asyncSend called on an invalid socket.");
        if (callback) callback(0);
        return;
    }

    std::lock_guard<std::mutex> lock(socketDataMutex);
    auto it = socketDataMap.find(clientFd);
    if (it == socketDataMap.end() || !it->second) {
        Logger::log("Error: asyncSend called but no socket data found for clientFd: " + std::to_string(clientFd));
        if (callback) callback(0);
        return;
    }

    auto& socketData = it->second;
    socketData->sendData.insert(socketData->sendData.end(), data.begin(), data.end());
    socketData->sendCallback = callback;

    // Ensure the socket is monitored for writability.
    addFdToEpoll(clientFd, EPOLLIN | EPOLLOUT | EPOLLET, socketData.get());
}

/**
 * @brief Asynchronously receives data.
 * @param bufferSize The size of the buffer to use.
 * @param callback The function to call upon completion.
 */
void LinuxAsyncNetworkInterface::asyncReceive(size_t bufferSize, RecvCallback callback) {
    if (clientFd == -1) {
        Logger::log("Error: asyncReceive called on an invalid socket.");
        if (callback) callback({}, 0);
        return;
    }

    std::lock_guard<std::mutex> lock(socketDataMutex);
    auto it = socketDataMap.find(clientFd);
    if (it == socketDataMap.end() || !it->second) {
        Logger::log("Error: asyncReceive called but no socket data found for clientFd: " + std::to_string(clientFd));
        if (callback) callback({}, 0);
        return;
    }

    auto& socketData = it->second;
    socketData->recvCallback = callback;
    socketData->buffer.resize(bufferSize);

    // Ensure the socket is monitored for readability.
    addFdToEpoll(clientFd, EPOLLIN | EPOLLET, socketData.get());
}

// --- Blocking methods ---

int LinuxAsyncNetworkInterface::blockingSend(const std::vector<char>& data) {
    if (clientFd == -1) return -1;
    int bytesSent = ::send(clientFd, data.data(), data.size(), 0);
    if (bytesSent == -1) {
        Logger::log("Error: blockingSend failed: " + std::string(strerror(errno)));
    }
    return bytesSent;
}

std::vector<char> LinuxAsyncNetworkInterface::blockingReceive(size_t bufferSize) {
    if (clientFd == -1) return {};
    std::vector<char> buffer(bufferSize);
    int bytesReceived = ::recv(clientFd, buffer.data(), buffer.size(), 0);
    if (bytesReceived <= 0) {
        Logger::log("Error: blockingReceive failed or connection closed. Error: " + std::string(strerror(errno)));
        return {};
    }
    buffer.resize(bytesReceived);
    return buffer;
}

/**
 * @brief Adds or modifies a file descriptor in the epoll set.
 * @param fd The file descriptor.
 * @param events The events to monitor.
 * @param data Pointer to the associated SocketData.
 */
void LinuxAsyncNetworkInterface::addFdToEpoll(int fd, uint32_t events, SocketData* data) {
    epoll_event event;
    event.events = events;
    event.data.ptr = data;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event) == -1) {
        if (errno == EEXIST) { // If it already exists, modify it.
            if (epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &event) == -1) {
                Logger::log("Error: epoll_ctl(MOD) failed for fd " + std::to_string(fd) + ": " + std::string(strerror(errno)));
            }
        } else {
            Logger::log("Error: epoll_ctl(ADD) failed for fd " + std::to_string(fd) + ": " + std::string(strerror(errno)));
        }
    }
}

/**
 * @brief The main worker thread function for processing epoll events.
 * This is the core of the asynchronous logic for Linux.
 */
void LinuxAsyncNetworkInterface::epollWorkerThread() {
    Logger::log("Info: Epoll worker thread starting.");
    const int MAX_EVENTS = 10;
    epoll_event events[MAX_EVENTS];

    while (running) {
        int numEvents = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        if (!running) break;

        if (numEvents == -1) {
            if (errno == EINTR) continue;
            Logger::log("Error: epoll_wait failed: " + std::string(strerror(errno)));
            break;
        }

        for (int i = 0; i < numEvents; ++i) {
            SocketData* data = static_cast<SocketData*>(events[i].data.ptr);
            if (!data) continue;

            // --- Handle Listen Socket Events (Accept) ---
            if (data->fd == listenFd) {
                if (events[i].events & EPOLLIN) {
                    sockaddr_in clientAddr;
                    socklen_t clientAddrLen = sizeof(clientAddr);
                    while (true) {
                        int newClientFd = accept(listenFd, (struct sockaddr*)&clientAddr, &clientAddrLen);
                        if (newClientFd == -1) {
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                Logger::log("Error: accept() failed: " + std::string(strerror(errno)));
                            }
                            break; // No more pending connections
                        }

                        setNonBlocking(newClientFd);
                        char clientIp[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
                        int clientPort = ntohs(clientAddr.sin_port);

                        this->clientFd = newClientFd;

                        auto newClientData = std::make_unique<SocketData>();
                        newClientData->fd = newClientFd;
                        newClientData->operationType = LinuxOperationType::Recv;

                        addFdToEpoll(newClientFd, EPOLLIN | EPOLLET, newClientData.get());

                        AcceptCallback acceptCb;
                        {
                            std::lock_guard<std::mutex> lock(socketDataMutex);
                            socketDataMap[newClientFd] = std::move(newClientData);
                            if (socketDataMap.count(listenFd)) {
                                acceptCb = socketDataMap[listenFd]->acceptCallback;
                            }
                        }

                        if (acceptCb) {
                            Logger::log("Info: Client connection accepted from " + std::string(clientIp) + ":" + std::to_string(clientPort));
                            acceptCb(true, clientIp, clientPort);
                        }
                    }
                }
                continue;
            }

            // --- Handle Client Socket Events ---
            std::unique_lock<std::mutex> lock(socketDataMutex);
            if (socketDataMap.find(data->fd) == socketDataMap.end()) {
                lock.unlock();
                continue; // Socket data was removed by another operation, skip.
            }

            // --- Error Handling ---
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                Logger::log("Error: Epoll error or hang-up on fd: " + std::to_string(data->fd));
                if (data->connectCallback) data->connectCallback(false);
                if (data->recvCallback) data->recvCallback({}, 0);
                if (data->sendCallback) data->sendCallback(0);

                epoll_ctl(epollFd, EPOLL_CTL_DEL, data->fd, NULL);
                ::close(data->fd);
                socketDataMap.erase(data->fd);
                lock.unlock();
                continue;
            }

            // --- Connection Completion ---
            if (data->operationType == LinuxOperationType::Connect && (events[i].events & EPOLLOUT)) {
                int result;
                socklen_t result_len = sizeof(result);
                if (getsockopt(data->fd, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0 || result != 0) {
                    Logger::log("Error: Async connect failed: " + std::string(strerror(result)));
                    if (data->connectCallback) data->connectCallback(false);
                    epoll_ctl(epollFd, EPOLL_CTL_DEL, data->fd, NULL);
                    ::close(data->fd);
                    socketDataMap.erase(data->fd);
                } else {
                    Logger::log("Info: Successfully connected to the server.");
                    data->operationType = LinuxOperationType::Recv;
                    addFdToEpoll(data->fd, EPOLLIN | EPOLLET, data);
                    if (data->connectCallback) data->connectCallback(true);
                }
                continue;
            }

            // --- Data Sending ---
            if (events[i].events & EPOLLOUT) {
                if (!data->sendData.empty()) {
                    ssize_t bytesSent = ::send(data->fd, data->sendData.data(), data->sendData.size(), 0);
                    if (bytesSent > 0) {
                        data->sendData.erase(data->sendData.begin(), data->sendData.begin() + bytesSent);
                        if (data->sendCallback) data->sendCallback(bytesSent);
                        data->sendCallback = nullptr;
                    }
                    if (data->sendData.empty()) {
                        addFdToEpoll(data->fd, EPOLLIN | EPOLLET, data); // Stop listening for write
                    }
                }
            }

            // --- Data Receiving ---
            if (events[i].events & EPOLLIN) {
                std::vector<char> receivedData;
                char buffer[4096];
                while (true) {
                    ssize_t bytesReceived = ::recv(data->fd, buffer, sizeof(buffer), 0);
                    if (bytesReceived > 0) {
                        receivedData.insert(receivedData.end(), buffer, buffer + bytesReceived);
                    } else if (bytesReceived == 0) {
                        Logger::log("Info: Connection closed by peer on fd: " + std::to_string(data->fd));
                        if (data->recvCallback) data->recvCallback({}, 0);
                        epoll_ctl(epollFd, EPOLL_CTL_DEL, data->fd, NULL);
                        ::close(data->fd);
                        socketDataMap.erase(data->fd);
                        break;
                    } else {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            Logger::log("Error: recv() failed for fd " + std::to_string(data->fd) + ": " + std::string(strerror(errno)));
                            if (data->recvCallback) data->recvCallback({}, 0);
                            epoll_ctl(epollFd, EPOLL_CTL_DEL, data->fd, NULL);
                            ::close(data->fd);
                            socketDataMap.erase(data->fd);
                        } else if (data->recvCallback && !receivedData.empty()) {
                            data->recvCallback(receivedData, receivedData.size());
                        }
                        break;
                    }
                }
            }
        }
    }
    Logger::log("Info: Epoll worker thread stopping.");
}

/**
 * @brief Sets up a listening socket for the server.
 * @param ip The IP address to bind to.
 * @param port The port to listen on.
 * @return True on success, false on failure.
 */
bool LinuxAsyncNetworkInterface::setupListeningSocket(const std::string& ip, int port) {
    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == -1) {
        Logger::log("Error: socket() for listening failed: " + std::string(strerror(errno)));
        return false;
    }

    if (!setNonBlocking(listenFd)) {
        ::close(listenFd);
        listenFd = -1;
        return false;
    }

    int reuse = 1;
    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        Logger::log("Error: setsockopt(SO_REUSEADDR) failed: " + std::string(strerror(errno)));
        ::close(listenFd);
        listenFd = -1;
        return false;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);
    serverAddr.sin_port = htons(port);

    if (bind(listenFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        Logger::log("Error: bind() for listening failed: " + std::string(strerror(errno)));
        ::close(listenFd);
        listenFd = -1;
        return false;
    }

    if (listen(listenFd, SOMAXCONN) == -1) {
        Logger::log("Error: listen() failed: " + std::string(strerror(errno)));
        ::close(listenFd);
        listenFd = -1;
        return false;
    }

    auto listenData = std::make_unique<SocketData>();
    listenData->fd = listenFd;
    listenData->operationType = LinuxOperationType::Accept;

    addFdToEpoll(listenFd, EPOLLIN | EPOLLET, listenData.get());

    std::lock_guard<std::mutex> lock(socketDataMutex);
    socketDataMap[listenFd] = std::move(listenData);

    Logger::log("Info: Listening socket set up on " + ip + ":" + std::to_string(port));
    return true;
}

#endif // !_WIN32

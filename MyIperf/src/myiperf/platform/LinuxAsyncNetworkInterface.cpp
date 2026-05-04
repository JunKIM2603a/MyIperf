// LinuxAsyncNetworkInterface.cpp
#ifndef _WIN32
#include "platform/LinuxAsyncNetworkInterface.h"
#include "myiperf/Logger.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h> // For strerror
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

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
 * @param ip The IP address to use.
 * @param port The port to use.
 * @return True on success, false on failure.
 */
bool LinuxAsyncNetworkInterface::initialize(const std::string& ip, int port) {
    epollFd = epoll_create1(0);
    if (epollFd == -1) {
        Logger::log("Error: epoll_create1 failed: " + std::string(strerror(errno)));
        return false;
    }

    if (port != 0) { // Server mode: set up listening socket
        listenFd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd == -1) {
            Logger::log("Error: socket creation failed: " + std::string(strerror(errno)));
            ::close(epollFd);
            return false;
        }

        int opt = 1;
        if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
             Logger::log("Warning: setsockopt(SO_REUSEADDR) failed");
        }

        if (!setNonBlocking(listenFd)) {
            ::close(listenFd);
            ::close(epollFd);
            return false;
        }

        sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) {
            // If invalid IP or empty, try INADDR_ANY if intended, but usually TestController passes valid IP.
            // If ip is "0.0.0.0", inet_pton handles it.
            if (ip.empty())
                serverAddr.sin_addr.s_addr = INADDR_ANY;
            else {
                Logger::log("Error: Invalid IP address: " + ip);
                ::close(listenFd);
                ::close(epollFd);
                return false;
            }
        }
        serverAddr.sin_port = htons(port);

        if (bind(listenFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
            Logger::log("Error: bind failed: " + std::string(strerror(errno)));
            ::close(listenFd);
            ::close(epollFd);
            return false;
        }

        if (listen(listenFd, SOMAXCONN) == -1) {
            Logger::log("Error: listen failed: " + std::string(strerror(errno)));
            ::close(listenFd);
            ::close(epollFd);
            return false;
        }

        auto listenData = std::make_unique<SocketData>();
        listenData->fd = listenFd;
        listenData->operationType = LinuxOperationType::Accept;
        listenData->currentEvents = EPOLLIN; // Always listen for connections

        addFdToEpoll(listenFd, listenData->currentEvents, listenData.get());

        std::lock_guard<std::mutex> lock(socketDataMutex);
        socketDataMap[listenFd] = std::move(listenData);

        Logger::log("Info: Server listening on " + ip + ":" + std::to_string(port));
    }

    running = true;
    epollThread = std::thread(&LinuxAsyncNetworkInterface::epollWorkerThread, this);
    Logger::log("Info: Epoll network interface initialized.");

    return true;
}

bool LinuxAsyncNetworkInterface::prepareServer(const std::string& ip, int port) {
    return initialize(ip, port);
}

/**
 * @brief Shuts down the interface, closes file descriptors, and stops the thread.
 */
void LinuxAsyncNetworkInterface::close() {
    if (!running.exchange(false)) {
        return; // Already closed
    }

    // Closing the epollFd will cause the epoll_wait in the worker thread to unblock (or fail).
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
    
    // Clear map
    {
        std::lock_guard<std::mutex> lock(socketDataMutex);
        socketDataMap.clear();
    }

    Logger::log("Info: Network interface closed.");
}

/**
 * @brief Asynchronously connects to a server.
 * @param ip The server's IP address.
 * @param port The server's port.
 * @param callback The function to call upon completion.
 */
void LinuxAsyncNetworkInterface::doAsyncConnect(const std::string& ip, int port, ConnectCallback callback) {
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

    int res = ::connect(clientFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (res == 0) {
        // Immediate connection
        auto clientData = std::make_unique<SocketData>();
        clientData->fd = clientFd;
        clientData->operationType = LinuxOperationType::Recv;
        clientData->currentEvents = 0; // Nothing yet

        {
             std::lock_guard<std::mutex> lock(socketDataMutex);
             socketDataMap[clientFd] = std::move(clientData);
        }
        callback(true);
        return;
    }

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
    clientData->currentEvents = EPOLLOUT;
    
    addFdToEpoll(clientFd, clientData->currentEvents, clientData.get());
    
    std::lock_guard<std::mutex> lock(socketDataMutex);
    socketDataMap[clientFd] = std::move(clientData);
}

/**
 * @brief Asynchronously accepts a client connection.
 * @param callback The function to call upon completion.
 */
void LinuxAsyncNetworkInterface::doAsyncAccept(AcceptCallback callback) {
    if (listenFd != -1) {
        std::lock_guard<std::mutex> lock(socketDataMutex);
        if (socketDataMap.count(listenFd)) {
            socketDataMap[listenFd]->acceptCallback = callback;
            // Listen socket is always monitored for EPOLLIN in initialize, so no need to add again.
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
void LinuxAsyncNetworkInterface::doAsyncSend(const std::vector<char>& data, SendCallback callback) {
    if (clientFd == -1) {
        Logger::log("Error: asyncSend called on an invalid socket.");
        callback(0);
        return;
    }

    std::lock_guard<std::mutex> lock(socketDataMutex);
    auto it = socketDataMap.find(clientFd);
    if (it == socketDataMap.end()) {
         Logger::log("Error: asyncSend socket data not found.");
         callback(0);
         return;
    }
    auto& socketData = it->second;
    socketData->sendData.insert(socketData->sendData.end(), data.begin(), data.end());
    socketData->sendCallback = callback;

    // Enable EPOLLOUT to get notified when we can write
    if (!(socketData->currentEvents & EPOLLOUT)) {
        socketData->currentEvents |= EPOLLOUT;
        addFdToEpoll(clientFd, socketData->currentEvents, socketData.get());
    }
}

/**
 * @brief Asynchronously receives data.
 * @param bufferSize The size of the buffer to use.
 * @param callback The function to call upon completion.
 */
void LinuxAsyncNetworkInterface::doAsyncReceive(size_t bufferSize, RecvCallback callback) {
    if (clientFd == -1) {
        Logger::log("Error: asyncReceive called on an invalid socket.");
        callback({}, 0);
        return;
    }

    std::lock_guard<std::mutex> lock(socketDataMutex);
    auto it = socketDataMap.find(clientFd);
    if (it == socketDataMap.end()) {
         Logger::log("Error: asyncReceive socket data not found.");
         callback({}, 0);
         return;
    }
    auto& socketData = it->second;
    socketData->recvCallback = callback;
    // Resize buffer if needed, or just ensure it's large enough.
    // We will resize it after reading to reflect actual bytes read.
    if (socketData->buffer.size() < bufferSize) {
        socketData->buffer.resize(bufferSize);
    }

    // Enable EPOLLIN to get notified when data is available
    if (!(socketData->currentEvents & EPOLLIN)) {
        socketData->currentEvents |= EPOLLIN;
        addFdToEpoll(clientFd, socketData->currentEvents, socketData.get());
    }
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
    if (epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &event) == -1) {
        if (errno == ENOENT) { // If it does not exist, add it.
             if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event) == -1) {
                Logger::log("Error: epoll_ctl(ADD) failed for fd " + std::to_string(fd) + ": " + std::string(strerror(errno)));
             }
        } else {
            Logger::log("Error: epoll_ctl(MOD) failed for fd " + std::to_string(fd) + ": " + std::string(strerror(errno)));
        }
    }
}

/**
 * @brief Removes a file descriptor from the epoll set.
 * @param fd The file descriptor to remove.
 */
void LinuxAsyncNetworkInterface::removeFdFromEpoll(int fd) {
    if (epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        // ENOENT is fine (maybe already closed)
        if (errno != ENOENT) {
             Logger::log("Warning: epoll_ctl(DEL) failed for fd " + std::to_string(fd) + ": " + std::string(strerror(errno)));
        }
    }
    std::lock_guard<std::mutex> lock(socketDataMutex);
    socketDataMap.erase(fd);
}

/**
 * @brief The main worker thread function for processing epoll events.
 */
void LinuxAsyncNetworkInterface::epollWorkerThread() {
    Logger::log("Info: Epoll worker thread starting.");
    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (running) {
        int numEvents = epoll_wait(epollFd, events, MAX_EVENTS, 500); // 500ms timeout check running
        if (!running) break;

        if (numEvents == -1) {
            if (errno == EINTR) continue; // Interrupted, safe to continue.
            Logger::log("Error: epoll_wait failed: " + std::string(strerror(errno)));
            break;
        }

        for (int i = 0; i < numEvents; ++i) {
            // CAUTION: 'data' is a pointer to the SocketData managed by std::unique_ptr in socketDataMap.
            // If the map entry is erased, this pointer becomes invalid.
            SocketData* data = static_cast<SocketData*>(events[i].data.ptr);
            if (!data) continue;

            // Handle Errors
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                // If it's a connect operation, we might have error in SO_ERROR
                if (data->operationType == LinuxOperationType::Connect) {
                    // Handled in EPOLLOUT check below usually, but check here too
                } else {
                    Logger::log("Error: Epoll error or hang-up on fd: " + std::to_string(data->fd));

                    // Capture necessary data before potential deletion
                    RecvCallback recvCb = data->recvCallback;
                    ConnectCallback connectCb = data->connectCallback;
                    AcceptCallback acceptCb = data->acceptCallback;
                    int fd = data->fd;

                    if (recvCb) recvCb({}, 0); // Callback with 0 bytes to signal closed/error
                    if (connectCb) connectCb(false);
                    if (acceptCb) acceptCb(false, "", 0);

                    ::close(fd);
                    removeFdFromEpoll(fd);
                    continue;
                }
            }

            // --- Handle Server Accept (EPOLLIN) ---
            if (data->fd == listenFd) {
                if (events[i].events & EPOLLIN) {
                    sockaddr_in clientAddr;
                    socklen_t clientLen = sizeof(clientAddr);
                    int connFd = ::accept(listenFd, (struct sockaddr*)&clientAddr, &clientLen);
                    if (connFd != -1) {
                        setNonBlocking(connFd);

                        clientFd = connFd; // Store accepted FD

                        auto clientData = std::make_unique<SocketData>();
                        clientData->fd = connFd;
                        clientData->operationType = LinuxOperationType::Recv;
                        clientData->currentEvents = 0;

                        // Capture callback to invoke outside lock if needed, but here it's fine.
                        AcceptCallback acceptCb;
                        {
                            std::lock_guard<std::mutex> lock(socketDataMutex);
                            if (socketDataMap.count(listenFd)) {
                                acceptCb = socketDataMap[listenFd]->acceptCallback;
                            }
                            socketDataMap[connFd] = std::move(clientData);
                        }

                        char ipStr[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
                        int port = ntohs(clientAddr.sin_port);

                        if (acceptCb) {
                            acceptCb(true, std::string(ipStr), port);
                        }
                    } else {
                         if (errno != EAGAIN && errno != EWOULDBLOCK) {
                             Logger::log("Error: accept failed: " + std::string(strerror(errno)));
                         }
                    }
                }
                continue;
            }

            // --- Handle Client Connect (EPOLLOUT) ---
            if (data->operationType == LinuxOperationType::Connect) {
                if (events[i].events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) {
                    int error = 0;
                    socklen_t len = sizeof(error);
                    if (getsockopt(data->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                         error = errno;
                    }

                    // Capture callback
                    ConnectCallback connectCb = data->connectCallback;
                    int fd = data->fd;

                    // Update state inside lock
                    {
                         std::lock_guard<std::mutex> lock(socketDataMutex);
                         // Re-check validity if needed
                         if (socketDataMap.count(fd)) {
                             auto& sd = socketDataMap[fd];
                             sd->currentEvents &= ~EPOLLOUT;
                             addFdToEpoll(fd, sd->currentEvents, sd.get());

                             if (error == 0) {
                                 sd->operationType = LinuxOperationType::Recv;
                             }
                         }
                    }

                    if (error != 0) {
                        Logger::log("Error: Async connect failed: " + std::string(strerror(error)));
                        if (connectCb) connectCb(false);
                    } else {
                        if (connectCb) connectCb(true);
                    }
                    continue;
                }
            }

            // --- Handle Send (EPOLLOUT) ---
            if (events[i].events & EPOLLOUT) {
                SendCallback callbackToCall = nullptr;
                size_t totalBytesSent = 0;

                {
                    std::lock_guard<std::mutex> lock(socketDataMutex);
                    // Check if we still have data to send
                    if (!data->sendData.empty()) {
                        int sent = ::send(data->fd, data->sendData.data(), data->sendData.size(), MSG_NOSIGNAL);
                        if (sent >= 0) {
                             totalBytesSent = sent;
                             data->sendData.erase(data->sendData.begin(), data->sendData.begin() + sent);
                        } else {
                             if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                 Logger::log("Error: send failed: " + std::string(strerror(errno)));
                                 // Handle error (close?)
                             }
                        }
                    }

                    // If queue empty, stop listening for EPOLLOUT
                    if (data->sendData.empty()) {
                        data->currentEvents &= ~EPOLLOUT;
                        addFdToEpoll(data->fd, data->currentEvents, data);
                        // Save callback to call outside lock
                        callbackToCall = data->sendCallback;
                    }
                }

                if (totalBytesSent > 0 && callbackToCall) {
                    callbackToCall(totalBytesSent);
                }
            }

            // --- Handle Receive (EPOLLIN) ---
            if (events[i].events & EPOLLIN) {
                RecvCallback callbackToCall = nullptr;
                std::vector<char> receivedData;
                bool connectionClosed = false;
                int fd = data->fd;

                {
                    std::lock_guard<std::mutex> lock(socketDataMutex);

                    // Double check if data is still valid in map (paranoid check)
                    // If removeFdFromEpoll was called, we wouldn't be here, but good practice?
                    // data pointer is from event, so it should be valid unless we removed it in previous iteration?
                    // We only process one event per FD per iteration effectively?

                    // Use a temporary buffer to read
                    std::vector<char> tempBuf(65536); // 64KB read
                    int bytesRead = ::recv(data->fd, tempBuf.data(), tempBuf.size(), 0);

                    if (bytesRead > 0) {
                        tempBuf.resize(bytesRead);
                        receivedData = std::move(tempBuf);
                        callbackToCall = data->recvCallback;

                        // Disable EPOLLIN until next asyncReceive call
                        data->currentEvents &= ~EPOLLIN;
                        addFdToEpoll(data->fd, data->currentEvents, data);

                    } else if (bytesRead == 0) {
                        // Connection closed
                        Logger::log("Info: Connection closed by peer.");
                        callbackToCall = data->recvCallback;
                        connectionClosed = true;
                        // We do NOT call removeFdFromEpoll here to avoid deadlock.
                        // We will do it outside the lock.
                    } else {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            Logger::log("Error: recv failed: " + std::string(strerror(errno)));
                        }
                    }
                } // Unlock mutex

                if (connectionClosed) {
                    ::close(fd);
                    removeFdFromEpoll(fd); // Now safe to call (it locks mutex internally)
                    if (callbackToCall) callbackToCall({}, 0);
                } else if (callbackToCall && !receivedData.empty()) {
                    callbackToCall(receivedData, receivedData.size());
                }
            }
        }
    }
    Logger::log("Info: Epoll worker thread stopping.");
}
#endif // !_WIN32

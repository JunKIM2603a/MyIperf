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
        callback(0);
        return;
    }

    std::lock_guard<std::mutex> lock(socketDataMutex);
    auto& socketData = socketDataMap[clientFd];
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
        callback({}, 0);
        return;
    }

    std::lock_guard<std::mutex> lock(socketDataMutex);
    auto& socketData = socketDataMap[clientFd];
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
 * @brief Removes a file descriptor from the epoll set.
 * @param fd The file descriptor to remove.
 */
void LinuxAsyncNetworkInterface::removeFdFromEpoll(int fd) {
    if (epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        Logger::log("Warning: epoll_ctl(DEL) failed for fd " + std::to_string(fd) + ": " + std::string(strerror(errno)));
    }
    std::lock_guard<std::mutex> lock(socketDataMutex);
    socketDataMap.erase(fd);
}

/**
 * @brief The main worker thread function for processing epoll events.
 */
void LinuxAsyncNetworkInterface::epollWorkerThread() {
    Logger::log("Info: Epoll worker thread starting.");
    const int MAX_EVENTS = 10;
    epoll_event events[MAX_EVENTS];

    while (running) {
        int numEvents = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        if (!running) break;

        if (numEvents == -1) {
            if (errno == EINTR) continue; // Interrupted, safe to continue.
            Logger::log("Error: epoll_wait failed: " + std::string(strerror(errno)));
            break;
        }

        for (int i = 0; i < numEvents; ++i) {
            SocketData* data = static_cast<SocketData*>(events[i].data.ptr);
            if (!data) continue;

            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                Logger::log("Error: Epoll error or hang-up on fd: " + std::to_string(data->fd));
                // Notify the application of the error.
                if (data->recvCallback) data->recvCallback({}, 0);
                if (data->sendCallback) data->sendCallback(0);
                if (data->connectCallback) data->connectCallback(false);
                if (data->acceptCallback) data->acceptCallback(false, "", 0);
                removeFdFromEpoll(data->fd);
                ::close(data->fd);
                continue;
            }

            // Handle different event types based on the operation stored in SocketData.
            // This part needs to be carefully implemented to handle state transitions correctly.
        }
    }
    Logger::log("Info: Epoll worker thread stopping.");
}
#endif // !_WIN32

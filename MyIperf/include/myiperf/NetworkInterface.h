// NetworkInterface.h
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <coroutine>

/**
 * Backend callbacks are implementation details used to bridge native async I/O
 * completion events into coroutine awaiters.
 */
using RecvCallback = std::function<void(const std::vector<char>& data, size_t bytesReceived)>;
using SendCallback = std::function<void(size_t bytesSent)>;
using ConnectCallback = std::function<void(bool success)>;
using AcceptCallback = std::function<void(bool success, const std::string& clientIP, int clientPort)>;

/**
 * @class NetworkInterface
 * @brief An abstract base class defining the interface for network operations.
 *
 * This class provides a pure virtual interface for platform-specific network
 * implementations (e.g., WinIOCP, Linux epoll). It defines asynchronous
 * methods for network communication, ensuring that the core application
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
     * @brief Prepares the interface to accept server-side connections.
     * @param ip The IP address to bind to.
     * @param port The port to listen on.
     * @return True if the server endpoint is ready, false otherwise.
     */
    virtual bool prepareServer(const std::string& ip, int port) = 0;

    /**
     * @brief Closes the network connection and cleans up resources.
     */
    virtual void close() = 0;

    // --- Coroutine Awaitables ---

    struct ConnectAwaiter {
        NetworkInterface* net;
        std::string ip;
        int port;
        bool result = false;

        ConnectAwaiter(NetworkInterface* n, std::string i, int p)
            : net(n), ip(std::move(i)), port(p) {}

        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> h) {
            net->doAsyncConnect(ip, port, [this, h](bool success) mutable {
                result = success;
                h.resume();
            });
        }
        bool await_resume() { return result; }
    };

    struct AcceptResult {
        bool success;
        std::string clientIP;
        int clientPort;
    };

    struct AcceptAwaiter {
        NetworkInterface* net;
        AcceptResult result;

        AcceptAwaiter(NetworkInterface* n) : net(n) {}

        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> h) {
            net->doAsyncAccept([this, h](bool success, const std::string& ip, int port) mutable {
                result = {success, ip, port};
                h.resume();
            });
        }
        AcceptResult await_resume() { return result; }
    };

    struct SendAwaiter {
        NetworkInterface* net;
        std::vector<char> data;
        size_t bytesSent = 0;

        SendAwaiter(NetworkInterface* n, std::vector<char> d)
            : net(n), data(std::move(d)) {}

        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> h) {
            net->doAsyncSend(data, [this, h](size_t sent) mutable {
                bytesSent = sent;
                h.resume();
            });
        }
        size_t await_resume() { return bytesSent; }
    };

    struct ReceiveResult {
        std::vector<char> data;
        size_t bytesReceived;
    };

    struct ReceiveAwaiter {
        NetworkInterface* net;
        size_t bufferSize;
        ReceiveResult result;

        ReceiveAwaiter(NetworkInterface* n, size_t size)
            : net(n), bufferSize(size) {}

        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> h) {
            net->doAsyncReceive(bufferSize, [this, h](const std::vector<char>& d, size_t b) mutable {
                result = {d, b};
                h.resume();
            });
        }
        ReceiveResult await_resume() { return result; }
    };

    // --- Coroutine Helper Methods ---

    ConnectAwaiter connect(const std::string& ip, int port) {
        return ConnectAwaiter(this, ip, port);
    }

    AcceptAwaiter accept() {
        return AcceptAwaiter(this);
    }

    SendAwaiter send(const std::vector<char>& data) {
        return SendAwaiter(this, data);
    }

    ReceiveAwaiter receive(size_t bufferSize) {
        return ReceiveAwaiter(this, bufferSize);
    }

protected:
    // Callback-based hooks implemented by platform backends.
    virtual void doAsyncConnect(const std::string& ip, int port, ConnectCallback callback) = 0;
    virtual void doAsyncAccept(AcceptCallback callback) = 0;
    virtual void doAsyncSend(const std::vector<char>& data, SendCallback callback) = 0;
    virtual void doAsyncReceive(size_t bufferSize, RecvCallback callback) = 0;
};

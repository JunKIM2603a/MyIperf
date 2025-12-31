#pragma once

#include "NetworkInterface.h"
#include <coroutine>
#include <iostream>
#include <string>
#include <vector>

// --- Awaiter for Async Connection ---
struct ConnectAwaiter {
    NetworkInterface* net;
    std::string ip;
    int port;
    bool result = false;

    ConnectAwaiter(NetworkInterface* n, std::string i, int p)
        : net(n), ip(std::move(i)), port(p) {}

    bool await_ready() { return false; }

    void await_suspend(std::coroutine_handle<> h) {
        net->asyncConnect(ip, port, [this, h](bool success) mutable {
            result = success;
            h.resume();
        });
    }

    bool await_resume() { return result; }
};

// --- Awaiter for Async Accept ---
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
        net->asyncAccept([this, h](bool success, const std::string& ip, int port) mutable {
            result = {success, ip, port};
            h.resume();
        });
    }

    AcceptResult await_resume() { return result; }
};

// --- Awaiter for Async Send ---
struct SendAwaiter {
    NetworkInterface* net;
    std::vector<char> data;
    size_t bytesSent = 0;

    SendAwaiter(NetworkInterface* n, std::vector<char> d)
        : net(n), data(std::move(d)) {}

    bool await_ready() { return false; }

    void await_suspend(std::coroutine_handle<> h) {
        net->asyncSend(data, [this, h](size_t sent) mutable {
            bytesSent = sent;
            h.resume();
        });
    }

    size_t await_resume() { return bytesSent; }
};

// --- Helper functions to make the syntax nicer ---

inline ConnectAwaiter co_connect(NetworkInterface* net, const std::string& ip, int port) {
    return ConnectAwaiter(net, ip, port);
}

inline AcceptAwaiter co_accept(NetworkInterface* net) {
    return AcceptAwaiter(net);
}

inline SendAwaiter co_send(NetworkInterface* net, const std::vector<char>& data) {
    return SendAwaiter(net, data);
}

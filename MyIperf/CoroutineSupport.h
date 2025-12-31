#pragma once

#include <coroutine>
#include <exception>
#include <iostream>

/**
 * @brief A minimal Task class for C++20 coroutines.
 * Allows a coroutine to be awaited by another coroutine.
 */
struct Task {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        Task get_return_object() {
            return Task(handle_type::from_promise(*this));
        }
        std::suspend_always initial_suspend() { return {}; }
        struct FinalSuspend {
             bool await_ready() const noexcept { return false; }
             void await_suspend(handle_type h) noexcept {
                 // Resume the waiting coroutine if one exists
                 if (h.promise().continuation) {
                     h.promise().continuation.resume();
                 }
             }
             void await_resume() noexcept {}
        };
        FinalSuspend final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {
            std::terminate(); // Or store exception to rethrow
        }

        std::coroutine_handle<> continuation;
    };

    handle_type coro;

    Task(handle_type h) : coro(h) {}
    ~Task() {
        if (coro) coro.destroy();
    }

    // Move-only
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : coro(other.coro) {
        other.coro = nullptr;
    }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro) coro.destroy();
            coro = other.coro;
            other.coro = nullptr;
        }
        return *this;
    }

    // Make Task awaitable
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> h) {
        coro.promise().continuation = h;
        coro.resume();
    }
    void await_resume() {}

    // Allow starting it manually (e.g., from non-coroutine code)
    void start() {
        if (coro && !coro.done()) {
            coro.resume();
        }
    }

    bool is_done() const {
        return coro && coro.done();
    }
};

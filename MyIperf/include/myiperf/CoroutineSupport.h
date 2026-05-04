#pragma once

#include <coroutine>
#include <exception>
#include <iostream>
#include <chrono>
#include <thread>

/**
 * @brief MyIperf에서 사용하는 최소 C++20 coroutine Task.
 *
 * 이 Task는 블로그 예제처럼 main()에서 직접 resume()만 호출하는 가장 단순한
 * coroutine 객체가 아니다. MyIperf는 아래처럼 Task 안에서 다른 Task를
 * 기다리는 중첩 구조를 사용한다.
 *
 *   co_await connectAndHandshake();
 *   co_await runClientToServerPhase();
 *
 * 이런 패턴에서는 자식 Task가 "나를 기다리는 부모 coroutine"을 기억해야 한다.
 * 그 부모 handle을 continuation에 저장하고, 자식이 끝날 때 final_suspend()에서
 * 다시 반환해서 부모 coroutine이 자동으로 이어서 실행되게 한다.
 *
 * root Task는 start()로 명시적으로 시작한다. child Task는 부모가 co_await할 때
 * 시작된다.
 */
struct Task {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct Awaiter {
        handle_type child;

        // Task를 기다릴 때는 항상 한 번 suspend 경로로 들어간다.
        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) const noexcept {
            // parent = 이 Task를 co_await하다가 멈추는 coroutine.
            // child  = 이 Task가 감싸고 있는 coroutine.
            //
            // child가 끝났을 때 parent를 다시 깨워야 하므로 parent handle을
            // child promise의 continuation에 저장한다. 그 뒤 child를 시작/재개한다.
            // Return child instead of calling child.resume() here. The runtime
            // will resume child after parent is fully suspended.
            child.promise().continuation = parent;
            return child;
        }

        void await_resume() const {
            // child Task가 끝나고 parent가 다시 깨어난 뒤 호출된다.
            // child에서 발생한 예외를 여기서 다시 던져 co_await 주변 try/catch가
            // 자연스럽게 동작하게 한다.
            if (child && child.promise().exception) {
                std::rethrow_exception(child.promise().exception);
            }
        }
    };

    struct promise_type {
        Task get_return_object() {
            // coroutine frame handle을 Task 객체로 감싸서 반환한다.
            // Task를 반환하는 coroutine 함수를 호출하면 frame은 만들어지지만,
            // 아래 initial_suspend() 때문에 함수 본문은 즉시 실행되지 않는다.
            return Task(handle_type::from_promise(*this));
        }
        // 처음에는 정지 상태로 시작한다.
        // 그래서 TestController는 root Task를 먼저 보관한 뒤 Task::start()로
        // 나중에 시작할 수 있고, child Task는 부모가 co_await childTask()에
        // 도달했을 때만 시작된다.
        std::suspend_always initial_suspend() { return {}; }

        struct FinalSuspend {
             bool await_ready() const noexcept { return false; }
             std::coroutine_handle<> await_suspend(handle_type h) noexcept {
                 // 자식 Task는 함수 본문이 모두 끝난 뒤 final_suspend()에 도달한다.
                 // 이 Task를 기다리던 부모 coroutine이 있으면, 그 부모 handle을
                 // 반환해서 부모 쪽 실행이 이어지게 한다.
                 if (h.promise().continuation) {
                     return h.promise().continuation;
                 }
                 // root Task는 보통 부모 continuation이 없다.
                 // noop coroutine을 반환한다는 것은 "다시 깨울 대상이 없다"는 뜻이다.
                 return std::noop_coroutine();
             }
             void await_resume() noexcept {}
        };
        FinalSuspend final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {
            // 예외를 promise 안에 저장한다.
            // 이 Task를 기다리던 부모는 Task::await_resume()에서 이 예외를 다시 던진다.
            exception = std::current_exception();
        }

        // 이 Task를 기다리고 있는 부모 coroutine handle.
        //
        // co_await childTask()를 만나면 Task::await_suspend(parentHandle)가
        // parentHandle을 여기에 저장한다. child가 끝나면 final_suspend()가 이
        // handle을 반환하고, 부모는 co_await 다음 줄부터 다시 실행된다.
        std::coroutine_handle<> continuation{nullptr};
        std::exception_ptr exception{nullptr};
    };

    handle_type coro;

    Task(handle_type coroutine) : coro(coroutine) {}
    ~Task() {
        if (coro) coro.destroy();
    }

    // coroutine handle 소유권이 중복되면 destroy()가 중복 호출될 수 있으므로
    // Task는 복사 금지, 이동만 허용한다.
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

    // Task는 coroutine frame을 소유하고, Awaiter는 co_await 동작을 담당한다.
    // lvalue/rvalue Task 모두 같은 방식으로 await될 수 있게 두 overload를 둔다.
    Awaiter operator co_await() & noexcept {
        return Awaiter{coro};
    }

    Awaiter operator co_await() && noexcept {
        return Awaiter{coro};
    }

    // coroutine이 아닌 코드에서 root Task를 직접 시작할 때 사용한다.
    void start() {
        if (coro && !coro.done()) {
            coro.resume();
        }
    }

    bool is_done() const {
        return coro && coro.done();
    }
};

/**
 * @brief 지정한 시간 동안 coroutine을 멈추는 단순 delay awaiter.
 *
 * 이 구현은 확장성보다 이해하기 쉬운 구조를 우선한다.
 * delay를 호출할 때마다 detached sleeper thread를 하나 만들고, 시간이 지나면
 * 그 thread가 coroutine을 다시 resume한다. 현재 send interval 용도로는
 * 따라가기 쉽지만, 대량 timer scheduler로 쓰기 좋은 구조는 아니다.
 */
struct DelayAwaiter {
    std::chrono::steady_clock::time_point endTime;

    DelayAwaiter(std::chrono::milliseconds duration)
        : endTime(std::chrono::steady_clock::now() + duration) {}

    bool await_ready() const noexcept {
        // 목표 시간이 이미 지났다면 멈추지 않고 바로 통과한다.
        return std::chrono::steady_clock::now() >= endTime;
    }

    void await_suspend(std::coroutine_handle<> h) const {
        // 현재 coroutine은 멈춘다. detached thread가 목표 시간까지 sleep한 뒤,
        // 같은 coroutine handle을 resume한다.
        std::thread([h, endTime = this->endTime]() {
            auto now = std::chrono::steady_clock::now();
            if (endTime > now) {
                std::this_thread::sleep_until(endTime);
            }
            if (!h.done()) {
                h.resume();
            }
        }).detach();
    }

    void await_resume() const noexcept {}
};

/**
 * @brief co_await delay(...) 형태로 쓰기 위한 helper 함수.
 * @param duration 멈출 시간.
 * @return co_await 가능한 DelayAwaiter.
 */
inline DelayAwaiter delay(std::chrono::milliseconds duration) {
    return DelayAwaiter(duration);
}

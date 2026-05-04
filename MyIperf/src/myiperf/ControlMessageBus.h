#pragma once

#include "myiperf/Protocol.h"

#include <atomic>
#include <coroutine>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// ControlMessageBus는 "코루틴 대기 저장소"입니다.
//
// session coroutine:
//   co_await waitFor(TEST_FIN)
//     -> await_ready(): 이미 도착해 bufferedMessages에 있으면 바로 통과
//     -> await_suspend(): 아직 없으면 continuation을 pendingWaits에 등록
//
// receiver thread:
//   deliver(TEST_FIN)
//     -> pendingWaits에서 continuation을 꺼냄
//     -> continuation.resume()으로 session coroutine을 깨움
//
// 즉 map이 코루틴을 멈추는 것은 아닙니다. co_await가 멈추고,
// 이 클래스는 나중에 다시 깨울 continuation을 보관합니다.
class ControlMessageBus {
public:
  struct Message {
    PacketHeader header{};
    std::vector<char> payload;
  };

  class Awaiter {
  public:
    Awaiter(ControlMessageBus& bus, MessageType type, int timeoutMs);
    Awaiter(const Awaiter&) = delete;
    Awaiter& operator=(const Awaiter&) = delete;
    ~Awaiter();

    bool await_ready();
    bool await_suspend(std::coroutine_handle<> handle);
    Message await_resume();

  private:
    friend class ControlMessageBus;

    ControlMessageBus& bus;
    MessageType type;
    int timeoutMs;
    std::thread timerThread;
    std::atomic<bool> cancelTimer{false};
    std::coroutine_handle<> continuation{nullptr};
    Message message;
    bool timedOut = false;
    bool cancelled = false;
  };

  Awaiter waitFor(MessageType type, int timeoutMs = 5000);
  void deliver(const PacketHeader& header, const std::vector<char>& payload);
  void clear();
  void cancelAll();

private:
  bool tryTakeBufferedMessage(MessageType type, Message& message);
  void registerPendingWait(MessageType type, Awaiter& awaiter);
  std::coroutine_handle<> takeContinuationForMessage(const PacketHeader& header,
                                                     const std::vector<char>& payload);

  std::mutex mutex;
  std::map<MessageType, Awaiter*> pendingWaits;
  std::map<MessageType, std::queue<Message>> bufferedMessages;
};

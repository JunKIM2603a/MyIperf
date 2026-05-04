#include "ControlMessageBus.h"

#include "ControlProtocol.h"

#include <chrono>
#include <stdexcept>
#include <string>

ControlMessageBus::Awaiter::Awaiter(ControlMessageBus& bus,
                                    MessageType type,
                                    int timeoutMs)
    : bus(bus), type(type), timeoutMs(timeoutMs) {}

ControlMessageBus::Awaiter::~Awaiter() {
  cancelTimer = true;
  if (timerThread.joinable()) {
    if (timerThread.get_id() == std::this_thread::get_id()) {
      timerThread.detach();
    } else {
      timerThread.join();
    }
  }
}

bool ControlMessageBus::Awaiter::await_ready() {
  // co_await가 먼저 호출하는 단계입니다.
  // 이미 같은 타입의 메시지가 먼저 도착해 버퍼에 있으면,
  // 코루틴을 멈추지 않고 await_resume()으로 바로 이어집니다.
  return bus.tryTakeBufferedMessage(type, message);
}

bool ControlMessageBus::Awaiter::await_suspend(std::coroutine_handle<> h) {
  // await_ready()가 false일 때만 호출됩니다.
  // 코루틴을 멈추는 직접 원인은 co_await이고, 여기서는 나중에
  // deliver(type)가 왔을 때 다시 깨울 continuation을 등록합니다.
  continuation = h;
  if (timeoutMs > 0) {
    timerThread = std::thread([this, h]() {
    const auto start = std::chrono::steady_clock::now();
    while (!cancelTimer) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      if (elapsed.count() >= timeoutMs) {
        std::coroutine_handle<> continuationToResume{nullptr};
        {
          std::lock_guard<std::mutex> lock(bus.mutex);
          auto it = bus.pendingWaits.find(type);
          if (it != bus.pendingWaits.end() && it->second == this) {
            timedOut = true;
            continuationToResume = h;
            bus.pendingWaits.erase(it);
          }
        }
        if (continuationToResume && !continuationToResume.done()) {
          continuationToResume.resume();
        }
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    });
  }

  {
    std::lock_guard<std::mutex> lock(bus.mutex);
    auto& queue = bus.bufferedMessages[type];
    if (!queue.empty()) {
      message = std::move(queue.front());
      queue.pop();
      cancelTimer = true;
      return false;
    }
    bus.pendingWaits[type] = this;
  }

  return true;
}

ControlMessageBus::Message ControlMessageBus::Awaiter::await_resume() {
  // resume()으로 코루틴이 다시 깨어난 뒤 호출됩니다.
  // 정상 수신이면 message를 돌려주고, stop/timeout이면 예외로 세션 흐름을 끝냅니다.
  if (cancelled) {
    throw std::runtime_error("Operation cancelled.");
  }
  if (timedOut) {
    throw std::runtime_error("Timeout waiting for message: " +
                             std::string(ControlProtocol::messageTypeToString(type)));
  }
  return std::move(message);
}

ControlMessageBus::Awaiter ControlMessageBus::waitFor(MessageType type,
                                                      int timeoutMs) {
  return Awaiter(*this, type, timeoutMs);
}

void ControlMessageBus::deliver(const PacketHeader& header,
                                const std::vector<char>& payload) {
  if (header.messageType == MessageType::DATA_PACKET) {
    return;
  }

  // PacketReceiver 쪽 스레드에서 들어오는 입구입니다.
  // lock을 잡고 continuation만 꺼낸 뒤, 실제 resume()은 lock 밖에서 합니다.
  // 재개된 코루틴이 다시 ControlMessageBus를 만져도 교착되지 않게 하기 위해서입니다.
  auto continuationToResume = takeContinuationForMessage(header, payload);
  if (continuationToResume && !continuationToResume.done()) {
    continuationToResume.resume();
  }
}

void ControlMessageBus::clear() {
  std::lock_guard<std::mutex> lock(mutex);
  pendingWaits.clear();
  bufferedMessages.clear();
}

void ControlMessageBus::cancelAll() {
  std::vector<std::coroutine_handle<>> continuations;
  {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& [_, awaiter] : pendingWaits) {
      if (awaiter) {
        awaiter->cancelled = true;
        if (awaiter->continuation) {
          continuations.push_back(awaiter->continuation);
        }
      }
    }
    pendingWaits.clear();
    bufferedMessages.clear();
  }

  for (auto continuation : continuations) {
    if (continuation && !continuation.done()) {
      continuation.resume();
    }
  }
}

bool ControlMessageBus::tryTakeBufferedMessage(MessageType type,
                                               Message& message) {
  std::lock_guard<std::mutex> lock(mutex);
  auto& queue = bufferedMessages[type];
  if (queue.empty()) {
    return false;
  }

  message = std::move(queue.front());
  queue.pop();
  return true;
}

void ControlMessageBus::registerPendingWait(MessageType type,
                                            Awaiter& awaiter) {
  std::lock_guard<std::mutex> lock(mutex);
  pendingWaits[type] = &awaiter;
}

std::coroutine_handle<> ControlMessageBus::takeContinuationForMessage(
    const PacketHeader& header,
    const std::vector<char>& payload) {
  std::lock_guard<std::mutex> lock(mutex);
  auto it = pendingWaits.find(header.messageType);
  if (it == pendingWaits.end() || !it->second) {
    bufferedMessages[header.messageType].push({header, payload});
    return nullptr;
  }

  it->second->message = {header, payload};
  auto continuationToResume = it->second->continuation;
  pendingWaits.erase(it);
  return continuationToResume;
}

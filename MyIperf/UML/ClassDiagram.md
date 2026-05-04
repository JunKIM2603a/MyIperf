# 클래스 다이어그램

이 문서는 MyIperf의 전체 객체 관계를 코드 이해용으로 요약한다. 핵심은
`TestController`가 실행에 필요한 객체를 소유하고, session 객체들은
`TestSessionContext`를 통해 그 객체들을 참조로 빌려 쓴다는 점이다.

## 관계 표기

- `*--`: 소유 관계. `unique_ptr` 또는 값 멤버처럼 lifetime을 관리한다.
- `-->`: 참조 또는 raw pointer 관계. 객체를 빌려 쓰며 소유하지 않는다.
- `..>`: 생성, 호출, 일시적 의존 관계.
- `Task`는 coroutine frame handle을 소유한다.
- `Awaiter`는 handle을 빌려 `co_await` 중인 parent coroutine과 child coroutine을 연결한다.

## 전체 객체 소유/참조 구조

```mermaid
classDiagram
    direction LR

    class main {
        +main()
    }

    class CLIHandler {
        -TestController& testController
        +CLIHandler(TestController& controller)
        +run(argc, argv) bool
        -parseArgs(argc, argv) Config
        +printHelp() void
    }

    class TestController {
        -unique_ptr~NetworkInterface~ networkInterface
        -unique_ptr~PacketGenerator~ packetGenerator
        -unique_ptr~PacketReceiver~ packetReceiver
        -unique_ptr~ControlMessageBus~ controlMessages
        -unique_ptr~ControlChannel~ controlChannel
        -Config currentConfig
        -Task mainTestTask
        -atomic~State~ currentState
        +startTest(Config) void
        +stopTest() void
        +getTestCompletionFuture() future~void~
        -runTestCoroutine() Task
        -transitionTo(State) void
    }

    class TestSessionContext {
        +Config& config
        +NetworkInterface& network
        +PacketGenerator& generator
        +PacketReceiver& receiver
        +ControlChannel& control
        +TestStats& clientStatsPhase1
        +TestStats& serverStatsPhase1
        +TestStats& clientStatsPhase2
        +TestStats& serverStatsPhase2
        +function transitionTo
    }

    class ClientTestSession {
        -TestSessionContext& context
        +run() Task
        -connectAndHandshake() Task
        -runClientToServerPhase() Task
        -runServerToClientPhase() Task
    }

    class ServerTestSession {
        -TestSessionContext& context
        +run() Task
        -acceptAndReceiveConfig() Task
        -runClientToServerPhase() Task
        -runServerToClientPhase() Task
    }

    class NetworkInterface {
        <<interface>>
        +initialize(ip, port) bool
        +prepareServer(ip, port) bool
        +close() void
        +connect(ip, port) ConnectAwaiter
        +accept() AcceptAwaiter
        +send(data) SendAwaiter
        +receive(size) ReceiveAwaiter
        #doAsyncConnect(ip, port, callback) void
        #doAsyncAccept(callback) void
        #doAsyncSend(data, callback) void
        #doAsyncReceive(size, callback) void
    }

    class PacketGenerator {
        -NetworkInterface* networkInterface
        -Config config
        -atomic~bool~ running
        +sendPackets(Config) Task
        +stop() void
        +resetStats() void
        +getStats() TestStats
    }

    class PacketReceiver {
        -NetworkInterface* networkInterface
        -PacketStreamParser parser
        -PacketReceiveStats stats
        -unique_ptr~PacketDispatcher~ dispatcher
        -Task receiverTask
        +start(ControlMessageBus) void
        +stop() void
        +resetStats() void
        +getStats() TestStats
        -receiverLoop() Task
    }

    class ControlMessageBus {
        -map pendingWaits
        -map bufferedMessages
        +waitFor(MessageType, timeoutMs) Awaiter
        +deliver(PacketHeader, payload) void
        +clear() void
        +cancelAll() void
    }

    class ControlChannel {
        -NetworkInterface& network
        -ControlMessageBus& messages
        +attachReceiver(PacketReceiver&) void
        +waitFor(MessageType, timeoutMs) Awaiter
        +send(MessageType, payload) Task
        +clear() void
        +cancelAll() void
    }

    main ..> TestController : creates
    main ..> CLIHandler : creates
    CLIHandler --> TestController : reference, no copy

    TestController *-- NetworkInterface : owns unique_ptr
    TestController *-- PacketGenerator : owns unique_ptr
    TestController *-- PacketReceiver : owns unique_ptr
    TestController *-- ControlMessageBus : owns unique_ptr
    TestController *-- ControlChannel : owns unique_ptr
    TestController ..> TestSessionContext : creates in coroutine
    TestController ..> ClientTestSession : creates by mode
    TestController ..> ServerTestSession : creates by mode

    TestSessionContext --> NetworkInterface : reference
    TestSessionContext --> PacketGenerator : reference
    TestSessionContext --> PacketReceiver : reference
    TestSessionContext --> ControlChannel : reference
    TestSessionContext --> Config : reference

    ClientTestSession --> TestSessionContext : reference
    ServerTestSession --> TestSessionContext : reference

    PacketGenerator --> NetworkInterface : raw pointer
    PacketReceiver --> NetworkInterface : raw pointer
    ControlChannel --> NetworkInterface : reference
    ControlChannel --> ControlMessageBus : reference
```

## 네트워크 플랫폼 구현

`NetworkInterface`는 상위 coroutine 코드가 사용하는 공통 API를 제공한다. 실제 I/O는
플랫폼 구현체의 protected backend hook인 `doAsyncXxx`에서 처리한다.

```mermaid
classDiagram
    direction TB

    class NetworkInterface {
        <<interface>>
        +connect(ip, port) ConnectAwaiter
        +accept() AcceptAwaiter
        +send(data) SendAwaiter
        +receive(size) ReceiveAwaiter
        #doAsyncConnect(ip, port, callback) void
        #doAsyncAccept(callback) void
        #doAsyncSend(data, callback) void
        #doAsyncReceive(size, callback) void
    }

    class WinIOCPNetworkInterface {
        -SOCKET listenSocket
        -SOCKET clientSocket
        -HANDLE iocpHandle
        -vector~thread~ workerThreads
        -iocpWorkerThread() void
    }

    class LinuxAsyncNetworkInterface {
        -int listenFd
        -int clientFd
        -int epollFd
        -thread epollThread
        -map socketDataMap
        -epollWorkerThread() void
    }

    class NetworkInterfaceFactory {
        +createNetworkInterface() unique_ptr~NetworkInterface~
    }

    NetworkInterface <|-- WinIOCPNetworkInterface
    NetworkInterface <|-- LinuxAsyncNetworkInterface
    NetworkInterfaceFactory ..> WinIOCPNetworkInterface : Windows
    NetworkInterfaceFactory ..> LinuxAsyncNetworkInterface : Linux
```

## 패킷 수신 처리 구조

`PacketReceiver`는 수신 coroutine lifecycle만 담당한다. byte stream 조립, 검증,
통계 갱신, 제어 메시지 전달은 별도 클래스로 나뉜다.

```mermaid
classDiagram
    direction LR

    class PacketReceiver {
        -NetworkInterface* networkInterface
        -PacketStreamParser parser
        -PacketReceiveStats stats
        -unique_ptr~PacketDispatcher~ dispatcher
        -Task receiverTask
        +start(ControlMessageBus) void
        +stop() void
        -receiverLoop() Task
    }

    class PacketStreamParser {
        -vector buffer
        +append(data, size) void
        +drainPackets() vector~ParsedPacket~
    }

    class PacketReceiveStats {
        +reset() void
        +onDataPacket(header, size, payload) void
        +onChecksumFailure() void
        +snapshot() TestStats
    }

    class PacketDispatcher {
        -ControlMessageBus& messages
        -PacketReceiveStats& stats
        +dispatch(packets) void
    }

    class ControlMessageBus {
        +deliver(PacketHeader, payload) void
    }

    PacketReceiver --> NetworkInterface : receive awaiter
    PacketReceiver *-- PacketStreamParser : value member
    PacketReceiver *-- PacketReceiveStats : value member
    PacketReceiver *-- PacketDispatcher : unique_ptr
    PacketDispatcher --> PacketReceiveStats : reference
    PacketDispatcher --> ControlMessageBus : reference
```

## Coroutine 지원 구조

`Task`는 MyIperf session 흐름을 순차 코드처럼 읽게 해주는 최소 coroutine runtime이다.
`NetworkInterface` awaiter와 `ControlMessageBus::Awaiter`는 외부 이벤트가 완료될 때
저장해 둔 coroutine handle을 `resume()`한다.

```mermaid
classDiagram
    direction LR

    class Task {
        -handle_type coro
        +start() void
        +is_done() bool
        +operator co_await() Awaiter
    }

    class TaskAwaiter {
        -handle_type child
        +await_ready() bool
        +await_suspend(parent) void
        +await_resume() void
    }

    class Promise {
        +initial_suspend()
        +final_suspend()
        +return_void()
        +unhandled_exception()
        +coroutine_handle continuation
        +exception_ptr exception
    }

    class NetworkAwaiter {
        +await_ready() bool
        +await_suspend(handle) void
        +await_resume() result
    }

    class ControlMessageAwaiter {
        -coroutine_handle continuation
        +await_ready() bool
        +await_suspend(handle) void
        +await_resume() Message
    }

    Task *-- Promise : coroutine promise
    Task ..> TaskAwaiter : creates when co_await Task
    TaskAwaiter --> Promise : stores parent continuation
    NetworkAwaiter ..> NetworkInterface : calls doAsyncXxx
    ControlMessageAwaiter --> ControlMessageBus : registers wait
```

## 읽을 때 중요한 점

- `CLIHandler`는 `TestController`를 복사하지 않고 참조한다.
- `TestController`가 네트워크, 송신기, 수신기, 제어 메시지 버스를 소유한다.
- `TestSessionContext`는 소유자가 아니라 session에 필요한 참조를 묶어 전달하는 구조체다.
- `PacketGenerator`와 `PacketReceiver`의 `NetworkInterface*`는 소유권이 없는 raw pointer다.
- `ControlChannel`은 session 코드가 `send`와 `waitFor`를 쉽게 쓰도록 만든 facade다.
- `doAsyncXxx` callback은 platform 내부 구현 세부사항이며, 상위 테스트 흐름은 `co_await` 중심으로 읽힌다.

# 실행 흐름 다이어그램

이 문서는 MyIperf의 전체 실행 흐름과 coroutine 대기 지점을 요약한다. 상위 흐름은
`co_await` 덕분에 순차 코드처럼 보이지만, 실제 네트워크 I/O와 제어 메시지 대기는
awaiter가 coroutine handle을 저장했다가 완료 시점에 `resume()`하는 방식으로 동작한다.

## 프로그램 시작과 테스트 시작

```mermaid
flowchart TD
    A[main] --> B[TestController controller 생성]
    B --> C[CLIHandler cli 생성]
    C --> D[cli.run argc argv]
    D --> E[CLIHandler가 Config 파싱]
    E --> F[TestController.startTest config]
    F --> G[TestController.reset]
    G --> H[currentConfig 저장]
    H --> I[mainTestTask = runTestCoroutine]
    I --> J[mainTestTask.start]
    J --> K[TestSessionContext 생성]
    K --> L{Config mode}
    L -->|CLIENT| M[ClientTestSession 생성]
    L -->|SERVER| N[ServerTestSession 생성]
    M --> O[co_await session.run]
    N --> O
    O --> P[session 완료 또는 예외]
    P --> Q[signalCompletion]
    Q --> R[main thread future wait 해제]
    R --> S[controller.stopTest]
    S --> T[receiver, generator, network 정리]
```

## Client session 흐름

```mermaid
flowchart TD
    A[ClientTestSession.run] --> B[connectAndHandshake]
    B --> C[network.initialize 0.0.0.0:0]
    C --> D[co_await network.connect]
    D --> E[connect 완료 시 coroutine 재개]
    E --> F[control.attachReceiver receiver]
    F --> G[control.send CONFIG_HANDSHAKE]
    G --> H[control.waitFor CONFIG_ACK]
    H --> I[runClientToServerPhase]

    I --> J[generator.sendPackets]
    J --> K[co_await network.send 반복]
    K --> L[control.send TEST_FIN]
    L --> M[control.waitFor TEST_FIN]
    M --> N[control.send STATS_EXCHANGE]
    N --> O[control.waitFor STATS_ACK]
    O --> P[phase 1 통계 저장]
    P --> Q[runServerToClientPhase]

    Q --> R[control.send CLIENT_READY]
    R --> S[receiver.resetStats]
    S --> T[control.waitFor TEST_FIN]
    T --> U[receiver.getStats]
    U --> V[control.send STATS_EXCHANGE]
    V --> W[control.waitFor STATS_ACK]
    W --> X[control.send SHUTDOWN_ACK]
    X --> Y[FINISHED]
```

## Server session 흐름

```mermaid
flowchart TD
    A[ServerTestSession.run] --> B[acceptAndReceiveConfig]
    B --> C[network.prepareServer]
    C --> D[co_await network.accept]
    D --> E[accept 완료 시 coroutine 재개]
    E --> F[control.attachReceiver receiver]
    F --> G[control.waitFor CONFIG_HANDSHAKE]
    G --> H[Config payload 적용]
    H --> I[control.send CONFIG_ACK]
    I --> J[runClientToServerPhase]

    J --> K[receiver.resetStats]
    K --> L[control.waitFor TEST_FIN]
    L --> M[control.send TEST_FIN]
    M --> N[control.waitFor STATS_EXCHANGE]
    N --> O[receiver.getStats]
    O --> P[control.send STATS_ACK]
    P --> Q[runServerToClientPhase]

    Q --> R[control.waitFor CLIENT_READY]
    R --> S[generator.resetStats]
    S --> T[generator.sendPackets]
    T --> U[co_await network.send 반복]
    U --> V[control.send TEST_FIN]
    V --> W[control.waitFor STATS_EXCHANGE]
    W --> X[generator.getStats]
    X --> Y[control.send STATS_ACK]
    Y --> Z[control.waitFor SHUTDOWN_ACK]
    Z --> AA[FINISHED]
```

## 네트워크 awaiter 재개 흐름

```mermaid
sequenceDiagram
    participant Session as Session coroutine
    participant Awaiter as Network Awaiter
    participant Backend as Platform doAsyncXxx
    participant Worker as IOCP or epoll worker

    Session->>Awaiter: co_await network.connect/accept/send/receive
    Awaiter->>Awaiter: await_ready false
    Awaiter->>Backend: await_suspend(handle), doAsyncXxx(callback)
    Note over Session: coroutine suspend
    Backend-->>Worker: native async I/O 등록
    Worker-->>Backend: I/O completion event
    Backend-->>Awaiter: callback(result)
    Awaiter->>Awaiter: result 저장
    Awaiter->>Session: handle.resume()
    Session->>Awaiter: await_resume()
    Awaiter-->>Session: result 반환
```

## Control message 수신/대기 흐름

```mermaid
sequenceDiagram
    participant Receiver as PacketReceiver
    participant Parser as PacketStreamParser
    participant Dispatcher as PacketDispatcher
    participant Bus as ControlMessageBus
    participant Session as Session coroutine

    Session->>Bus: co_await control.waitFor(type)
    Bus->>Bus: buffered message 확인
    alt message already buffered
        Bus-->>Session: await_ready true
    else not arrived yet
        Bus->>Bus: continuation 등록
        Note over Session: coroutine suspend
    end

    Receiver->>Receiver: co_await network.receive
    Receiver->>Parser: append bytes
    Parser-->>Receiver: ParsedPacket 목록
    Receiver->>Dispatcher: dispatch packets
    alt DATA_PACKET
        Dispatcher->>Dispatcher: PacketReceiveStats 갱신
    else control message
        Dispatcher->>Bus: deliver header payload
        Bus->>Session: continuation.resume()
    end
```

## TestController 상태 전이

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> CONNECTING: client startTest
    CONNECTING --> SENDING_CONFIG: connect 성공
    SENDING_CONFIG --> WAITING_FOR_ACK: CONFIG_HANDSHAKE 전송
    WAITING_FOR_ACK --> RUNNING_TEST: CONFIG_ACK 수신

    IDLE --> ACCEPTING: server startTest
    ACCEPTING --> WAITING_FOR_CONFIG: accept 성공
    WAITING_FOR_CONFIG --> RUNNING_TEST: CONFIG_HANDSHAKE 수신

    RUNNING_TEST --> FINISHING: phase 1 송수신 종료
    FINISHING --> EXCHANGING_STATS: TEST_FIN 교환
    EXCHANGING_STATS --> WAITING_FOR_SERVER_FIN: client phase 1 완료
    EXCHANGING_STATS --> WAITING_FOR_CLIENT_READY: server phase 1 완료

    WAITING_FOR_SERVER_FIN --> EXCHANGING_SERVER_STATS: client가 server TEST_FIN 수신
    EXCHANGING_SERVER_STATS --> FINISHED: client가 최종 STATS_ACK 수신 후 SHUTDOWN_ACK 전송

    WAITING_FOR_CLIENT_READY --> RUNNING_SERVER_TEST: server가 CLIENT_READY 수신
    RUNNING_SERVER_TEST --> SERVER_TEST_FINISHING: phase 2 송신 종료
    SERVER_TEST_FINISHING --> WAITING_FOR_SHUTDOWN_ACK: 최종 STATS_ACK 전송
    WAITING_FOR_SHUTDOWN_ACK --> FINISHED: SHUTDOWN_ACK 수신

    IDLE --> ERRORED: setup 실패
    CONNECTING --> ERRORED: connect 실패
    ACCEPTING --> ERRORED: accept 실패
    RUNNING_TEST --> ERRORED: runtime 실패
    WAITING_FOR_ACK --> ERRORED: timeout 또는 예외
    WAITING_FOR_CONFIG --> ERRORED: timeout 또는 예외

    FINISHED --> [*]
    ERRORED --> [*]
```

## 읽을 때 중요한 점

- `control.waitFor`는 메시지가 이미 도착해 있으면 즉시 통과하고, 없으면 coroutine handle을 저장한다.
- `PacketReceiver`는 background receive loop를 돌며 `DATA_PACKET`은 통계로, control message는 `ControlMessageBus`로 보낸다.
- `ControlMessageBus::deliver`는 대기 중인 coroutine이 있으면 lock 밖에서 `resume()`한다.
- `network.connect/accept/send/receive`는 상위 API이고, 실제 platform I/O는 `doAsyncXxx`와 worker thread가 처리한다.
- session coroutine은 실패 시 예외를 던지고, `TestController::runTestCoroutine`이 잡아 `ERRORED`로 전이한다.
- `INITIALIZING` state enum은 존재하지만 현재 코드에서는 명시적으로 전이하지 않는다.

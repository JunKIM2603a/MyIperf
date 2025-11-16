# 클래스 다이어그램

이 문서는 `MyIperf` 프로젝트의 주요 클래스 구조와 관계를 설명합니다.

## 1. 다이어그램

```mermaid
classDiagram
    direction LR

    class CLIHandler {
        -TestController& testController
        +CLIHandler(controller)
        +run(argc, argv) void
        +static printHelp() void
        -parseArgs(argc, argv) Config
    }

    class ConfigParser {
        -string filepath
        -Config configData
        +ConfigParser(filepath)
        +load() bool
        +getConfig() Config
    }

    class Config {
        <<Data>>
        +enum TestMode { CLIENT, SERVER }
        -int packetSize
        -int numPackets
        -int sendIntervalMs
        -string protocol
        -string targetIP
        -int port
        -TestMode mode
        -bool saveLogs
        -int handshakeTimeoutMs
        +Config()
        +toJson() json
        +static fromJson(json) Config
    }

    class TestController {
        <<Singleton>>
        -atomic~State~ currentState
        -unique_ptr~NetworkInterface~ networkInterface
        -unique_ptr~PacketGenerator~ packetGenerator
        -unique_ptr~PacketReceiver~ packetReceiver
        -Config currentConfig
        -promise~void~ testCompletionPromise
        -atomic~bool~ testCompletionPromise_set
        -uint32_t m_expectedDataPacketCounter
        -atomic~long long~ m_contentMismatchCount
        -time_point m_testStartTime
        -TestStats m_remoteStats
        -TestStats m_clientStatsPhase1
        -TestStats m_serverStatsPhase1
        -TestStats m_clientStatsPhase2
        -TestStats m_serverStatsPhase2
        -mutex m_cliBlockMutex
        -condition_variable m_cliBlockCv
        -atomic~bool~ m_cliBlockFlag
        -atomic~bool~ m_stopped
        -thread m_handshakeWatchdog
        -atomic~bool~ m_handshakeWatchdogArmed
        -mutex m_handshakeWatchdogMutex
        -condition_variable m_handshakeWatchdogCv
        -bool m_handshakeWatchdogCancel
        +TestController()
        +~TestController()
        +startTest(config) void
        +stopTest() void
        +getTestCompletionFuture() future~void~
        +parseStats(payload) json
        -reset() void
        -onPacket(header, payload) void
        -onTestCompleted() void
        -startHandshakeWatchdog() void
        -cancelHandshakeWatchdog() void
        -sendClientStatsAndAwaitAck() void
        -transitionTo(State) void
        -transitionTo_nolock(State) void
        -cancelTimer() void
    }

    class NetworkInterface {
        <<Abstract>>
        +virtual ~NetworkInterface()
        +virtual initialize(ip, port) bool
        +virtual close() void
        +virtual asyncConnect(ip, port, cb) void
        +virtual asyncAccept(cb) void
        +virtual asyncSend(data, cb) void
        +virtual asyncReceive(size, cb) void
        +virtual blockingSend(data) int
        +virtual blockingReceive(size) vector~char~
    }

    class WinIOCPNetworkInterface {
        -HANDLE iocpHandle
        -SOCKET listenSocket
        -SOCKET clientSocket
        -iocpWorkerThread() void
    }

    class LinuxAsyncNetworkInterface {
        -int epollFd
        -int listenFd
        -int clientFd
        -epollWorkerThread() void
    }

    class PacketGenerator {
        -NetworkInterface* networkInterface
        -atomic~bool~ running
        -atomic~long long~ totalBytesSent
        -atomic~long long~ totalPacketsSent
        -Config config
        -uint32_t packetCounter
        -CompletionCallback completionCallback
        -thread m_generatorThread
        -mutex m_mutex
        -condition_variable m_cv
        -time_point m_startTime
        -time_point m_endTime
        -TestStats m_LastStats
        +PacketGenerator(netInterface)
        +~PacketGenerator()
        +start(config, onComplete) void
        +stop() void
        +resetStats() void
        +getStats() TestStats
        +lastStats() TestStats
        +saveLastStats(Stats) void
        -sendNextPacket() void
        -generatorThreadLoop() void
        -onPacketSent(bytesSent) void
        -shouldContinueSending() bool
        -preparePacketTemplate() void
    }

    class PacketReceiver {
        -NetworkInterface* networkInterface
        -atomic~bool~ running
        -time_point m_startTime
        -time_point m_endTime
        -atomic~long long~ currentBytesReceived
        -mutable mutex statsMutex
        -int packetBufferSize
        -vector~char~ m_receiveBuffer
        -PacketCallback onPacketCallback
        -ReceiverCompletionCallback onCompleteCallback
        -uint32_t expectedPacketCounter
        -atomic~long long~ m_totalPacketsReceived
        -atomic~long long~ m_failedChecksumCount
        -atomic~long long~ m_sequenceErrorCount
        -atomic~long long~ m_contentMismatchCount
        +PacketReceiver(netInterface)
        +~PacketReceiver()
        +start(onPacket) void
        +start(onPacket, onComplete) void
        +stop() void
        +getStats() TestStats
        +resetStats() void
        -receiveNextPacket() void
        -onPacketReceived(data, bytesReceived) void
        -processBuffer() void
    }

    class Logger {
        <<Static>>
        +static start() void
        +static stop() void
        +static log(message) void
        +static writeFinalReport(config, clientStats, serverStats) void
    }

    class Protocol {
        <<Helper>>
        +struct PacketHeader
        +enum MessageType
        +struct TestStats
        +static calculateChecksum(data, size) uint32_t
        +static verifyPacket(header, payload) bool
    }

    CLIHandler ..> ConfigParser : Uses
    CLIHandler ..> TestController : Controls
    ConfigParser o-- Config : Owns
    main ..> CLIHandler : Creates and runs
    main ..> Logger : Manages
    main ..> TestController : Manages

    TestController *-- "1" NetworkInterface : Owns
    TestController *-- "1" PacketGenerator : Owns
    TestController *-- "1" PacketReceiver : Owns
    TestController ..> Config : Uses
    TestController ..> Logger : Uses
    TestController ..> Protocol : Uses
    TestController ..> nlohmann.json : Uses

    PacketGenerator ..> NetworkInterface : Uses
    PacketGenerator ..> Protocol : Uses
    PacketGenerator ..> Config : Uses

    PacketReceiver ..> NetworkInterface : Uses
    PacketReceiver ..> Protocol : Uses
    PacketReceiver ..> Config : Uses

    NetworkInterface <|-- WinIOCPNetworkInterface : Implements
    NetworkInterface <|-- LinuxAsyncNetworkInterface : Implements

    Config ..> nlohmann.json : Uses
```

## 2. 주요 클래스 설명

*   **`main`**: 애플리케이션 진입점. `Logger` 및 `CLIHandler`와 같은 고수준 구성 요소를 초기화하고 조율합니다.
*   **`CLIHandler`**: 명령줄 인수를 구문 분석하고 `TestController`를 제어하여 테스트를 시작하는 역할을 합니다.
*   **`ConfigParser`**: JSON 설정 파일을 읽고 구문 분석하여 `Config` 객체를 생성합니다.
*   **`Config`**: 모드, 대상 IP, 포트 및 패킷 크기와 같은 테스트의 모든 설정 정보를 저장하는 데이터 클래스입니다.
*   **`TestController`**: 전체 테스트 수명 주기를 관리하는 핵심 클래스. 상태 머신을 사용하여 테스트 흐름을 제어하고 `NetworkInterface`, `PacketGenerator` 및 `PacketReceiver`의 동작을 조정합니다.
*   **`NetworkInterface`**: 네트워크 통신을 위한 추상 인터페이스. 플랫폼별 구현이 있습니다.
*   **`WinIOCPNetworkInterface`**: 고성능 비동기 I/O를 위해 Windows I/O 완료 포트(IOCP)를 사용하여 `NetworkInterface`를 구현합니다.
*   **`LinuxAsyncNetworkInterface`**: 고성능 비동기 I/O를 위해 Linux epoll을 사용하여 `NetworkInterface`를 구현합니다.
*   **`PacketGenerator`**: 클라이언트 모드에서 설정에 따라 테스트 패킷을 생성하고 `NetworkInterface`를 통해 전송합니다.
*   **`PacketReceiver`**: 네트워크에서 데이터를 수신하고 완전한 패킷으로 조립하며 유효성을 검사하고 통계를 기록합니다.
*   **`Protocol`**: `PacketHeader`, `MessageType` 및 `TestStats` 구조를 포함하여 통신 프로토토콜을 정의하는 헬퍼 네임스페이스/클래스입니다.
*   **`Logger`**: 콘솔, 파일 및 명명된 파이프에 쓸 수 있는 스레드 안전한 비동기 로깅 유틸리티를 제공하는 정적 클래스입니다.

## 3. 관계 설명

*   **제어**: `CLIHandler`는 `startTest`와 같은 `TestController`의 메서드를 호출하여 테스트 흐름을 제어합니다.
*   **구성**: `TestController`는 `unique_ptr`를 통해 `NetworkInterface`, `PacketGenerator` 및 `PacketReceiver`의 인스턴스를 소유하여 수명 주기를 관리합니다.
*   **연관/의존성**:
    *   `PacketGenerator`와 `PacketReceiver`는 `NetworkInterface`에 대한 포인터를 사용하여 데이터를 보내거나 받습니다.
    *   여러 클래스가 `Config` 객체를 참조하여 테스트 설정을 읽습니다.
*   **실현/구현**: `WinIOCPNetworkInterface` 및 `LinuxAsyncNetworkInterface`는 추상 `NetworkInterface`의 구체적인 구현입니다.

# MyIperf (IPEFTC)

iperf와 유사한 C++ 기반 네트워크 성능 테스트 도구입니다. 클라이언트-서버 모델을 사용하여 네트워크 처리량을 측정합니다. 이 프로젝트는 CMake를 사용하여 빌드되었으며 Windows (IOCP)와 Linux (epoll)를 모두 지원합니다.

## 주요 기능

-   **크로스 플랫폼 지원**: Windows (IOCP) 및 Linux (epoll)를 완벽 지원
-   **고성능 비동기 I/O**: 플랫폼별 최적화된 API를 사용한 고속 데이터 전송
-   **양방향 처리량 측정**: 단일 세션에서 클라이언트→서버, 서버→클라이언트 양방향 테스트 수행
-   **정교한 프로토콜**: 체크섬, 시퀀스 번호, 패킷 손실 감지 포함
-   **상세한 통계 보고서**: 로컬 및 원격 관점의 통계를 포함한 최종 보고서 생성
-   **유연한 설정**: 명령줄 또는 JSON 파일을 통한 테스트 매개변수 설정
-   **강력한 로깅 시스템**: 콘솔, 파일, 명명된 파이프(Windows)에 비동기 로깅
-   **자동화된 테스트**: TestRunner를 통한 반복 테스트 및 자동 결과 수집
-   **안정적인 상태 관리**: Race condition이 없는 견고한 상태 머신 구현

## 빌드 방법

1.  **빌드 디렉터리 생성:**
    ```shell
    cd D:\01_SW2_Project\MyIperf
    Remove-Item .\build\ -Recurse -Force -ErrorAction Stop
    mkdir build
    cd build
    ```

2.  **CMake로 빌드 파일 생성:**
    ```shell
    cmake ..
    ```

3.  **프로젝트 빌드:**
    *   생성된 `.sln` 파일을 Visual Studio에서 열고 컴파일할 수 있습니다.
    *   또는 명령줄에서 빌드할 수 있습니다 (예: 릴리스 모드):
        ```shell
        cmake --build . --config Release
        ```

## 실행 방법

### 서버 모드

수신 대기할 IP와 포트를 지정하여 서버 모드에서 실행 파일을 실행합니다.

```shell
# 디버그 빌드
.\build\Debug\IPEFTC.exe --mode server --target 0.0.0.0 --port 5201 --save-logs true

# 릴리스 빌드
.\build\Release\IPEFTC.exe --mode server --target 0.0.0.0 --port 5201 --save-logs true
```

### 클라이언트 모드

서버의 IP와 포트 및 기타 테스트 매개변수를 지정하여 클라이언트 모드에서 실행 파일을 실행합니다.

```shell
# 디버그 빌드
.\build\Debug\IPEFTC.exe --mode client --target 127.0.0.1 --port 5201 --packet-size 8192 --num-packets 10000 --save-logs true

# 릴리스 빌드
.\build\Release\IPEFTC.exe --mode client --target 127.0.0.1 --port 5201 --packet-size 8192 --num-packets 10000 --save-logs true
```

## 명령줄 옵션

| 옵션 | 설명 |
|---|---|
| `--mode <client|server>` | **필수.** 작동 모드를 지정합니다. |
| `--config <path>` | JSON 설정 파일의 경로. 명령줄 옵션이 파일 설정을 재정의합니다. |
| `--target <ip_address>` | 클라이언트의 대상 IP 주소. |
| `--port <port_number>` | 연결을 위한 포트 번호. |
| `--packet-size <bytes>` | 데이터 패킷의 크기 (바이트 단위, 헤더 포함). |
| `--num-packets <count>` | 보낼 패킷 수 (0은 무제한). |
| `--interval-ms <ms>` | 패킷 전송 간 지연 시간 (밀리초, 0은 연속 전송). |
| `--save-logs <true|false>`| 콘솔 로그를 'Log' 디렉터리에 파일로 저장합니다. |
| `-h, --help` | 도움말 메시지를 표시하고 종료합니다. |

## 최종 보고서 이해하기

최종 보고서는 두 단계로 나뉘며, 각 단계는 로컬 및 원격 시스템 관점의 통계를 보여줍니다.

-   **1단계: 클라이언트에서 서버로**: 클라이언트가 서버로 데이터를 보냅니다.
-   **2단계: 서버에서 클라이언트로**: 서버가 클라이언트로 데이터를 보냅니다.

### 주요 메트릭

-   **Total Bytes**: 패킷 헤더를 포함하여 전송된 총 바이트.
-   **Total Packets**: 전송된 총 데이터 패킷 수.
-   **Duration (s)**: 테스트의 데이터 전송 단계에 소요된 총 시간.
-   **Throughput (Mbps)**: 초당 메가비트로 계산된 데이터 전송 속도. 공식: `(Total Bytes * 8) / (Duration * 1,000,000)`.
-   **Checksum/Sequence Errors**: 전송 중 발생할 수 있는 패킷 손상 또는 손실을 나타냅니다.

---
## 프로토콜 흐름 및 상태 머신

애플리케이션은 테스트 생명주기를 관리하기 위해 엄격한 상태 머신을 따릅니다. 모든 상태 전이는 **메시지 전송 전에 수행**되어 네트워크 속도와 무관하게 안정적으로 동작합니다. 주요 상태와 전환은 아래 다이어그램에 나와 있습니다.

### 핵심 설계 원칙

1. **상태 전이 우선**: 모든 비동기 메시지 전송(`asyncSend`) 전에 상태 전이를 완료하여 race condition 방지
2. **Graceful Shutdown**: Phase 2 완료 후 `SHUTDOWN_ACK`를 통한 안전한 종료 보장
3. **스레드 안전성**: `transitionTo_nolock` 사용으로 mutex 데드락 방지
4. **견고한 핸드셰이크**: 각 단계에서 `TEST_FIN`, `STATS_EXCHANGE`, `STATS_ACK` 교환으로 동기화 보장

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> CONNECTING: startTest(client)
    IDLE --> ACCEPTING: startTest(server)

    CONNECTING --> SENDING_CONFIG: Connection complete
    SENDING_CONFIG --> WAITING_FOR_ACK: State transition BEFORE asyncSend
    WAITING_FOR_ACK --> RUNNING_TEST: ACK received

    ACCEPTING --> WAITING_FOR_CONFIG: Client accepted
    WAITING_FOR_CONFIG --> RUNNING_TEST: State transition BEFORE asyncSend

    RUNNING_TEST --> FINISHING: Test duration ended (Client)
    RUNNING_TEST --> FINISHING: TEST_FIN received (Server)

    FINISHING --> EXCHANGING_STATS: TEST_FIN received (Client)
    FINISHING --> WAITING_FOR_CLIENT_READY: State transition BEFORE asyncSend (Server)

    EXCHANGING_STATS --> WAITING_FOR_SERVER_FIN: State transition BEFORE asyncSend (Client)

    WAITING_FOR_CLIENT_READY --> RUNNING_SERVER_TEST: CLIENT_READY received (Server)
    RUNNING_SERVER_TEST --> SERVER_TEST_FINISHING: Test duration ended (Server)
    
    WAITING_FOR_SERVER_FIN --> EXCHANGING_SERVER_STATS: State transition BEFORE asyncSend (Client)
    SERVER_TEST_FINISHING --> WAITING_FOR_SHUTDOWN_ACK: State transition BEFORE asyncSend (Server)

    WAITING_FOR_SHUTDOWN_ACK --> FINISHED: SHUTDOWN_ACK received (Server)
    EXCHANGING_SERVER_STATS --> FINISHED: SHUTDOWN_ACK sent (Client)

    ERRORED --> [*]
    FINISHED --> [*]
```

### 주요 프로토콜 메시지

| 메시지 타입 | 방향 | 설명 |
|---|---|---|
| `CONFIG_HANDSHAKE` | Client → Server | 테스트 설정 전송 |
| `CONFIG_ACK` | Server → Client | 설정 확인 |
| `DATA_PACKET` | Both directions | 실제 데이터 전송 (체크섬, 시퀀스 번호 포함) |
| `TEST_FIN` | Both directions | 테스트 단계 완료 신호 |
| `STATS_EXCHANGE` | Both directions | 통계 정보 교환 |
| `STATS_ACK` | Both directions | 통계 수신 확인 |
| `CLIENT_READY` | Client → Server | Phase 2 준비 완료 신호 |
| `SHUTDOWN_ACK` | Client → Server | 최종 종료 확인 (Graceful shutdown) |
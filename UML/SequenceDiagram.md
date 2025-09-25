# 전체 동작 시퀀스 다이어그램

이 문서는 `MyIperf`의 클라이언트와 서버가 테스트를 시작하고 완료하기까지의 전체 상호작용 흐름을 상세하게 보여줍니다.

## 1. 다이어그램

```mermaid
sequenceDiagram
    participant C_TC as 클라이언트<br>컨트롤러
    participant C_PG as 클라이언트<br>생성기
    participant C_NI as 클라이언트<br>네트워크
    participant S_TC as 서버<br>컨트롤러
    participant S_PR as 서버<br>수신기
    participant S_NI as 서버<br>네트워크

    activate S_TC
    S_TC->>S_NI: initialize() & asyncAccept()
    note over S_TC, S_NI: 1. 서버 시작 및<br>연결 대기
    deactivate S_TC

    activate C_TC
    C_TC->>C_NI: initialize() & asyncConnect()
    note over C_TC, C_NI: 2. 클라이언트 시작 및<br>서버에 연결
    
    C_NI->>S_NI: [TCP 연결 요청]
    activate S_NI
    S_NI-->>C_NI: [TCP 연결 수락]
    deactivate S_NI

    activate S_TC
    S_NI-->>S_TC: onAccepted() 콜백
    S_TC->>S_PR: start()
    note over S_TC: 상태: ACCEPTING -> WAITING_FOR_CONFIG
    deactivate S_TC
    
    C_NI-->>C_TC: onConnected() 콜백
    note over C_TC: 상태: CONNECTING -> SENDING_CONFIG
    C_TC->>C_NI: asyncSend(CONFIG_HANDSHAKE)
    note over C_TC, C_NI: 3. 설정 정보 전송
    deactivate C_TC

    activate S_PR
    S_NI->>S_PR: onPacket(CONFIG_HANDSHAKE)
    activate S_TC
    S_PR-->>S_TC: onPacket() 콜백 전달
    note over S_TC: 상태: WAITING_FOR_CONFIG -> RUNNING_TEST
    S_TC->>S_NI: asyncSend(CONFIG_ACK)
    deactivate S_TC
    deactivate S_PR

    activate C_TC
    C_NI-->>C_TC: onPacket(CONFIG_ACK)
    note over C_TC: 상태: WAITING_FOR_ACK -> RUNNING_TEST
    C_TC->>C_PG: start()
    note over C_TC, C_PG: 4. 테스트 데이터<br>전송 시작
    
    loop 데이터 전송
        activate C_PG
        C_PG->>C_NI: asyncSend(DATA_PACKET)
        deactivate C_PG
        activate S_PR
        S_NI->>S_PR: onPacket(DATA_PACKET)
        deactivate S_PR
    end
    
    activate C_PG
    C_PG-->>C_TC: onTestCompleted() 콜백
    deactivate C_PG
    note over C_TC: 상태: RUNNING_TEST -> FINISHING
    C_TC->>C_NI: asyncSend(TEST_FIN)
    note over C_TC, C_NI: 5. 종료 및<br>결과 교환
    
    activate S_TC
    activate S_PR
    S_NI->>S_PR: onPacket(TEST_FIN)
    S_PR-->>S_TC: onPacket() 콜백 전달
    deactivate S_PR
    note over S_TC: 상태: RUNNING_TEST -> FINISHING
    S_TC->>S_NI: asyncSend(TEST_FIN)
    
    C_NI-->>C_TC: onPacket(TEST_FIN)
    note over C_TC: 상태: FINISHING -> EXCHANGING_STATS
    C_TC->>C_NI: asyncSend(STATS_EXCHANGE)
    
    activate S_PR
    S_NI->>S_PR: onPacket(STATS_EXCHANGE)
    S_PR-->>S_TC: onPacket() 콜백 전달
    deactivate S_PR
    note over S_TC: 상태: FINISHING -> FINISHED
    S_TC->>S_NI: asyncSend(STATS_ACK)
    
    C_NI-->>C_TC: onPacket(STATS_ACK)
    note over C_TC: 상태: EXCHANGING_STATS -> FINISHED
    
    C_TC->>C_TC: stopTest() (리소스 정리)
    deactivate C_TC
    
    S_TC->>S_TC: stopTest() (리소스 정리)
    deactivate S_TC
```

## 2. 전체 시퀀스 설명

1.  **서버 준비**:
    *   서버 `TestController`가 `NetworkInterface`를 초기화하고 `asyncAccept`를 호출하여 클라이언트의 접속을 비동기적으로 기다립니다.

2.  **클라이언트 연결**:
    *   클라이언트 `TestController`가 `NetworkInterface`를 초기화하고 `asyncConnect`를 호출하여 서버에 연결을 시도합니다.
    *   TCP 3-way handshake를 통해 연결이 수립됩니다.

3.  **설정 교환 (Handshake)**:
    *   서버는 연결을 감지(`onAccepted`)하고, `PacketReceiver`를 시작하여 클라이언트로부터 올 데이터를 받을 준비를 합니다.
    *   클라이언트는 연결 성공(`onConnected`) 후, 테스트 설정을 담은 `CONFIG_HANDSHAKE` 메시지를 서버로 전송합니다.
    *   서버 `PacketReceiver`는 이 메시지를 받아 `TestController`에 전달하고, 서버 `TestController`는 이에 대한 응답으로 `CONFIG_ACK`을 클라이언트로 보냅니다.
    *   클라이언트는 `CONFIG_ACK`을 받으면 양쪽 모두 테스트를 진행할 준비가 된 것입니다 (`RUNNING_TEST` 상태).

4.  **테스트 데이터 전송**:
    *   클라이언트 `TestController`가 `PacketGenerator`를 시작합니다.
    *   `PacketGenerator`는 설정된 시간 동안 `DATA_PACKET`을 `NetworkInterface`를 통해 계속 전송합니다.
    *   서버 `PacketReceiver`는 들어오는 `DATA_PACKET`들을 지속적으로 수신합니다.

5.  **종료 및 결과 교환**:
    *   클라이언트에서 테스트 시간이 만료되면 `PacketGenerator`가 `TestController`에게 완료를 알립니다.
    *   클라이언트는 `TEST_FIN` 메시지를 보내 데이터 전송이 끝났음을 알립니다.
    *   서버는 `TEST_FIN`을 받고, 자신도 `TEST_FIN`으로 응답하여 양측 모두 데이터 전송이 끝났음을 확인합니다 (종료 핸드셰이크).
    *   클라이언트는 종료 핸드셰이크 완료 후, 자신의 통계 정보를 `STATS_EXCHANGE` 메시지로 서버에 보냅니다.
    *   서버는 통계를 수신한 후, 최종 확인 의미로 `STATS_ACK`를 클라이언트에 보냅니다.
    *   `STATS_ACK`를 받은 클라이언트를 포함하여 양측 모두 `FINISHED` 상태가 되며 테스트가 정상적으로 종료됩니다.

6.  **리소스 정리**:
    *   양측 `TestController`는 `stopTest()`를 통해 사용했던 모든 리소스(소켓 등)를 정리합니다.

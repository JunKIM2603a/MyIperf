# 코드 분석: TestController 상태 머신

이 문서는 `MyIperf`의 핵심 로직인 `TestController`의 상태 머신 작동 방식을 코드 레벨에서 상세히 설명합니다.

## 1. 개요

`TestController`는 [StateMachineDiagram.md](./StateMachineDiagram.md)에 정의된 상태 전이를 코드상에서 구현합니다. 비동기 네트워크 I/O의 이벤트를 처리하며, 정확한 순서로 메시지를 교환하기 위해 정교한 상태 관리를 수행합니다.

## 2. 핵심 구현 패턴: "State Before Send"

네트워크 지연이 거의 없는 로컬 루프백이나 고속 네트워크 환경에서는 메시지 전송 직후 응답이 올 수 있습니다. `asyncSend` 호출 **이후** 콜백에서 상태를 변경하면, 시스템이 응답을 받았을 때 아직 이전 상태에 머물러 있어 로직 에러가 발생할 수 있습니다 (Race Condition).

이를 방지하기 위해 `MyIperf`는 모든 메시지 전송 전에 상태를 먼저 전이시킵니다.

### 예시: 클라이언트 설정 전송 (TestController.cpp)

```cpp
// [TestController.cpp:L303-313]
// Critical fix: Transition state BEFORE sending to prevent race condition
transitionTo_nolock(State::WAITING_FOR_ACK);

networkInterface->asyncSend(packet, [this](size_t bytesSent) {
    if (bytesSent > 0) {
        Logger::log("Info: Client sent config packet successfully.");
    } else {
        transitionTo_nolock(State::ERRORED);
    }
});
```

## 3. 주요 상태별 로직 코드 분석

### `IDLE` → `CONNECTING` / `ACCEPTING`

- **시점**: `startTest()` 호출 시.
- **코드**: [L182, L189](file:///d:/01_SW2_Project/MyIperf/MyIperf/TestController.cpp#L182-189)
- **설명**: 모드(CLIENT/SERVER)에 따라 네트워크 인터페이스를 초기화하고 첫 번째 비동기 작업(`asyncConnect` 또는 `asyncAccept`)을 게시합니다.

### `SENDING_CONFIG` → `WAITING_FOR_ACK`

- **시점**: 클라이언트가 서버에 연결된 후.
- **코드**: [L303](file:///d:/01_SW2_Project/MyIperf/MyIperf/TestController.cpp#L303)
- **설명**: 설정을 JSON으로 전송하기 전 미리 `WAITING_FOR_ACK` 상태로 진입합니다.

### `RUNNING_TEST` (Phase 1: Client to Server)

- **시점**: 클라이언트가 `CONFIG_ACK`을 받았을 때, 서버가 `CONFIG_HANDSHAKE`를 처리했을 때.
- **코드**: [L332-L340](file:///d:/01_SW2_Project/MyIperf/MyIperf/TestController.cpp#L332-L340)
- **설명**: 클라이언트는 `PacketGenerator`를 시작하고, 서버는 `PacketReceiver`의 통계를 리셋하여 측정을 시작합니다.

### `EXCHANGING_STATS` (Handshake & Phase 2 준비)

- **시점**: Phase 1 데이터 전송 완료 후.
- **코드**: [L393, L593, L736](file:///d:/01_SW2_Project/MyIperf/MyIperf/TestController.cpp#L393)
- **설명**: 양측의 통계를 교환합니다. 클라이언트는 `CLIENT_READY`를 보내 서버의 전송 시작(Phase 2)을 유도합니다.

### `FINISHED` & Graceful Shutdown

- **시점**: 모든 테스트 완료 및 통계 교환 완료 시.
- **코드**: [L425-L448](file:///d:/01_SW2_Project/MyIperf/MyIperf/TestController.cpp#L425-L448)
- **설명**: `SHUTDOWN_ACK`를 최종적으로 교환하여 소켓 레이어에서 안전하게 종료되도록 보장합니다.

## 4. 디버깅 팁

`TestController.cpp`에는 상태 전이 시마다 로그를 남기는 `transitionTo_nolock` 함수가 있습니다.

```cpp
void TestController::transitionTo_nolock(State newState) {
    currentState = newState;
    Logger::log("Info: Transitioning to state: " + std::string(stateToString(newState)));
    // ...
}
```

문제가 발생할 경우 로그 파일 또는 콘솔 출력에서 `Transitioning to state:` 패턴을 추적하여 어느 단계에서 멈추거나 잘못 전이되었는지 파악할 수 있습니다.

---
> [!TIP]
> 상세한 시퀀스 흐름은 [SequenceDiagram.md](./SequenceDiagram.md)를 참조하세요.

# MyIperf 개발자 트러블슈팅 가이드

이 문서는 `MyIperf` 개발 및 테스트 과정에서 발생할 수 있는 주요 문제들과 그 해결 방법, 그리고 코드상에 반영된 주요 설계 설계 결정을 설명합니다.

## 1. 네트워크 및 포트 관련 문제

### "WSAEADDRINUSE (10048)" 또는 "Address already in use" 에러

- **현상**: 서버 시작 시 소켓 바인딩 오류가 발생하며 서버가 실행되지 않음.
- **원인**: 지정한 포트(기본 5201)를 다른 프로세스가 사용 중이거나, 이전 테스트 세션이 비정상 종료되어 소켓이 `TIME_WAIT` 상태일 때 발생.
- **해결**:
  - `netstat -ano | findstr :5201` 명령어로 포트 점유 확인.
  - 다른 포트를 사용하도록 `-p [port]` 옵션 변경.
  - 소켓 재사용 옵션(`SO_REUSEADDR`)이 코드에 적용되어 있으나 OS 수준의 리드타임이 필요할 수 있음.

### "WSAEACCES (10013)" 에러

- **현상**: 특정 포트에서 `bind` 실패.
- **원인**: Windows의 경우 운영체제에서 예약한 포트 범위이거나 관리자 권한이 필요한 포트(0~1023)를 사용하려 할 때 발생.
- **해결**: 1024 이상의 사용자 정의 포트를 사용하거나 관리자 권한으로 실행.

## 2. 레이스 컨디션 (Race Condition) 방지

`MyIperf`는 고속 네트워크 환경에서 발생할 수 있는 비동기 로직 충돌을 방지하기 위해 다음과 같은 규칙을 준수합니다.

### 상태 전이 후 전송 (State-Before-Send)

- **문제**: 메시지 전송 후 콜백에서 상태를 바꾸면, 콜백이 실행되기 전에 상대방의 응답(ACK)이 먼저 도착하여 잘못된 상태에서 처리될 수 있음.
- **해결**: `TestController.cpp` 내의 `transitionTo_nolock` 호출을 `asyncSend`보다 먼저 수행함.
- **관련 코드**: `TestController::transitionTo_nolock` 내의 `State::SENDING_CONFIG`, `State::FINISHING` 등.

## 3. 통계 데이터 불일치

### Throughput(Mbps) 값이 너무 낮거나 0으로 나옴

- **원인**: 테스트 기간이 너무 짧거나(Default 10s 미만), 패킷 생성 속도가 네트워크 대역폭보다 현저히 낮게 설정된 경우.
- **확인**: `config.json`의 `sendIntervalMs`를 줄이거나 `packetSize`를 늘려 부하를 조정함.

### Sequence Error 발생

- **원인**: UDP가 아닌 TCP를 사용함에도 시퀀스 에러가 발생한다면, 로직상의 패킷 누락이나 버퍼 오버런(Buffer Overrun) 가능성이 있음.
- **확인**: `PacketReceiver.cpp`에서 수신 버퍼 크기와 시퀀스 번호 검증 로직을 확인.

## 4. 로깅 및 디버깅

### 상세 로그 확인 방법

`MyIperf`는 `Logger` 클래스를 통해 상세한 상태 정보를 남깁니다.

- 콘솔 출력뿐만 아니라 `./Log` 디렉토리에 파일로 저장됨.
- `DebugPause` 매크로가 삽입된 지점은 디버그 모드에서 흐름을 제어하는 데 사용될 수 있음.

---
> [!IMPORTANT]
> 새로운 테스트 시나리오를 추가할 때는 반드시 `TestController`의 기존 상태 머신과 충돌하지 않는지 [StateMachineDiagram.md](./StateMachineDiagram.md)를 통해 먼저 검증해 주세요.

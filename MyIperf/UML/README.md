# MyIperf 코드 이해 문서

이 폴더는 `MyIperf` 코드를 이해하는 데 직접 필요한 문서만 남긴 최소 문서 세트입니다.

## 문서 목록

| 파일 | 내용 |
| --- | --- |
| [ClassDiagram.md](./ClassDiagram.md) | 핵심 클래스와 소유 관계 |
| [ActivityDiagram.md](./ActivityDiagram.md) | 테스트 실행 흐름과 `TestController::State` 전이 |
| [SequenceDiagram.md](./SequenceDiagram.md) | 클라이언트/서버 간 제어 메시지 순서 |
| [NetworkBackends.md](./NetworkBackends.md) | Windows IOCP, Linux epoll 백엔드 동작 요약 |

## 읽는 순서

1. `ClassDiagram.md`에서 전체 구성 요소와 의존 관계를 먼저 확인합니다.
2. `ActivityDiagram.md`에서 클라이언트 모드와 서버 모드의 실행 흐름을 봅니다.
3. `SequenceDiagram.md`에서 `MessageType` 기반 핸드셰이크 순서를 따라갑니다.
4. 플랫폼별 비동기 I/O 구현이 궁금하면 `NetworkBackends.md`를 봅니다.

## 주요 코드 진입점

- `src/myiperf/TestController.cpp`
- `src/myiperf/ClientTestSession.cpp`
- `src/myiperf/ServerTestSession.cpp`
- `src/myiperf/ControlChannel.h`
- `src/myiperf/ControlMessageBus.h`
- `include/myiperf/NetworkInterface.h`

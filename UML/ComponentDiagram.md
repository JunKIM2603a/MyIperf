# 컴포넌트 다이어그램 (Component Diagram)

이 문서는 `MyIperf`를 구성하는 주요 소프트웨어 컴포넌트와 그들 사이의 의존 관계를 설명합니다.

## 1. 다이어그램

```mermaid
componentDiagram
    direction LR

    package "사용자 인터페이스" {
        [CLI]
    }

    package "코어 로직" {
        [테스트 컨트롤러] as Controller
        [패킷 처리] as PacketProcessor
    }

    package "인프라스트럭처" {
        [네트워크]
        [설정]
        [로깅]
    }

    CLI --> Controller : 제어
    Controller --> PacketProcessor : 사용
    Controller --> [네트워크] : 사용
    Controller --> [설정] : 사용
    Controller --> [로깅] : 사용
    PacketProcessor --> [네트워크] : 사용
```

## 2. 컴포넌트 설명

*   **CLI (Command Line Interface)**
    *   **역할**: 사용자와의 상호작용을 담당하는 진입점입니다.
    *   **기능**: 사용자가 입력한 명령줄 인수를 파싱하고, `테스트 컨트롤러` 컴포넌트를 호출하여 테스트를 시작하거나 설정을 관리합니다.
    *   **주요 파일**: `main.cpp`, `CLIHandler.h/cpp`

*   **테스트 컨트롤러 (Test Controller)**
    *   **역할**: 애플리케이션의 핵심 로직을 담당하며, 전체 테스트 흐름을 총괄합니다.
    *   **기능**: 상태 머신을 통해 테스트의 생명주기(시작, 설정 교환, 진행, 종료)를 관리합니다. 다른 컴포넌트들을 조율하여 테스트가 올바르게 수행되도록 합니다.
    *   **주요 파일**: `TestController.h/cpp`

*   **패킷 처리 (Packet Processor)**
    *   **역할**: 테스트 데이터 패킷의 생성과 수신을 담당합니다.
    *   **기능**: 클라이언트 모드에서는 데이터를 생성(`PacketGenerator`)하고, 서버 모드에서는 데이터를 수신(`PacketReceiver`)하여 통계를 계산합니다.
    *   **주요 파일**: `PacketGenerator.h/cpp`, `PacketReceiver.h/cpp`

*   **네트워크 (Network)**
    *   **역할**: 실제 네트워크 통신을 처리하는 계층입니다.
    *   **기능**: 플랫폼(Windows/Linux)에 맞는 비동기 I/O 모델(IOCP/epoll)을 사용하여 데이터를 보내고 받는 저수준(low-level) 통신을 캡슐화합니다. `NetworkInterface`를 통해 일관된 인터페이스를 제공합니다.
    *   **주요 파일**: `NetworkInterface.h`, `WinIOCPNetworkInterface.h/cpp`, `LinuxAsyncNetworkInterface.h/cpp`

*   **설정 (Configuration)**
    *   **역할**: 애플리케이션의 모든 설정을 관리합니다.
    *   **기능**: `Config` 객체를 통해 테스트 파라미터를 저장하고, `ConfigParser`를 통해 JSON 파일에서 설정을 읽어옵니다.
    *   **주요 파일**: `Config.h/cpp`, `ConfigParser.h/cpp`

*   **로깅 (Logging)**
    *   **역할**: 프로그램의 동작 상태와 테스트 결과를 기록합니다.
    *   **기능**: 콘솔, 파일, 명명된 파이프(Named Pipe) 등 다양한 출력 대상으로 로그를 비동기적으로 기록하는 기능을 제공합니다.
    *   **주요 파일**: `Logger.h/cpp`

## 3. 관계 설명

*   `CLI`는 `테스트 컨트롤러`를 호출하여 사용자의 요청을 전달합니다.
*   `테스트 컨트롤러`는 테스트를 진행하기 위해 `패킷 처리`, `네트워크`, `설정`, `로깅` 등 거의 모든 하위 컴포넌트들을 사용하고 조율합니다.
*   `패킷 처리` 컴포넌트는 생성하거나 수신할 패킷을 실제로 주고받기 위해 `네트워크` 컴포넌트를 사용합니다.

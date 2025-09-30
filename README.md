# MyIperf 프로젝트

이 저장소에는 `MyIperf`와 `MyIperf_Pipe`라는 두 가지 주요 프로젝트가 포함되어 있습니다.

## MyIperf

`MyIperf`는 `iperf`에서 영감을 받은 C++ 기반의 네트워크 성능 테스트 도구입니다. 클라이언트 또는 서버로 실행되도록 구성하여 네트워크 대역폭과 품질을 테스트할 수 있습니다.

### 주요 기능
- 클라이언트 및 서버 모드
- JSON 파일을 통한 구성 (`config_client.json`, `config_server.json`)
- 크로스플랫폼 네트워크 코드 (Windows IOCP 및 Linux 지원)
- 로깅 기능

## MyIperf_Pipe

`MyIperf_Pipe`는 `MyIperf` 도구와 함께 작동하도록 설계된 유틸리티입니다. 프로젝트 구조를 기반으로 볼 때, 명명된 파이프를 통해 `MyIperf`의 데이터나 로그를 읽어 처리하거나 표시하는 소비자 애플리케이션으로 보입니다.
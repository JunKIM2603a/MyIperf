# MyIperf Project

이 저장소는 네트워크 성능 테스트 도구인 `MyIperf`와 자동화 실행 도구인 `TestRunner`로 구성됩니다.

## MyIperf

`MyIperf`는 C++ 기반 네트워크 성능 테스트 애플리케이션입니다. 클라이언트 또는 서버 모드로 실행할 수 있으며, JSON 설정 파일과 CLI 옵션을 통해 테스트 조건을 지정합니다.

주요 기능:
- 클라이언트/서버 모드
- JSON 설정 파일 지원
- Windows IOCP 및 Linux epoll 기반 비동기 네트워크 처리
- 콘솔/파일 로깅

## TestRunner

`TestRunner`는 여러 머신에서 `MyIperf` 테스트를 원격 제어하고 반복 실행하기 위한 자동화 도구입니다. TCP 제어 채널을 통해 서버/클라이언트 실행을 조율하고 테스트 결과를 수집합니다.

## Git Tags

릴리스 버전은 일반적으로 `vX.Y.Z` 형식의 annotated tag로 관리합니다.

```bash
git tag -a v1.0.0 -m "Release version 1.0.0"
git push origin v1.0.0
```

# MyIperf

`MyIperf`는 네트워크 처리량을 측정하는 C++ 기반 테스트 도구입니다. 프로젝트는 재사용 가능한 코어 라이브러리와 CLI 애플리케이션으로 분리되어 있습니다.

## 프로젝트 구조

- `myiperf_core`: 네트워크 I/O, 프로토콜, 설정, 로깅, 테스트 제어 로직을 담은 정적 라이브러리
- `IPEFTC`: `myiperf_core`를 링크해서 사용하는 CLI 애플리케이션

주요 디렉터리:

- `include/myiperf/`: 공개 헤더
- `src/myiperf/`: 코어 라이브러리 구현 및 private 헤더
- `src/myiperf/platform/`: Windows IOCP, Linux epoll 플랫폼 구현
- `app/ipeftc/`: CLI 진입점과 인자 처리
- `third_party/nlohmann/`: 프로젝트 로컬 JSON 헤더

## 빌드

저장소 루트(`D:\01_SW2_Project\MyIperf`)에서 실행합니다.

```powershell
cmake -S MyIperf -B MyIperf/build
cmake --build MyIperf/build --config Release
```

`MyIperf` 프로젝트 디렉터리 안에서 실행할 때는 다음처럼 사용할 수 있습니다.

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Windows Release 빌드 산출물:

```text
MyIperf/build/lib/Release/myiperf_core.lib
MyIperf/build/bin/Release/IPEFTC.exe
```

## 실행

서버:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --mode server --target 0.0.0.0 --port 5201 --save-logs true
```

클라이언트:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --mode client --target 127.0.0.1 --port 5201 --packet-size 8192 --num-packets 10000 --save-logs true
```

`MyIperf` 프로젝트 디렉터리 안에 있다면 실행 경로는 다음처럼 짧아집니다.

```powershell
.\build\bin\Release\IPEFTC.exe --help
```

## 관련 프로젝트

| 프로젝트 | 설명 |
| --- | --- |
| `TestRunner` | 여러 머신에서 `IPEFTC` 서버/클라이언트 실행을 조율하고 결과를 수집하는 자동화 도구 |

## 개발 참고

- 공개 API를 사용하는 코드는 `#include "myiperf/..."` 형식으로 include합니다.
- `PacketGenerator`, `PacketReceiver`, 플랫폼별 NetworkInterface 구현은 `myiperf_core`의 private 구현 세부사항입니다.
- `CLIHandler`와 `main.cpp`는 애플리케이션 계층에만 속하며 `myiperf_core`에는 포함되지 않습니다.
- `IPEFTC`는 파일 경로가 아니라 CMake 타깃 `MyIperf::core`를 통해 코어 라이브러리를 링크합니다.

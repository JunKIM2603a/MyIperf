# MyIperf Project

이 저장소는 네트워크 성능 테스트 도구인 `MyIperf`와 분산 자동화 실행 도구인 `TestRunner`로 구성됩니다.

- `MyIperf`: 실제 TCP 성능 테스트를 수행하는 C++ 코어 라이브러리와 CLI 애플리케이션
- `TestRunner`: 여러 머신 또는 여러 포트에서 `IPEFTC` 실행을 원격 제어하고 결과를 수집하는 자동화 도구

현재 구현은 기존 `Config` handshake와 네트워크 테스트 흐름을 유지하면서, 실행 결과를 호출 프로그램이 안정적으로 소비할 수 있도록 별도 결과 전달 계층을 제공합니다.

## Repository Layout

```text
.
├── MyIperf/       # myiperf_core library and IPEFTC CLI
├── ResultPipeVerifier/ # automated result pipe/event stream smoke verifier
├── ResultJsonViewer/ # final result JSON console viewer
├── TestRunner/    # distributed IPEFTC orchestration tool
├── Log/           # generated logs
└── README.md      # project overview
```

상세 문서는 각 하위 프로젝트 README를 기준으로 봅니다.

| 문서 | 내용 |
| --- | --- |
| `MyIperf/README.md` | `IPEFTC` 빌드/실행, 결과 JSON, C++ API, result pipe |
| `ResultPipeVerifier/README.md` | Windows/Linux result pipe 자동 smoke 검증 |
| `ResultJsonViewer/README.md` | 최종 결과 JSON 파일 콘솔 요약 출력 |
| `TestRunner/README.md` | 분산 실행, control protocol, JSON-first 결과 수집, 멀티 포트/반복 실행 |
| `MyIperf/UML/README.md` | 구조와 실행 흐름 다이어그램 |

## Quick Build

저장소 루트에서 실행합니다.

```powershell
cmake -S .\MyIperf -B .\MyIperf\build
cmake -S .\ResultPipeVerifier -B .\ResultPipeVerifier\build
cmake -S .\ResultJsonViewer -B .\ResultJsonViewer\build
cmake -S .\TestRunner -B .\TestRunner\build

cmake --build .\MyIperf\build --config Release
cmake --build .\ResultPipeVerifier\build --config Release
cmake --build .\ResultJsonViewer\build --config Release
cmake --build .\TestRunner\build --config Release
```

Windows Release 산출물:

```text
MyIperf\build\bin\Release\IPEFTC.exe
MyIperf\build\lib\Release\myiperf_core.lib
ResultPipeVerifier\build\Release\ResultPipeVerifier.exe
ResultJsonViewer\build\Release\ResultJsonViewer.exe
TestRunner\build\Release\TestRunner.exe
```

Debug 빌드는 `--config Debug`로 동일하게 수행합니다.

## Quick Run

`IPEFTC` 단독 실행:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --mode server --target 0.0.0.0 --port 5201 --save-logs false
.\MyIperf\build\bin\Release\IPEFTC.exe --mode client --target 127.0.0.1 --port 5201 --packet-size 8192 --num-packets 10000 --save-logs false
```

`TestRunner` 분산 실행:

```powershell
.\TestRunner\build\Release\TestRunner.exe --mode server --control-port 9500 --ipeftc-path .\MyIperf\build\bin\Release\IPEFTC.exe
.\TestRunner\build\Release\TestRunner.exe --mode client --server 127.0.0.1 --control-port 9500 --test-port 5201 --num-packets 10000 --result-dir Results
```

`TestRunner`의 기본 control port는 `9500`입니다. 테스트 데이터 포트 기본값은 `5201`입니다.

## 결과 확인 3가지 방법

### 1. JSON 결과 파일

`IPEFTC`는 각 실행 결과를 구조화된 JSON 파일로 저장합니다.

```text
result-<runId>-CLIENT.json
result-<runId>-SERVER.json
latest-CLIENT.json
latest-SERVER.json
```

정확한 실행 결과를 프로그램에서 읽을 때는 `latest-*`가 아니라 `runId`가 포함된 `result-<runId>-<ROLE>.json` 파일을 사용합니다. `latest-*` 파일은 사람이 최근 결과를 빠르게 확인하기 위한 편의 파일입니다.

주요 옵션:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --mode client `
    --target 127.0.0.1 `
    --port 5201 `
    --num-packets 10000 `
    --run-id smoke-001 `
    --result-dir Results `
    --result-json Results\client-copy.json
```

저장된 최종 JSON은 `ResultJsonViewer`로 사람용 요약 형태로 확인할 수 있습니다.

```powershell
.\ResultJsonViewer\build\Release\ResultJsonViewer.exe --file Results\result-smoke-001-CLIENT.json
.\ResultJsonViewer\build\Release\ResultJsonViewer.exe --result-dir Results --run-id smoke-001 --role CLIENT
```

### 2. C++ 라이브러리 API

`myiperf_core` 사용자는 `RunOptions`와 `TestRunResult`를 통해 실행 결과를 직접 받을 수 있습니다.

```cpp
Config config;
config.setMode(Config::TestMode::CLIENT);
config.setTargetIP("127.0.0.1");
config.setPort(5201);
config.setNumPackets(10000);

RunOptions options;
options.runId = "api-run-001";
options.resultDir = "Results";

TestController controller;
TestRunResult result = controller.runTestAsync(config, options).get();
```

기존 `startTest(const Config&)` API는 유지되며, 새 API로 `startTest(config, options)`, `runTestAsync(config, options)`, `getLastResult()`가 제공됩니다.

### 3. Result Pipe / Event Stream

`--result-pipe <name>`을 지정하면 `IPEFTC`가 별도 result pipe로 JSON Lines 이벤트를 보냅니다.

지원 이벤트:

- `run_started`
- `phase_result`
- `final_result`

result pipe는 best-effort 채널입니다. reader가 없거나 pipe 전송에 실패해도 테스트 자체는 계속되며, 최종 판정은 JSON 결과 파일 또는 C++ API 결과를 기준으로 확인합니다.

Windows/Linux result pipe 동작은 `ResultPipeVerifier`로 자동 검증할 수 있습니다.

```powershell
.\ResultPipeVerifier\build\Release\ResultPipeVerifier.exe --ipeftc-path .\MyIperf\build\bin\Release\IPEFTC.exe
```

## TestRunner 결과 수집

`TestRunner`는 각 run/port마다 고유 `runId`를 생성하고 `IPEFTC` 양쪽에 전달합니다.

```text
tr-run<run>-port<port>-<counter>
```

결과 수집은 다음 순서로 수행됩니다.

1. `result-<runId>-CLIENT.json`, `result-<runId>-SERVER.json` 우선 파싱
2. 파일이 없거나 파싱에 실패하면 stderr에 이유 출력
3. 기존 stdout parser fallback으로 가능한 한 결과 산출

이 방식 때문에 최신 `IPEFTC`에서는 구조화 JSON을 우선 사용하고, 이전 버전 또는 JSON 저장 실패 상황에서도 기존 stdout 기반 동작을 유지합니다.

## Verification

빌드 확인:

```powershell
cmake --build .\MyIperf\build --config Release
cmake --build .\ResultPipeVerifier\build --config Release
cmake --build .\ResultJsonViewer\build --config Release
cmake --build .\TestRunner\build --config Release
```

도움말 확인:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --help
.\ResultPipeVerifier\build\Release\ResultPipeVerifier.exe --help
.\ResultJsonViewer\build\Release\ResultJsonViewer.exe --help
.\TestRunner\build\Release\TestRunner.exe --help
```

잔류 프로세스 확인:

```powershell
Get-Process IPEFTC,TestRunner -ErrorAction SilentlyContinue
Get-Process IPEFTC,ResultPipeVerifier,TestRunner -ErrorAction SilentlyContinue
```

## Git Tags

릴리스 버전은 일반적으로 `vX.Y.Z` 형식의 annotated tag로 관리합니다.

```powershell
git tag -a v1.0.0 -m "Release version 1.0.0"
git push origin v1.0.0
```

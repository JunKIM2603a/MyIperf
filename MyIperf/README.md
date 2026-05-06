# MyIperf

`MyIperf`는 TCP 네트워크 처리량을 측정하는 C++ 기반 테스트 도구입니다. 프로젝트는 재사용 가능한 코어 라이브러리인 `myiperf_core`와 CLI 애플리케이션인 `IPEFTC`로 분리되어 있습니다.

`IPEFTC`는 클라이언트/서버 양쪽에서 같은 wire protocol을 사용해 두 단계 테스트를 수행합니다.

1. phase 1: CLIENT -> SERVER
2. phase 2: SERVER -> CLIENT

각 phase가 끝나면 양쪽 통계가 교환되고, 최종 결과는 `TestRunResult` 하나로 정리됩니다.

## 프로젝트 구조

```text
MyIperf/
├── include/myiperf/          # public headers
├── src/myiperf/              # core implementation and private headers
├── src/myiperf/platform/     # Windows IOCP, Linux epoll network interfaces
├── app/ipeftc/               # CLI entry point and argument handling
├── third_party/nlohmann/     # bundled nlohmann/json
├── UML/                      # design and handoff documents
└── CMakeLists.txt
```

주요 CMake 타깃:

| 타깃 | 설명 |
| --- | --- |
| `myiperf_core` | 네트워크 I/O, protocol, config, logger, test controller를 담은 정적 라이브러리 |
| `IPEFTC` | `myiperf_core`를 사용하는 CLI 실행 파일 |

## 빌드

저장소 루트에서 실행합니다.

```powershell
cmake -S .\MyIperf -B .\MyIperf\build
cmake --build .\MyIperf\build --config Release
```

`MyIperf` 디렉터리 안에서 실행할 수도 있습니다.

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Windows Release 산출물:

```text
MyIperf\build\lib\Release\myiperf_core.lib
MyIperf\build\bin\Release\IPEFTC.exe
```

Debug 빌드는 `--config Debug`를 사용합니다.

## CLI 사용법

서버:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --mode server --target 0.0.0.0 --port 5201 --save-logs false
```

클라이언트:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --mode client --target 127.0.0.1 --port 5201 --packet-size 8192 --num-packets 10000 --save-logs false
```

결과 파일을 명시하는 실행:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --mode client `
    --target 127.0.0.1 `
    --port 5201 `
    --packet-size 8192 `
    --num-packets 10000 `
    --run-id smoke-001 `
    --result-dir Results `
    --result-json Results\client-smoke-001.json
```

도움말:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --help
```

## CLI 옵션

| 옵션 | 설명 | 기본값 |
| --- | --- | --- |
| `--mode <client|server>` | 실행 모드 | 필수 |
| `--config <path>` | JSON 설정 파일. CLI 옵션이 파일 값을 override | 없음 |
| `--target <ip>` | client target IP 또는 server bind IP | `127.0.0.1` |
| `--port <port>` | 테스트 TCP 포트 | `5201` |
| `--packet-size <bytes>` | 전송 packet 크기. header 포함 | `1024` |
| `--num-packets <count>` | 전송 packet 개수. `0`이면 수동 중단 전까지 무제한 | `0` |
| `--interval-ms <ms>` | packet 전송 간격 | `0` |
| `--save-logs <true|false>` | `Log` 디렉터리에 로그 저장 | `false` |
| `--handshake-timeout-ms <ms>` | client가 `CONFIG_ACK`를 기다리는 시간 | `5000` |
| `--run-id <id>` | 결과 파일/API/event에 기록할 안정적인 실행 ID | 자동 생성 |
| `--result-dir <path>` | `result-<runId>-<ROLE>.json` 저장 디렉터리 | `Results` |
| `--result-json <path>` | 기본 결과 파일 외에 동일 결과를 지정 경로에도 저장 | 없음 |
| `--result-pipe <name>` | 별도 result pipe로 JSON Lines 이벤트 전송 | 없음 |
| `--quiet <true|false>`, `-q <true|false>` | 콘솔 출력 제어. 현재 parser에서 지원 | 콘솔 출력 |
| `--version`, `-v` | 버전 출력 | - |
| `--help`, `-h` | 도움말 출력 | - |

`RunOptions` 계열 옵션(`--run-id`, `--result-dir`, `--result-json`, `--result-pipe`)은 로컬 실행 결과 전달에만 사용됩니다. 원격 peer로 전송되는 `Config`에는 포함되지 않습니다.

## 결과 전달 방식

`MyIperf`는 같은 `TestRunResult`를 원본으로 사용해 세 가지 방식으로 결과를 전달합니다.

### 1. JSON 결과 파일

기본 저장 파일:

```text
result-<runId>-CLIENT.json
result-<runId>-SERVER.json
latest-CLIENT.json
latest-SERVER.json
```

정확한 실행 결과를 프로그램에서 소비할 때는 반드시 `runId`가 포함된 파일을 읽습니다. `latest-CLIENT.json`, `latest-SERVER.json`은 사람이 가장 최근 결과를 확인하기 위한 편의 파일이며, 병렬 실행 또는 multi-port 실행에서는 원하는 run과 다를 수 있습니다.

`--result-json <path>`를 지정하면 기본 파일에 더해 지정 경로에도 같은 결과를 저장합니다.

저장은 임시 파일에 먼저 쓴 뒤 replace합니다.

- Windows: `MoveFileExA(... MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`
- POSIX: `std::filesystem::rename`

### 2. C++ 라이브러리 API

공개 API:

```cpp
void startTest(const Config& config);
void startTest(const Config& config, const RunOptions& options);
std::future<TestRunResult> runTestAsync(const Config& config,
                                        const RunOptions& options = RunOptions{});
TestRunResult getLastResult() const;
```

예시:

```cpp
#include "myiperf/Config.h"
#include "myiperf/RunOptions.h"
#include "myiperf/TestController.h"
#include "myiperf/TestRunResult.h"

Config config;
config.setMode(Config::TestMode::CLIENT);
config.setTargetIP("127.0.0.1");
config.setPort(5201);
config.setPacketSize(8192);
config.setNumPackets(10000);

RunOptions options;
options.runId = "api-run-001";
options.resultDir = "Results";

TestController controller;
TestRunResult result = controller.runTestAsync(config, options).get();

if (!result.success) {
    // result.failureReason contains the final reason.
}
```

`runTestAsync()`는 내부 completion future를 사용합니다. 같은 실행에 대해 외부에서 `getTestCompletionFuture()`를 동시에 소유하려고 하면 `std::promise::get_future()`의 1회 호출 제약 때문에 문제가 될 수 있습니다. 라이브러리 사용자는 `runTestAsync()` 또는 기존 completion future 중 하나의 소유 흐름만 선택해야 합니다.

### 3. Result pipe / event stream

`--result-pipe <name>`을 지정하면 별도 result pipe로 JSON Lines 이벤트를 publish합니다.

지원 이벤트:

| event type | 시점 | 주요 내용 |
| --- | --- | --- |
| `run_started` | test 시작 직후 | `runId`, `role`, `startedAt`, `config` |
| `phase_result` | 각 phase summary 생성 직후 | `runId`, `role`, `phaseNumber`, `phase` |
| `final_result` | 최종 결과 확정 후 | `runId`, `role`, `result` |

Windows에서는 pipe 이름이 `\\.\pipe\` prefix 없이 들어와도 내부적으로 정규화합니다.

result pipe는 best-effort입니다.

- reader가 없어도 테스트는 계속됩니다.
- pipe 전송 실패는 테스트 실패로 만들지 않습니다.
- pipe 오류는 `Warning:` 로그로만 남깁니다.
- 정확한 최종 판정은 JSON 결과 파일 또는 `getLastResult()`를 기준으로 확인합니다.

## 결과 타입

### `RunOptions`

```cpp
struct RunOptions {
    std::string runId;
    std::string resultDir = "Results";
    std::string resultJson;
    std::string resultPipe;
};
```

`runId`가 비어 있으면 `TestController`가 timestamp, process id, atomic counter 기반으로 자동 생성합니다.

### `TestPhaseResult`

```cpp
struct TestPhaseResult {
    std::string phaseName;
    std::string senderRole;
    std::string receiverRole;
    TestStats senderStats;
    TestStats receiverStats;
    bool success = false;
    std::string failureReason;
};
```

### `TestRunResult`

```cpp
struct TestRunResult {
    std::string schemaVersion = "1";
    std::string runId;
    std::string role;
    std::string startedAt;
    std::string finishedAt;
    std::string finalState;
    bool success = false;
    std::string failureReason;
    std::string resultExportWarning;
    Config config;
    TestPhaseResult phase1;
    TestPhaseResult phase2;
};
```

주요 phase 의미:

| phase | 방향 | sender | receiver |
| --- | --- | --- | --- |
| `phase1` | client-to-server | `CLIENT` | `SERVER` |
| `phase2` | server-to-client | `SERVER` | `CLIENT` |

## JSON 예시

```json
{
  "schemaVersion": "1",
  "runId": "smoke-001",
  "role": "CLIENT",
  "startedAt": "2026-05-06T09:00:00",
  "finishedAt": "2026-05-06T09:00:01",
  "finalState": "FINISHED",
  "success": true,
  "failureReason": "",
  "resultExportWarning": "",
  "config": {
    "packetSize": 8192,
    "numPackets": 10000,
    "sendIntervalMs": 0,
    "protocol": "TCP",
    "targetIP": "127.0.0.1",
    "port": 5201,
    "mode": "CLIENT",
    "saveLogs": false,
    "handshakeTimeoutMs": 5000
  },
  "phase1": {
    "phaseName": "client_to_server",
    "senderRole": "CLIENT",
    "receiverRole": "SERVER",
    "success": true,
    "failureReason": ""
  },
  "phase2": {
    "phaseName": "server_to_client",
    "senderRole": "SERVER",
    "receiverRole": "CLIENT",
    "success": true,
    "failureReason": ""
  }
}
```

실제 JSON에는 각 phase에 `senderStats`, `receiverStats`가 포함됩니다.

## 성공/실패 판정

`TestRunResult.success`는 다음 조건을 모두 만족해야 `true`입니다.

- 최종 상태가 `FINISHED`
- phase 1, phase 2가 모두 성공
- `numPackets > 0`이면 receiver packet count가 기대 packet count와 일치
- `numPackets > 0`이면 receiver byte count가 `packetSize * numPackets`와 일치
- checksum failure, sequence error, content mismatch가 모두 0

결과 export 실패는 네트워크 테스트 실패로 처리하지 않습니다. 대신 `Warning:` 로그와 `resultExportWarning`에 남깁니다.

## 로그와 출력

`Logger`는 콘솔 출력과 파일 로그를 지원합니다.

- `--save-logs true`: `Log` 디렉터리에 로그 저장
- `--quiet true`: 콘솔 출력 비활성화

최종 콘솔 리포트는 local stats와 remote stats를 함께 보여줍니다.

- CLIENT local stats: client가 보낸 데이터
- CLIENT remote stats: server가 받은 데이터
- SERVER local stats: server가 받은 데이터
- SERVER remote stats: client가 보낸 데이터

## 검증 명령

빌드:

```powershell
cmake --build .\MyIperf\build --config Release
cmake --build .\MyIperf\build --config Debug
```

도움말:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --help
```

JSON smoke test 후 확인할 항목:

- server/client exit code 0
- `result-<runId>-CLIENT.json`
- `result-<runId>-SERVER.json`
- `latest-CLIENT.json`
- `latest-SERVER.json`
- JSON의 `success: true`
- JSON의 `finalState: FINISHED`

## 개발 참고

- 공개 API를 사용하는 코드는 `#include "myiperf/..."` 형식으로 include합니다.
- `Config`는 원격 peer에 전송되는 테스트 설정입니다.
- `RunOptions`는 로컬 실행 결과 전달 옵션이며 peer로 전송하지 않습니다.
- `PacketGenerator`, `PacketReceiver`, 플랫폼별 `NetworkInterface` 구현은 `myiperf_core`의 private 구현 세부사항입니다.
- `CLIHandler`와 `app/ipeftc/main.cpp`는 애플리케이션 계층이며 `myiperf_core` public API에는 포함되지 않습니다.

## 알려진 주의사항

- `resultExportWarning`은 export 도중 생긴 저장 경고입니다. 먼저 저장된 일부 JSON 파일에는 이후 발생한 export warning이 반영되지 않을 수 있습니다.
- `runTestAsync()`와 기존 completion future를 같은 실행에서 동시에 소유하지 않습니다.
- result pipe는 실시간 관찰용 best-effort 채널입니다. 최종 결과 확인은 JSON 파일 또는 API를 우선합니다.
- 현재 protocol은 TCP만 지원합니다.

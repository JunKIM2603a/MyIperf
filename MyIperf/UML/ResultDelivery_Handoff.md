# Result Delivery Handoff

작성일: 2026-05-06

이 문서는 새 대화 컨텍스트에서 현재 작업을 그대로 이어가기 위한 인수인계 문서이다.  
목표는 `MyIperf` 실행 결과를 호출 프로그램 또는 라이브러리 사용자에게 안정적으로 전달하는 기능의 현재 상태, 변경 파일, 검증 결과, 남은 주의사항을 보존하는 것이다.

## 현재 목표

`MyIperf`의 기존 네트워크 handshake와 `Config` 전송 동작은 유지하면서, 실행 결과 전달 기능을 별도 런타임 계층으로 추가했다.

결과 전달 방식은 세 가지를 모두 지원한다.

1. JSON 결과 파일
2. 라이브러리 API
3. result pipe/event stream

공통 결과 원본은 `TestRunResult`이다.  
`TestRunner`는 JSON 결과 파일을 우선 사용하고, 파일이 없거나 파싱 실패 시 기존 stdout 파서를 fallback으로 사용한다.

## 작업트리 상태

현재 `git status --short` 기준 변경 파일은 다음과 같다.

```text
 M MyIperf/CMakeLists.txt
 M MyIperf/app/ipeftc/CLIHandler.cpp
 M MyIperf/app/ipeftc/CLIHandler.h
 M MyIperf/include/myiperf/TestController.h
 M MyIperf/src/myiperf/ClientTestSession.cpp
 M MyIperf/src/myiperf/ServerTestSession.cpp
 M MyIperf/src/myiperf/TestController.cpp
 M MyIperf/src/myiperf/TestSessionContext.h
 M TestRunner/ControlClient.cpp
 M TestRunner/ControlServer.cpp
 M TestRunner/Message.cpp
 M TestRunner/Message.h
 M TestRunner/ProcessManager.cpp
 M TestRunner/ProcessManager.h
 M TestRunner/main.cpp
?? MyIperf/include/myiperf/RunOptions.h
?? MyIperf/include/myiperf/TestRunResult.h
?? MyIperf/src/myiperf/ResultEventSink.cpp
?? MyIperf/src/myiperf/ResultEventSink.h
```

주의: 새 파일 4개는 아직 git에 tracked 상태가 아니다.

## 추가된 MyIperf 파일

### `MyIperf/include/myiperf/RunOptions.h`

테스트 실행 자체에만 필요한 런타임 옵션이다.  
원격 peer로 전달되는 `Config`에는 넣지 않는다.

주요 필드:

- `runId`
- `resultDir`
- `resultJson`
- `resultPipe`

기본 `resultDir`은 `Results`이다.  
`runId`가 비어 있으면 `TestController`가 timestamp, pid, atomic counter 기반으로 생성한다.

### `MyIperf/include/myiperf/TestRunResult.h`

구조화된 테스트 결과 타입과 JSON serializer/deserializer를 포함한다.

주요 타입:

- `TestPhaseResult`
- `TestRunResult`

`TestRunResult` 주요 필드:

- `schemaVersion`
- `runId`
- `role`
- `startedAt`
- `finishedAt`
- `finalState`
- `success`
- `failureReason`
- `resultExportWarning`
- `Config config`
- `phase1`
- `phase2`

### `MyIperf/src/myiperf/ResultEventSink.h`
### `MyIperf/src/myiperf/ResultEventSink.cpp`

result pipe/event stream 전송 담당 클래스이다.

특징:

- `--result-pipe`가 설정된 경우에만 동작한다.
- JSON line 이벤트를 전송한다.
- 이벤트 전송은 best-effort이다.
- reader가 없어도 테스트는 계속된다.
- pipe 오류는 `Warning:` 로그만 남기고 테스트 실패로 만들지 않는다.
- 테스트 coroutine/completion path에서 blocking I/O를 직접 하지 않도록 내부 queue와 worker thread를 사용한다.

지원 이벤트:

- `run_started`
- `phase_result`
- `final_result`

Windows named pipe 관련 구현 메모:

- `PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT`로 생성한다.
- 연결 후 `SetNamedPipeHandleState(... PIPE_WAIT ...)`로 wait mode를 설정한다.
- `WriteFile` 후 `FlushFileBuffers`를 호출해 여러 JSON line이 reader에 도착하도록 한다.
- reader 연결이 약간 늦는 경우를 위해 worker가 짧게 retry한다.

## 변경된 MyIperf 구조

### CMake

`MyIperf/CMakeLists.txt`에 다음 항목이 추가되었다.

- public header:
  - `RunOptions.h`
  - `TestRunResult.h`
- private header:
  - `ResultEventSink.h`
- source:
  - `ResultEventSink.cpp`

### CLI

변경 파일:

- `MyIperf/app/ipeftc/CLIHandler.h`
- `MyIperf/app/ipeftc/CLIHandler.cpp`

새 CLI 옵션:

```text
--run-id <id>
--result-dir <dir>
--result-json <path>
--result-pipe <name>
```

`CLIHandler`는 이제 `Config`만 반환하지 않고 내부적으로 다음 구조를 사용한다.

```cpp
struct ParsedCommandLine {
    Config config;
    RunOptions runOptions;
};
```

`handleCommand()`는 `testController.startTest(config, runOptions)`를 호출한다.

### TestSessionContext

변경 파일:

- `MyIperf/src/myiperf/TestSessionContext.h`

추가된 필드:

```cpp
std::function<void(int)> notifyPhaseComplete;
```

session이 phase summary를 만든 직후 `TestController`에 phase 완료 이벤트를 알려주기 위한 콜백이다.

### ClientTestSession / ServerTestSession

변경 파일:

- `MyIperf/src/myiperf/ClientTestSession.cpp`
- `MyIperf/src/myiperf/ServerTestSession.cpp`

각 phase summary 생성 뒤 다음 호출이 추가되었다.

```cpp
context.notifyPhaseComplete(1);
context.notifyPhaseComplete(2);
```

이 호출을 통해 `ResultEventSink`가 `phase_result` 이벤트를 내보낼 수 있다.

### TestController

변경 파일:

- `MyIperf/include/myiperf/TestController.h`
- `MyIperf/src/myiperf/TestController.cpp`

기존 API는 유지한다.

```cpp
void startTest(const Config& config);
```

새 API:

```cpp
void startTest(const Config& config, const RunOptions& options);
std::future<TestRunResult> runTestAsync(const Config& config, const RunOptions& options = RunOptions{});
TestRunResult getLastResult() const;
```

결과 lifecycle 핵심 함수:

```cpp
void finalizeResultOnce(const std::string& failureReason = "");
TestRunResult buildCurrentResult(const std::string& failureReason) const;
std::string exportResult(const TestRunResult& result);
void publishRunStarted();
void publishPhaseResult(int phaseNumber);
```

결과 확정 순서:

1. `TestRunResult` 확정
2. JSON 저장
3. `final_result` pipe event 전송
4. `ResultEventSink` 정리
5. `signalCompletion()`

이 순서는 중요하다.  
호출 프로그램이 completion future가 풀린 직후 `getLastResult()` 또는 JSON 파일을 읽어도 최신 결과를 보게 하기 위함이다.

## 결과 JSON 파일 정책

기본 파일:

```text
result-<runId>-CLIENT.json
result-<runId>-SERVER.json
latest-CLIENT.json
latest-SERVER.json
```

`--result-json <path>`를 지정하면 지정 경로에도 추가 저장한다.

저장은 임시 파일에 먼저 쓴 뒤 replace한다.

- Windows: `MoveFileExA(... MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`
- POSIX: `std::filesystem::rename`

`latest-CLIENT.json`, `latest-SERVER.json`은 사람이 최근 결과를 확인하기 위한 편의 파일이다.  
호출 프로그램이 정확한 최신 결과를 원하면 반드시 `runId`가 포함된 `result-<runId>-<ROLE>.json`을 읽어야 한다.

## 성공/실패 판정

`TestRunResult.success`는 다음 조건을 기준으로 한다.

- 최종 상태가 `FINISHED`여야 한다.
- phase 1, phase 2가 모두 성공이어야 한다.
- `numPackets > 0`이면 receiver packet count와 byte count가 기대값과 맞아야 한다.
- checksum failure, sequence error, content mismatch가 모두 0이어야 한다.

phase 의미:

- phase 1: client-to-server
  - sender: CLIENT
  - receiver: SERVER
- phase 2: server-to-client
  - sender: SERVER
  - receiver: CLIENT

결과 export 실패는 네트워크 테스트 실패로 만들지 않는다.  
대신 `Warning:` 로그와 `resultExportWarning`에 남긴다.

## 변경된 TestRunner 구조

### Message

변경 파일:

- `TestRunner/Message.h`
- `TestRunner/Message.cpp`

`TestConfig`에 추가된 필드:

```cpp
std::string runId;
std::string resultDir = "Results";
```

JSON control protocol에도 `runId`, `resultDir`가 포함된다.

### main

변경 파일:

- `TestRunner/main.cpp`

새 옵션:

```text
--result-dir <dir>
```

각 run/port마다 고유 `runId`를 생성한다.

형식:

```text
tr-run<run>-port<port>-<counter>
```

### ProcessManager

변경 파일:

- `TestRunner/ProcessManager.h`
- `TestRunner/ProcessManager.cpp`

IPEFTC 실행 시 다음 옵션을 추가 전달한다.

```text
--run-id <runId>
--result-dir <resultDir>
```

새 함수:

```cpp
bool ParseResultFile(const TestConfig& config,
                     const std::string& role,
                     TestResult& result,
                     std::string& error);
```

동작:

- `result-<runId>-CLIENT.json` 또는 `result-<runId>-SERVER.json`을 읽는다.
- JSON 내부 `runId`가 요청한 값과 같은지 확인한다.
- MyIperf JSON 결과를 TestRunner `TestResult`로 변환한다.
- 실패하면 error message를 채우고 false를 반환한다.

role별 우선 stats:

- Client 결과:
  - phase 2 receiver stats가 CLIENT이면 우선 사용
  - 없으면 phase 1 sender stats fallback
- Server 결과:
  - phase 1 receiver stats가 SERVER이면 우선 사용
  - 없으면 phase 2 sender stats fallback

### ControlClient / ControlServer

변경 파일:

- `TestRunner/ControlClient.cpp`
- `TestRunner/ControlServer.cpp`

IPEFTC 실행 후 처리 순서:

1. JSON result file 파싱 시도
2. 실패 시 stderr에 이유 출력
3. 기존 stdout parser fallback

`ControlServer`는 `CONFIG_REQUEST` 수신 시 현재 `TestConfig`를 보관한다.  
서버 측 IPEFTC 결과 JSON을 찾기 위해 필요하다.

## 검증 완료 내역

다음 명령들이 성공했다.

```powershell
cmake -S .\MyIperf -B .\MyIperf\build
cmake -S .\TestRunner -B .\TestRunner\build
cmake --build .\MyIperf\build --config Release
cmake --build .\MyIperf\build --config Debug
cmake --build .\TestRunner\build --config Release
cmake --build .\TestRunner\build --config Debug
```

CLI help 확인:

- `IPEFTC --help`에 `--run-id`, `--result-dir`, `--result-json`, `--result-pipe` 표시 확인
- `TestRunner --help`에 `--result-dir` 표시 확인

MyIperf JSON smoke test:

- server/client exit code 0
- `result-smoke-json-001-CLIENT.json`
- `result-smoke-json-001-SERVER.json`
- `latest-CLIENT.json`
- `latest-SERVER.json`
- JSON의 `success: true`, `finalState: FINISHED` 확인

result pipe no-reader test:

- `--result-pipe` 지정
- pipe reader 없음
- 테스트 정상 종료
- JSON 파일 정상 생성

result pipe reader test:

- PowerShell background job으로 Windows named pipe reader 실행
- client가 다음 4개 JSON line 이벤트를 전송하는 것 확인
  - `run_started`
  - `phase_result`
  - `phase_result`
  - `final_result`

TestRunner JSON-first smoke test:

- 단일 port 실행 성공
- client/server 모두 PASS
- `result-tr-run...-CLIENT.json`, `result-tr-run...-SERVER.json` 생성
- stdout fallback 메시지 없음

TestRunner fallback test:

- 존재하지 않는 result dir 사용
- JSON 파일 생성 실패
- TestRunner가 stderr에 fallback 이유 출력
- 기존 stdout parser로 PASS 처리 확인

TestRunner multi-port test:

- `--num-ports 2`
- 두 port 모두 PASS
- role별 result JSON 총 4개 생성 확인

프로세스 잔류 확인:

```powershell
Get-Process IPEFTC,TestRunner -ErrorAction SilentlyContinue
```

잔류 프로세스 없음.

## 알려진 주의사항

### `resultExportWarning`

결과 export 도중 일부 파일 저장에 실패하면 `m_lastResult.resultExportWarning`에는 경고가 남는다.  
다만 이미 먼저 저장된 JSON 파일에는 이후에 생긴 export warning이 포함되지 않을 수 있다.

현재 정책상 export 실패는 테스트 실패로 보지 않으므로 큰 문제는 아니다.  
추후 개선하려면 모든 write 결과를 먼저 수집한 뒤 최종 JSON을 한 번 더 확정하는 방식으로 정리할 수 있다.

### `runTestAsync()`

`runTestAsync()`는 내부적으로 completion future를 사용한다.  
같은 실행에 대해 외부에서 `getTestCompletionFuture()`를 동시에 호출하면 `std::promise::get_future()`는 한 번만 호출 가능하므로 문제가 될 수 있다.

추후 라이브러리 API를 더 안정화하려면 completion future 소유 정책을 명확히 문서화하거나 shared future 구조로 바꾸는 것을 고려한다.

### result pipe

result pipe는 best-effort이다.

- reader가 없어도 테스트는 계속된다.
- pipe 전송 실패는 테스트 실패로 만들지 않는다.
- 정확한 최종 결과 확인은 JSON 파일 또는 `getLastResult()`를 우선으로 한다.

### TestRunner stdout fallback

TestRunner의 stdout parser fallback은 의도적으로 유지했다.  
이전 버전 IPEFTC 또는 JSON 저장 실패 상황에서도 TestRunner가 가능한 한 동작하도록 하기 위함이다.

## 다음 컨텍스트에서 이어갈 때 할 일

새 대화에서 다음 순서로 진행하면 된다.

1. 이 파일을 먼저 읽는다.
2. `git status --short`로 변경 상태를 확인한다.
3. 새 파일 4개가 여전히 untracked인지 확인한다.
4. 빌드가 깨지지 않는지 다시 확인한다.
5. 필요하면 `git diff`를 검토하고 커밋 준비를 한다.

권장 확인 명령:

```powershell
git status --short
cmake --build .\MyIperf\build --config Release
cmake --build .\TestRunner\build --config Release
```

결과 기능 smoke test 예시:

```powershell
.\MyIperf\build\bin\Release\IPEFTC.exe --help
.\TestRunner\build\Release\TestRunner.exe --help
```

TestRunner 로컬 smoke test는 server/client 두 프로세스를 함께 실행해야 한다.  
이미 이전 컨텍스트에서 단일 port, fallback, multi-port까지 검증했다.

## 중요한 설계 원칙

- `Config`는 네트워크 peer에 전달되는 시험 설정이다.
- `RunOptions`는 로컬 실행 결과 전달을 위한 런타임 옵션이다.
- result pipe는 로그 pipe와 섞지 않는다.
- 최종 completion은 결과 확정 이후에 신호해야 한다.
- 정확한 최신 결과는 `latest`가 아니라 `runId` 기반 파일 또는 API로 조회한다.
- 기존 MyIperf wire protocol과 TestRunner control protocol 호환성은 유지한다.


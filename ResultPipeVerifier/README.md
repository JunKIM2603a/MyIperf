# ResultPipeVerifier

`ResultPipeVerifier`는 `IPEFTC --result-pipe` event stream을 Windows와 Linux에서 자동 검증하는 독립 C++/CMake smoke test 도구입니다.

이 도구는 `IPEFTC` server/client 프로세스를 직접 실행하고 다음 항목을 확인합니다.

- server/client 양쪽 result pipe 이벤트 수신
- 이벤트 순서: `run_started`, `phase_result`, `phase_result`, `final_result`
- 이벤트 `runId`, `role`, `phaseNumber`
- `final_result.result.success == true`
- `final_result.result.finalState == "FINISHED"`
- `result-<runId>-CLIENT.json`, `result-<runId>-SERVER.json` 생성 및 성공 상태

## 빌드

저장소 루트에서 실행합니다.

```powershell
cmake -S .\ResultPipeVerifier -B .\ResultPipeVerifier\build
cmake --build .\ResultPipeVerifier\build --config Release
```

Linux:

```bash
cmake -S ./ResultPipeVerifier -B ./ResultPipeVerifier/build -DCMAKE_BUILD_TYPE=Release
cmake --build ./ResultPipeVerifier/build
```

## 실행

Windows:

```powershell
.\ResultPipeVerifier\build\Release\ResultPipeVerifier.exe `
    --ipeftc-path .\MyIperf\build\bin\Release\IPEFTC.exe
```

Linux:

```bash
./ResultPipeVerifier/build/ResultPipeVerifier \
    --ipeftc-path ./MyIperf/build/bin/Release/IPEFTC
```

기본 실행은 다음 설정으로 smoke test를 수행합니다.

| 옵션 | 기본값 |
| --- | --- |
| `--host` | `127.0.0.1` |
| `--bind` | `0.0.0.0` |
| `--port` | `55301` |
| `--packet-size` | `1024` |
| `--num-packets` | `5` |
| `--interval-ms` | `1` |
| `--timeout-ms` | `30000` |
| `--result-dir` | `ResultPipeVerifier/results/<runId>` |

## 옵션

```text
--ipeftc-path <path>   Path to IPEFTC executable
--host <ip>            Client target IP
--bind <ip>            Server bind IP
--port <port>          Test port
--packet-size <bytes>  Packet size
--num-packets <count>  Packet count
--interval-ms <ms>     Send interval
--run-id <id>          Stable run ID
--result-dir <path>    Output directory
--timeout-ms <ms>      Overall timeout
-h, --help             Show help
```

`--ipeftc-path`를 생략하면 현재 작업 디렉터리와 일반적인 `MyIperf/build/bin/<config>` 경로에서 `IPEFTC`를 자동 탐색합니다.

## 산출물

검증 결과는 `--result-dir` 아래에 저장됩니다.

```text
client-events.jsonl
server-events.jsonl
client-output.log
server-output.log
verification-report.json
result-<runId>-CLIENT.json
result-<runId>-SERVER.json
latest-CLIENT.json
latest-SERVER.json
```

`verification-report.json`에는 pass/fail, 오류 목록, process exit code, 이벤트 개수, 결과 파일 경로가 포함됩니다.

## 동작 방식

`ResultPipeVerifier`는 result pipe reader만 띄우는 도구가 아니라, `IPEFTC` 서버와 클라이언트를 모두 실행해서 end-to-end로 검증하는 smoke test runner입니다. 한 번 실행하면 다음 순서로 동작합니다.

1. 옵션을 파싱하고 `runId`, `resultDir`, 테스트 포트, packet 수를 확정합니다.
2. `--ipeftc-path`가 없으면 현재 작업 디렉터리와 `MyIperf/build/bin/<config>` 후보에서 `IPEFTC` 실행 파일을 찾습니다.
3. `resultDir`을 만들고 server/client용 result pipe 이름을 준비합니다.
4. server/client pipe reader thread를 먼저 시작합니다.
5. `IPEFTC --mode server`를 실행하고 server가 `ACCEPTING` 상태가 될 때까지 stdout을 감시합니다.
6. server 준비가 확인되면 `IPEFTC --mode client`를 실행합니다.
7. client와 server 프로세스가 종료될 때까지 기다립니다.
8. pipe reader를 정리하고, 수집한 이벤트와 process output을 파일로 저장합니다.
9. event stream과 결과 JSON 파일을 검증합니다.
10. 콘솔 summary와 `verification-report.json`을 출력합니다.

즉, verifier가 보는 검증 대상은 두 가지입니다.

- 실시간 result pipe event stream
- `IPEFTC`가 최종적으로 저장한 `result-<runId>-<ROLE>.json`

두 경로가 모두 성공해야 최종 결과가 PASS가 됩니다.

## 실행 중 생성되는 IPEFTC 명령

기본 옵션으로 실행하면 verifier는 내부적으로 아래와 같은 형태의 `IPEFTC` server/client를 실행합니다. 실제 pipe 이름과 runId는 실행마다 달라집니다.

Server:

```text
IPEFTC --mode server
       --target 0.0.0.0
       --port 55301
       --run-id <runId>
       --result-dir <resultDir>
       --result-pipe <serverPipe>
       --save-logs false
```

Client:

```text
IPEFTC --mode client
       --target 127.0.0.1
       --port 55301
       --packet-size 1024
       --num-packets 5
       --interval-ms 1
       --run-id <runId>
       --result-dir <resultDir>
       --result-pipe <clientPipe>
       --save-logs false
```

server와 client는 같은 `runId`와 `resultDir`을 사용합니다. 그래서 verifier는 양쪽 이벤트와 양쪽 JSON 결과가 같은 실행에 속하는지 검증할 수 있습니다.

## Pipe 이름과 연결 방식

Windows와 Linux는 result pipe 구현이 다릅니다.

| 플랫폼 | `IPEFTC`에 전달하는 값 | verifier 연결 방식 |
| --- | --- | --- |
| Windows | `rpv-<runId>-server`, `rpv-<runId>-client` | `\\.\pipe\<name>`에 `CreateFileA`로 연결 |
| Linux | `<resultDir>/server.fifo`, `<resultDir>/client.fifo` | verifier가 `mkfifo`로 FIFO를 만든 뒤 non-blocking read |

Windows에서는 `IPEFTC`가 named pipe server를 생성합니다. verifier는 pipe가 생길 때까지 짧게 retry하면서 client로 연결합니다.

Linux에서는 verifier가 FIFO 파일을 먼저 만듭니다. 그 뒤 `IPEFTC`가 같은 path를 writer로 열고 JSON Lines를 씁니다.

## Event stream 검증

server pipe와 client pipe 각각에서 정확히 4개의 JSON Lines 이벤트가 와야 합니다.

```text
run_started
phase_result
phase_result
final_result
```

각 이벤트는 다음 조건을 만족해야 합니다.

| 항목 | 조건 |
| --- | --- |
| `type` | 기대 순서와 일치 |
| `runId` | verifier가 이번 실행에 지정한 runId와 일치 |
| `role` | server pipe는 `SERVER`, client pipe는 `CLIENT` |
| `phaseNumber` | 첫 번째 phase event는 `1`, 두 번째 phase event는 `2` |
| `phase` | phase event에 object로 존재 |
| `result` | final event에 object로 존재 |
| `result.success` | `true` |
| `result.finalState` | `FINISHED` |
| `result.role` | pipe role과 일치 |
| `result.runId` | 이번 runId와 일치 |

이벤트가 4개보다 적거나, 순서가 바뀌거나, role/runId가 다르면 FAIL입니다.

## 결과 JSON 검증

event stream 검증 후 verifier는 `resultDir`에서 아래 파일을 확인합니다.

```text
result-<runId>-SERVER.json
result-<runId>-CLIENT.json
```

각 파일은 다음 조건을 만족해야 합니다.

| 항목 | 조건 |
| --- | --- |
| 파일 존재 | true |
| `runId` | 이번 runId와 일치 |
| `role` | 파일 role과 일치 |
| `success` | `true` |
| `finalState` | `FINISHED` |

`latest-SERVER.json`, `latest-CLIENT.json`도 `IPEFTC`가 생성하지만 verifier의 정확한 판정에는 `result-<runId>-<ROLE>.json`만 사용합니다.

## 프로세스와 타임아웃 처리

`ResultPipeVerifier`는 server/client 프로세스 stdout과 stderr를 하나의 로그로 capture합니다.

- server output: `server-output.log`
- client output: `client-output.log`

server는 먼저 실행되며, stdout에서 다음 readiness marker 중 하나가 보이면 준비 완료로 판단합니다.

- `Transitioning to state: ACCEPTING`
- `Server is running. Waiting for the test to complete`

server가 준비되지 않으면 client를 실행하지 않고 실패 처리합니다.

`--timeout-ms`는 전체 smoke test에서 사용하는 주요 timeout입니다. client 또는 server가 timeout 안에 종료되지 않으면 verifier가 child process를 종료하고 FAIL로 기록합니다.

## 플랫폼별 동작

Windows:

- `IPEFTC`가 named pipe server를 생성합니다.
- verifier는 `CreateFileA("\\\\.\\pipe\\...")`로 pipe에 연결합니다.
- `ERROR_BROKEN_PIPE`, `ERROR_PIPE_NOT_CONNECTED`는 writer 종료 시 나올 수 있으므로 정상 종료로 처리합니다.

Linux:

- verifier가 `<resultDir>/server.fifo`, `<resultDir>/client.fifo`를 먼저 생성합니다.
- `IPEFTC`는 해당 FIFO path로 JSON Lines를 씁니다.
- verifier는 non-blocking read loop로 writer open/close 사이의 EOF를 기다리며 처리합니다.

## 실패 시 확인

- `server-output.log`, `client-output.log`에서 IPEFTC 실행 오류를 확인합니다.
- `verification-report.json`의 `errors` 배열을 확인합니다.
- 포트 충돌이 있으면 `--port` 값을 바꿉니다.
- Windows 방화벽 또는 Linux firewall이 local TCP test port를 막고 있지 않은지 확인합니다.

## verification-report.json 읽는 법

예시:

```json
{
  "passed": true,
  "errors": [],
  "events": {
    "serverCount": 4,
    "clientCount": 4,
    "serverFile": "ResultPipeVerifier/results/<runId>/server-events.jsonl",
    "clientFile": "ResultPipeVerifier/results/<runId>/client-events.jsonl"
  },
  "processes": {
    "server": {
      "started": true,
      "timedOut": false,
      "exitCode": 0
    },
    "client": {
      "started": true,
      "timedOut": false,
      "exitCode": 0
    }
  }
}
```

중요 필드:

| 필드 | 의미 |
| --- | --- |
| `passed` | 모든 검증 조건이 성공했는지 |
| `errors` | 실패 이유 목록. PASS면 빈 배열 |
| `events.serverCount` | server result pipe에서 읽은 이벤트 수 |
| `events.clientCount` | client result pipe에서 읽은 이벤트 수 |
| `processes.*.exitCode` | 각 `IPEFTC` 프로세스 종료 코드 |
| `processes.*.timedOut` | timeout으로 종료됐는지 |
| `resultFiles.*` | verifier가 확인한 role별 result JSON 경로 |

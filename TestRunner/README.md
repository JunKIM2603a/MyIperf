# TestRunner - 분산 IPEFTC 테스트 자동화

`TestRunner`는 `IPEFTC` 서버/클라이언트 프로세스를 TCP 제어 채널로 조율하는 분산 테스트 자동화 도구입니다. 원격 머신에는 `TestRunner --mode server`를 실행하고, 로컬 또는 제어 머신에는 `TestRunner --mode client`를 실행해 테스트 포트, packet 크기, packet 수, 반복 횟수, 멀티 포트 실행을 제어합니다.

최신 `TestRunner`는 `IPEFTC`가 생성한 구조화 JSON 결과 파일을 우선 파싱합니다. JSON 파일이 없거나 파싱에 실패하면 이유를 stderr에 출력한 뒤 기존 stdout parser로 fallback합니다.

## 주요 특징

- TCP control channel 기반 원격 실행 조율
- `IPEFTC` 서버 준비 확인 후 client 자동 시작
- 단일 포트, 멀티 포트, 반복 실행 지원
- run/port별 고유 `runId` 생성
- `IPEFTC` JSON 결과 파일 우선 수집
- JSON 결과 실패 시 stdout parser fallback
- client/server 결과를 같은 summary table로 출력

## 아키텍처

```text
Control layer
=============

TestRunner client                         TestRunner server
-----------------                         -----------------
CONFIG_REQUEST -------------------------> receive config
                                           launch IPEFTC server
SERVER_READY    <------------------------ server ready
launch IPEFTC client
wait for client completion
RESULTS_REQUEST ------------------------> parse server result
RESULTS_RESPONSE <----------------------- return server result
SERVER_SHUTDOWN ------------------------> stop control server


Test layer
==========

IPEFTC client       <==================>  IPEFTC server
                    TCP test port


Result layer
============

Each IPEFTC process writes:
  result-<runId>-CLIENT.json
  result-<runId>-SERVER.json
  latest-CLIENT.json
  latest-SERVER.json

TestRunner parses result-<runId>-<ROLE>.json first.
```

분산 머신에서 `--result-dir`이 상대 경로이면 각 머신의 해당 `TestRunner` 실행 위치 기준으로 해석됩니다. client 결과 JSON은 client 쪽 파일 시스템에, server 결과 JSON은 server 쪽 파일 시스템에 생성됩니다.

## 빌드

저장소 루트에서 실행합니다.

```powershell
cmake -S .\TestRunner -B .\TestRunner\build
cmake --build .\TestRunner\build --config Release
```

`TestRunner` 디렉터리 안에서 실행할 수도 있습니다.

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Windows Release 산출물:

```text
TestRunner\build\Release\TestRunner.exe
```

Debug 빌드는 `--config Debug`를 사용합니다.

## 빠른 실행

원격 또는 server 머신:

```powershell
.\TestRunner\build\Release\TestRunner.exe --mode server `
    --control-port 9500 `
    --ipeftc-path .\MyIperf\build\bin\Release\IPEFTC.exe
```

로컬 또는 client 머신:

```powershell
.\TestRunner\build\Release\TestRunner.exe --mode client `
    --server 127.0.0.1 `
    --control-port 9500 `
    --ipeftc-path .\MyIperf\build\bin\Release\IPEFTC.exe `
    --test-port 5201 `
    --packet-size 8192 `
    --num-packets 10000 `
    --interval-ms 0 `
    --result-dir Results
```

도움말:

```powershell
.\TestRunner\build\Release\TestRunner.exe --help
```

## 명령행 옵션

### 공통 옵션

| 옵션 | 설명 | 기본값 |
| --- | --- | --- |
| `--mode <server|client>` | 실행 모드 | 필수 |
| `--control-port <port>` | TestRunner control channel TCP 포트 | `9500` |
| `--ipeftc-path <path>` | 실행할 `IPEFTC` 경로 | 자동 탐색 |
| `--version`, `-v` | 버전 정보 출력 | - |
| `--help`, `-h` | 도움말 출력 | - |

`--ipeftc-path`를 생략하면 `ProcessManager`는 현재 작업 디렉터리, `MyIperf\build\bin\Release\IPEFTC.exe`, `MyIperf\build\bin\Debug\IPEFTC.exe` 등 알려진 후보를 순서대로 탐색합니다. 탐색 실패 시 후보 경로 목록을 출력합니다.

### 서버 모드

서버 모드는 공통 옵션만 사용합니다.

```powershell
.\TestRunner.exe --mode server --control-port 9500
```

### 클라이언트 모드

| 옵션 | 설명 | 기본값 |
| --- | --- | --- |
| `--server <ip>` | TestRunner server IP | 필수 |
| `--server-bind <ip>` | 원격 `IPEFTC` server가 bind할 IP | `0.0.0.0` |
| `--test-port <port>` | 첫 번째 `IPEFTC` 테스트 포트 | `5201` |
| `--packet-size <bytes>` | packet 크기 | `8192` |
| `--num-packets <count>` | 전송 packet 개수 | `10000` |
| `--interval-ms <ms>` | packet 전송 간격 | `0` |
| `--result-dir <path>` | `IPEFTC` 결과 JSON 저장 디렉터리 | `Results` |
| `--num-ports <count>` | 순차 테스트할 포트 수 | `1` |
| `--total-runs <count>` | 전체 반복 실행 횟수 | `1` |

`TestRunner` client는 각 run/port 조합마다 고유 `runId`를 생성합니다.

```text
tr-run<run>-port<port>-<counter>
```

예시:

```text
tr-run1-port5201-0
tr-run1-port5202-1
tr-run2-port5201-2
```

`--result-dir`은 이 `runId` 기반 결과 파일을 찾는 데 사용됩니다. 상대 경로를 쓰면 client와 server 각각의 실행 위치 기준으로 같은 문자열이 전달됩니다.

## 사용 시나리오

### 단일 포트 테스트

서버:

```powershell
.\TestRunner.exe --mode server --control-port 9500
```

클라이언트:

```powershell
.\TestRunner.exe --mode client `
    --server 192.168.1.100 `
    --control-port 9500 `
    --test-port 5201 `
    --packet-size 8192 `
    --num-packets 10000 `
    --result-dir Results
```

### 멀티 포트 테스트

```powershell
.\TestRunner.exe --mode client `
    --server 192.168.1.100 `
    --control-port 9500 `
    --test-port 5201 `
    --num-ports 5 `
    --packet-size 1500 `
    --num-packets 100000 `
    --result-dir Results
```

포트는 `--test-port`부터 순차 할당됩니다. 예를 들어 `--test-port 5201 --num-ports 5`이면 `5201`부터 `5205`까지 테스트합니다.

현재 client는 Windows IOCP 기반 `IPEFTC` child process 안정성을 위해 포트별 `IPEFTC` 실행을 직렬화합니다. 서버는 control client별 worker thread를 사용합니다.

### 반복 실행

```powershell
.\TestRunner.exe --mode client `
    --server 192.168.1.100 `
    --control-port 9500 `
    --test-port 5201 `
    --num-packets 10000 `
    --total-runs 10 `
    --result-dir Results
```

반복 실행은 같은 `TestRunner` client 프로세스 안에서 run 단위로 진행됩니다. 각 테스트의 `IPEFTC` child process는 새로 실행되며, run 사이에는 약 1초 대기합니다.

### 원격 server bind IP 지정

원격 서버 머신에 여러 NIC가 있거나 특정 interface에 bind해야 하면 `--server-bind`를 사용합니다.

```powershell
.\TestRunner.exe --mode client `
    --server 192.168.1.100 `
    --server-bind 10.10.0.25 `
    --test-port 5201
```

`--server`는 TestRunner control server에 접속할 IP이고, `--server-bind`는 원격 `IPEFTC --mode server --target <ip>`에 전달되는 bind IP입니다.

## 결과 수집

`ProcessManager`는 `IPEFTC` 실행 시 다음 옵션을 전달합니다.

```text
--run-id <runId>
--result-dir <resultDir>
```

각 role별로 다음 파일을 먼저 찾습니다.

```text
result-<runId>-CLIENT.json
result-<runId>-SERVER.json
```

JSON 파싱 절차:

1. 파일 존재 여부 확인
2. JSON 내부 `runId`가 요청한 `runId`와 같은지 확인
3. role별 우선 stats 추출
4. `success`, `failureReason`, checksum/sequence/content mismatch 반영

role별 우선 stats:

| TestRunner role | 우선 사용 | fallback |
| --- | --- | --- |
| `Client` | `phase2.receiverStats`가 `CLIENT`일 때 | `phase1.senderStats` |
| `Server` | `phase1.receiverStats`가 `SERVER`일 때 | `phase2.senderStats` |

JSON 파일이 없거나 파싱에 실패하면 다음과 같은 메시지를 stderr에 출력하고 stdout parser로 fallback합니다.

```text
[ControlClient] Result JSON not found: ... Falling back to stdout parser.
[ControlServer] Failed to parse result JSON ... Falling back to stdout parser.
```

이 fallback은 이전 버전 `IPEFTC`와 JSON 저장 실패 상황을 지원하기 위해 유지됩니다.

## 결과 파일

각 `IPEFTC` 프로세스는 `--result-dir`에 다음 파일을 저장합니다.

```text
result-<runId>-CLIENT.json
result-<runId>-SERVER.json
latest-CLIENT.json
latest-SERVER.json
```

`TestRunner`는 정확한 run 결과를 위해 `latest-*`가 아니라 `result-<runId>-<ROLE>.json`을 사용합니다.

멀티 포트 또는 반복 실행에서는 role별 JSON 파일이 run/port마다 생성됩니다. 예를 들어 2개 포트를 1회 실행하면 client/server 합산 4개의 `result-tr-run...json` 파일이 생성됩니다.

## Control Protocol

모든 control message는 JSON 문자열이며, 전송 형식은 다음과 같습니다.

1. 메시지 길이: 4 bytes, network byte order
2. 메시지 본문: JSON string

최대 메시지 크기는 64KB입니다.

### CONFIG_REQUEST

client가 server에 테스트 설정을 전송합니다.

```json
{
  "messageType": "CONFIG_REQUEST",
  "testConfig": {
    "mode": "",
    "configPath": "",
    "targetIP": "192.168.1.100",
    "serverBindIP": "0.0.0.0",
    "port": 5201,
    "packetSize": 8192,
    "numPackets": 10000,
    "sendIntervalMs": 0,
    "saveLogs": true,
    "protocol": "TCP",
    "runId": "tr-run1-port5201-0",
    "resultDir": "Results"
  }
}
```

`mode`와 `configPath`는 현재 serializer가 포함하는 legacy 필드이며 일반 실행에서는 빈 문자열일 수 있습니다.

### SERVER_READY

server가 `IPEFTC` server 준비 완료를 알립니다.

```json
{
  "messageType": "SERVER_READY",
  "port": 5201,
  "serverIP": "0.0.0.0"
}
```

### RESULTS_REQUEST

client가 server 결과를 요청하면서 client 결과도 함께 보냅니다.

```json
{
  "messageType": "RESULTS_REQUEST",
  "port": 5201,
  "clientResult": {
    "role": "Client",
    "port": 5201,
    "duration": 0.25,
    "throughput": 260.5,
    "hostTotalBytes": 0,
    "totalBytes": 81920000,
    "totalPackets": 10000,
    "expectedBytes": 81920000,
    "expectedPackets": 10000,
    "sequenceErrors": 0,
    "checksumErrors": 0,
    "contentMismatches": 0,
    "failureReason": "Test passed successfully",
    "success": true
  }
}
```

### RESULTS_RESPONSE

server가 server 결과를 반환합니다.

```json
{
  "messageType": "RESULTS_RESPONSE",
  "serverResult": {
    "role": "Server",
    "port": 5201,
    "duration": 0.25,
    "throughput": 260.5,
    "hostTotalBytes": 0,
    "totalBytes": 81920000,
    "totalPackets": 10000,
    "expectedBytes": 81920000,
    "expectedPackets": 10000,
    "sequenceErrors": 0,
    "checksumErrors": 0,
    "contentMismatches": 0,
    "failureReason": "Test passed successfully",
    "success": true
  }
}
```

### ERROR_MESSAGE

오류 발생 시 사용합니다.

```json
{
  "messageType": "ERROR_MESSAGE",
  "error": "Failed to launch IPEFTC server"
}
```

### HEARTBEAT

연결 유지 확인용 메시지입니다.

```json
{
  "messageType": "HEARTBEAT"
}
```

### SERVER_SHUTDOWN

client가 모든 run 완료 후 server에 종료를 요청합니다.

```json
{
  "messageType": "SERVER_SHUTDOWN"
}
```

## 출력 예시

client summary는 client/server role별 결과를 함께 표시합니다.

```text
--- TEST SUMMARY (Client-side View: Run 1) ---
Role    Port    Duration (s)   Throughput (Mbps) Total Bytes Rx        Total Packets Rx        Status
-----------------------------------------------------------------------------------------------------
Client  5201    0.25           260.50            81920000              10000                   PASS
Server  5201    0.25           260.50            81920000              10000                   PASS
-----------------------------------------------------------------------------------------------------

========================================================
          FINAL GLOBAL SUMMARY (Client-side)
========================================================
```

프로그램 종료 코드는 실패 여부를 반영합니다.

- 모든 client/server result가 PASS이면 `0`
- 하나라도 실패하면 `1`

## MyIperf 단독 실행 vs TestRunner 자동화 실행

| 항목 | MyIperf 단독 실행 | TestRunner 자동화 실행 |
| --- | --- | --- |
| 실행 단위 | 사용자가 server/client 각각 직접 실행 | TestRunner가 원격 `IPEFTC` server와 local client 실행 조율 |
| 제어 채널 | 없음 | TCP control channel |
| 테스트 채널 | `IPEFTC` TCP test port | 동일 |
| 결과 원본 | `TestRunResult` | `IPEFTC`의 `TestRunResult` JSON 우선 |
| 결과 fallback | 없음 | JSON 실패 시 stdout parser fallback |
| 멀티 포트 | 수동 실행 필요 | `--num-ports`로 순차 실행 |
| 반복 실행 | 수동 반복 필요 | `--total-runs`로 반복 |

## 주의사항

- control port와 test port는 달라야 합니다.
- 방화벽에서 control port 기본 `9500`, test port 기본 `5201+`를 허용해야 합니다.
- Windows dynamic port range와 겹치는 높은 test port를 쓰면 임시 포트와 충돌할 수 있습니다. 기본값 `5201` 사용을 권장합니다.
- `--num-packets 0` 무한 모드는 TestRunner 자동 검증에 적합하지 않으며 지원 대상이 아닙니다.
- `--result-dir` 상대 경로는 각 머신의 실행 위치 기준입니다. 분산 환경에서 결과 파일을 한 곳에 모으려면 공유 경로 또는 후처리 수집 절차를 사용합니다.
- `IPEFTC` child process가 비정상 종료되면 잔류 프로세스를 확인합니다.

```powershell
Get-Process IPEFTC,TestRunner -ErrorAction SilentlyContinue
```

## 문제 해결

### control server 연결 실패

확인할 항목:

- `TestRunner --mode server`가 실행 중인지
- `--server` IP와 `--control-port`가 맞는지
- server 방화벽이 control port를 허용하는지

### IPEFTC 실행 파일을 찾지 못함

`--ipeftc-path`를 명시합니다.

```powershell
.\TestRunner.exe --mode server --ipeftc-path D:\path\to\IPEFTC.exe
```

### server start timeout

확인할 항목:

- test port가 이미 사용 중인지
- server bind IP가 해당 머신에 존재하는지
- `IPEFTC.exe --mode server ...`가 단독 실행으로 정상 동작하는지

### JSON 결과 fallback 발생

stderr의 fallback 이유를 확인합니다.

- `Result JSON not found`: `--result-dir` 경로, 권한, 상대 경로 기준 확인
- `runId mismatch`: stale 파일 또는 잘못된 result directory 확인
- parse failure: `IPEFTC` JSON schema와 파일 손상 여부 확인

fallback이 발생해도 stdout parser가 성공하면 summary는 PASS가 될 수 있습니다.

## 검증 명령

빌드:

```powershell
cmake --build .\TestRunner\build --config Release
cmake --build .\TestRunner\build --config Debug
```

도움말:

```powershell
.\TestRunner\build\Release\TestRunner.exe --help
```

로컬 smoke test 흐름:

```powershell
.\TestRunner\build\Release\TestRunner.exe --mode server --control-port 9500 --ipeftc-path .\MyIperf\build\bin\Release\IPEFTC.exe
.\TestRunner\build\Release\TestRunner.exe --mode client --server 127.0.0.1 --control-port 9500 --test-port 5201 --num-packets 100 --result-dir Results
```

확인할 항목:

- client/server summary가 PASS
- `result-tr-run...-CLIENT.json` 생성
- `result-tr-run...-SERVER.json` 생성
- 정상 JSON-first 실행에서는 `Falling back to stdout parser` 메시지 없음

## 개발자 정보

```text
TestRunner/
├── Protocol.h             # control protocol constants and message types
├── Message.h/.cpp         # JSON serialization/deserialization
├── ProcessManager.h/.cpp  # IPEFTC process launch, output capture, result JSON parsing
├── ControlServer.h/.cpp   # TCP control server
├── ControlClient.h/.cpp   # TCP control client
├── IpeftcOutputParser.*   # stdout fallback parser
├── main.cpp               # CLI entry point
└── CMakeLists.txt
```

확장 시 우선 고려할 지점:

- UDP 지원: `TestConfig`, `IPEFTC` protocol, 결과 판정 확장 필요
- 인증: control protocol message에 token 또는 handshake 추가
- 중앙 결과 수집: 분산 `resultDir` 파일을 한 위치로 모으는 별도 collector 필요
- 더 안정적인 API 연동: stdout fallback 의존도를 줄이고 `TestRunResult` schema version을 기준으로 변환

# ResultJsonViewer

`ResultJsonViewer`는 `IPEFTC`가 최종 저장한 `result-<runId>-<ROLE>.json` 파일을 읽어서 사람이 보기 좋은 콘솔 요약으로 출력하는 독립 C++/CMake 도구입니다.

이 도구는 `IPEFTC`를 실행하지 않고, result pipe도 읽지 않습니다. 이미 저장된 최종 JSON 파일을 열어 핵심 스키마를 검증하고, 최종 판정에 맞는 exit code를 반환합니다.

## 용도

- 상위 프로그램이나 작업자가 `IPEFTC` 결과 JSON을 빠르게 확인
- `success`와 `finalState` 기준으로 PASS/FAIL exit code 획득
- phase 1/2 송수신 통계와 mismatch count를 콘솔에서 확인
- `ResultPipeVerifier`가 만든 smoke 결과 JSON을 사람이 읽는 형태로 확인

## 빌드

저장소 루트에서 실행합니다.

```powershell
cmake -S .\ResultJsonViewer -B .\ResultJsonViewer\build
cmake --build .\ResultJsonViewer\build --config Release
```

Linux:

```bash
cmake -S ./ResultJsonViewer -B ./ResultJsonViewer/build -DCMAKE_BUILD_TYPE=Release
cmake --build ./ResultJsonViewer/build
```

`ResultJsonViewer`는 `myiperf_core`에 링크하지 않습니다. JSON 파싱을 위해 `MyIperf/third_party/nlohmann/json.hpp`만 include합니다.

## 실행

특정 result JSON 파일을 직접 읽는 방식이 기본입니다.

```powershell
.\ResultJsonViewer\build\Release\ResultJsonViewer.exe `
    --file .\Results\result-smoke-001-CLIENT.json
```

Linux:

```bash
./ResultJsonViewer/build/ResultJsonViewer \
    --file ./Results/result-smoke-001-CLIENT.json
```

`resultDir`, `runId`, `role`을 지정해서 파일명을 조립할 수도 있습니다.

```powershell
.\ResultJsonViewer\build\Release\ResultJsonViewer.exe `
    --result-dir .\Results `
    --run-id smoke-001 `
    --role client
```

위 명령은 내부적으로 다음 파일을 읽습니다.

```text
Results\result-smoke-001-CLIENT.json
```

`--role`은 `client`, `CLIENT`, `server`, `SERVER`처럼 대소문자를 섞어 입력할 수 있으며, 내부 검증과 출력에서는 `CLIENT` 또는 `SERVER`로 정규화됩니다.

## 옵션

```text
--file <path>        Read a specific result JSON file. Takes precedence.
--result-dir <path>  Directory containing result-<runId>-<ROLE>.json.
--run-id <id>        Run ID used in the result file name.
--role <role>        CLIENT or SERVER. Case-insensitive.
-h, --help           Show help.
```

입력 규칙:

- `--file`이 있으면 항상 `--file`을 우선합니다.
- `--file`이 없으면 `--result-dir`, `--run-id`, `--role`이 모두 필요합니다.
- `role`은 `CLIENT` 또는 `SERVER`만 허용합니다.

## 출력 예시

```text
=== MyIperf Result Summary ===
Status: PASS

runId                 : smoke-001
role                  : CLIENT
schemaVersion         : 1
startedAt             : 2026-05-06T09:00:00.000+09:00
finishedAt            : 2026-05-06T09:00:01.000+09:00
finalState            : FINISHED

Config
targetIP              : 127.0.0.1
port                  : 5201
packetSize            : 1024
numPackets            : 5
intervalMs            : 1
protocol              : TCP

Phase Summary
Phase  Name                  Sender      Receiver    Success  Snd pkt/bytes     Rcv pkt/bytes     Rcv Mbps          Chk/Seq/Content
1      client_to_server      CLIENT      SERVER      PASS     5 / 5120          5 / 5120          6.780             0 / 0 / 0
2      server_to_client      SERVER      CLIENT      PASS     5 / 5120          5 / 5120          7.012             0 / 0 / 0

Phase 1 (client_to_server): PASS
Phase 2 (server_to_client): PASS
```

실패 결과이면 `Status: FAIL`로 출력하고, JSON에 `failureReason`이 있으면 함께 출력합니다. `resultExportWarning`이 있으면 PASS/FAIL과 별개로 표시합니다.

## 검증하는 필드

필수 top-level 필드:

- `runId`
- `role`
- `success`
- `finalState`
- `config`
- `phase1`
- `phase2`

필수 phase 필드:

- `phaseName`
- `senderRole`
- `receiverRole`
- `success`
- `senderStats`
- `receiverStats`

필수 stats 필드:

- `totalPacketsSent`
- `totalPacketsReceived`
- `totalBytesSent`
- `totalBytesReceived`
- `failedChecksumCount`
- `sequenceErrorCount`
- `contentMismatchCount`
- `duration`
- `throughputMbps`

필수 config 필드:

- `targetIP`
- `port`
- `packetSize`
- `numPackets`
- `sendIntervalMs`
- `protocol`

스키마 검증은 핵심 필드가 있는지와 타입이 맞는지를 확인합니다. 추가 필드는 무시합니다.

## Exit Code

| Exit code | 의미 |
| --- | --- |
| `0` | JSON을 정상적으로 읽었고 `success == true`, `finalState == "FINISHED"` |
| `1` | JSON은 정상적으로 읽었지만 최종 판정이 FAIL |
| `2` | 파일 열기 실패, JSON parse 실패, 옵션 오류, 필수 필드 누락 또는 타입 오류 |

상위 프로그램에서는 exit code를 기준으로 자동 판정할 수 있습니다. 콘솔 출력은 작업자 확인용 요약입니다.

## ResultPipeVerifier 결과 확인 예시

`ResultPipeVerifier` smoke test가 만든 결과를 바로 확인할 수 있습니다.

```powershell
.\ResultJsonViewer\build\Release\ResultJsonViewer.exe `
    --file .\ResultPipeVerifier\results\<runId>\result-<runId>-CLIENT.json

.\ResultJsonViewer\build\Release\ResultJsonViewer.exe `
    --file .\ResultPipeVerifier\results\<runId>\result-<runId>-SERVER.json
```

runId 기반 조회:

```powershell
.\ResultJsonViewer\build\Release\ResultJsonViewer.exe `
    --result-dir .\ResultPipeVerifier\results\<runId> `
    --run-id <runId> `
    --role CLIENT
```

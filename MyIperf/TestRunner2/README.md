# TestRunner2 - 분산 IPEFTC 테스트 시스템

TestRunner2는 TestRunner에 TCP 기반 원격 제어 기능을 추가한 분산 테스트 시스템입니다. 서로 다른 머신에서 IPEFTC 서버와 클라이언트를 원격으로 제어하고 동기화하여 네트워크 성능 테스트를 수행할 수 있습니다.

## 프로젝트 개요

### 주요 특징

- **원격 제어**: TCP 소켓을 통한 원격 IPEFTC 프로세스 제어
- **자동 동기화**: 서버 준비 완료 후 클라이언트 자동 시작
- **단일/멀티 포트 지원**: 하나 또는 여러 포트에서 동시 테스트 실행
- **결과 자동 수집**: 서버와 클라이언트 결과를 자동으로 수집하고 검증
- **JSON 프로토콜**: 명확한 메시지 구조로 확장 가능

### 아키텍처

```
┌─────────────────────────────────────────────────────────────┐
│                    제어 계층 (Control Layer)                  │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ControlClient              TCP 9000              ControlServer │
│  (로컬 머신)          <───────────────────>        (원격 머신)  │
│                                                               │
│  1. CONFIG_REQUEST  ─────────────────────>                   │
│                                             [IPEFTC 서버 시작]  │
│                     <───────────────────── 2. SERVER_READY    │
│  [IPEFTC 클라이언트 시작]                                      │
│                                                               │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    테스트 계층 (Test Layer)                   │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  IPEFTC Client            TCP 60000+           IPEFTC Server  │
│  (로컬 머신)          <═══════════════════>      (원격 머신)   │
│                      성능 테스트 수행                          │
│                                                               │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    결과 수집 (Results)                        │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  3. RESULTS_REQUEST ─────────────────────>                   │
│                     <───────────────────── 4. RESULTS_RESPONSE│
│                                                               │
│  [결과 분석 및 출력]                                           │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

## 빌드 방법

### 1. 빌드 디렉토리 생성

```bash
cd TestRunner2
mkdir build
cd build
```

### 2. CMake 설정

```bash
cmake ..
```

### 3. 빌드

**Release 모드 (권장):**
```bash
cmake --build . --config Release
```

**Debug 모드:**
```bash
cmake --build . --config Debug
```

빌드가 완료되면 실행 파일이 다음 경로에 생성됩니다:
```
TestRunner2\build\Release\TestRunner2.exe
```

## 사용 방법

### 기본 사용 패턴

TestRunner2는 단일 실행 파일로 서버/클라이언트 모드를 선택하여 사용합니다.

```bash
TestRunner2.exe --mode <server|client> [옵션]
```

### 서버 모드

원격 머신에서 서버 모드로 실행합니다. 서버는 클라이언트 연결을 대기하며, 요청에 따라 IPEFTC 서버를 실행합니다.

**기본 실행:**
```bash
.\TestRunner2.exe --mode server
```

**포트 지정:**
```bash
.\TestRunner2.exe --mode server --control-port 9000
```

**IPEFTC 경로 지정:**
```bash
.\TestRunner2.exe --mode server --control-port 9000 --ipeftc-path "C:\path\to\IPEFTC.exe"
```

### 클라이언트 모드 (단일 포트)

로컬 머신에서 클라이언트 모드로 실행하여 원격 서버와 테스트를 수행합니다.

**기본 실행:**
```bash
.\TestRunner2.exe --mode client --server 192.168.1.100
```

**상세 설정:**
```bash
.\TestRunner2.exe --mode client ^
    --server 192.168.1.100 ^
    --control-port 9000 ^
    --test-port 60000 ^
    --packet-size 8192 ^
    --num-packets 10000 ^
    --interval-ms 0 ^
    --save-logs true
```

### 클라이언트 모드 (멀티 포트)

여러 포트에서 동시에 테스트를 실행합니다. TestRunner의 멀티 스레드 패턴과 유사합니다.

**5개 포트 동시 테스트:**
```bash
.\TestRunner2.exe --mode client ^
    --server 192.168.1.100 ^
    --num-ports 5 ^
    --test-port 60000 ^
    --packet-size 8192 ^
    --num-packets 10000
```

포트는 `--test-port`부터 순차적으로 할당됩니다 (예: 60000, 60001, 60002, 60003, 60004).

## 명령행 옵션

### 공통 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--mode` | 실행 모드 (server 또는 client) | **필수** |
| `--control-port` | 제어 포트 번호 | 9000 |
| `--ipeftc-path` | IPEFTC.exe 경로 | `..\\build\\Release\\IPEFTC.exe` |

### 서버 전용 옵션

서버는 공통 옵션만 사용합니다.

### 클라이언트 전용 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--server` | 서버 IP 주소 | **필수** |
| `--test-port` | IPEFTC 테스트 포트 | 60000 |
| `--packet-size` | 패킷 크기 (바이트) | 8192 |
| `--num-packets` | 전송할 패킷 개수 | 10000 |
| `--interval-ms` | 패킷 전송 간격 (ms) | 0 (최대 속도) |
| `--save-logs` | 로그 저장 여부 (true/false) | true |
| `--num-ports` | 동시 테스트 포트 수 (멀티 포트 모드) | 1 (단일) |

## 프로토콜 명세

### 메시지 타입

TestRunner2는 JSON 기반 메시지 프로토콜을 사용합니다.

#### 1. CONFIG_REQUEST (Client → Server)

클라이언트가 테스트 설정을 서버에 전송합니다.

```json
{
  "messageType": "CONFIG_REQUEST",
  "testConfig": {
    "port": 60000,
    "packetSize": 8192,
    "numPackets": 10000,
    "sendIntervalMs": 0,
    "protocol": "TCP",
    "saveLogs": true
  }
}
```

#### 2. SERVER_READY (Server → Client)

서버가 IPEFTC 서버 준비 완료를 알립니다.

```json
{
  "messageType": "SERVER_READY",
  "port": 60000,
  "serverIP": "0.0.0.0"
}
```

#### 3. RESULTS_REQUEST (Client → Server)

클라이언트가 서버 테스트 결과를 요청합니다.

```json
{
  "messageType": "RESULTS_REQUEST",
  "port": 60000
}
```

#### 4. RESULTS_RESPONSE (Server → Client)

서버가 테스트 결과를 전송합니다.

```json
{
  "messageType": "RESULTS_RESPONSE",
  "serverResult": {
    "role": "Server",
    "port": 60000,
    "duration": 2.35,
    "throughput": 278.63,
    "totalBytes": 81920000,
    "totalPackets": 10000,
    "expectedBytes": 81920000,
    "expectedPackets": 10000,
    "sequenceErrors": 0,
    "checksumErrors": 0,
    "contentMismatches": 0,
    "failureReason": "",
    "success": true
  }
}
```

#### 5. ERROR_MESSAGE (양방향)

오류 발생 시 전송됩니다.

```json
{
  "messageType": "ERROR_MESSAGE",
  "error": "Failed to launch IPEFTC server"
}
```

#### 6. HEARTBEAT (양방향)

연결 유지 확인용 메시지입니다.

```json
{
  "messageType": "HEARTBEAT"
}
```

### 메시지 전송 형식

모든 메시지는 다음 형식으로 전송됩니다:

1. **메시지 길이** (4바이트, network byte order)
2. **메시지 데이터** (JSON 문자열)

최대 메시지 크기: 64KB (65536 바이트)

## 사용 시나리오

### 시나리오 1: 기본 단일 포트 테스트

**원격 서버 (192.168.1.100):**
```bash
.\TestRunner2.exe --mode server --control-port 9000
```

**로컬 클라이언트:**
```bash
.\TestRunner2.exe --mode client --server 192.168.1.100 --test-port 60000 --num-packets 10000
```

### 시나리오 2: 멀티 포트 성능 테스트

**원격 서버 (192.168.1.100):**
```bash
.\TestRunner2.exe --mode server --control-port 9000
```

**로컬 클라이언트 (5개 포트 동시 테스트):**
```bash
.\TestRunner2.exe --mode client ^
    --server 192.168.1.100 ^
    --num-ports 5 ^
    --test-port 60000 ^
    --packet-size 1500 ^
    --num-packets 100000 ^
    --interval-ms 0
```

### 시나리오 3: 저속 전송 테스트

**원격 서버:**
```bash
.\TestRunner2.exe --mode server
```

**로컬 클라이언트 (10ms 간격):**
```bash
.\TestRunner2.exe --mode client ^
    --server 192.168.1.100 ^
    --test-port 60000 ^
    --num-packets 1000 ^
    --interval-ms 10
```

## 출력 형식

### 서버 출력

```
==================================================
Starting TestRunner2 Server
Control Port: 9000
IPEFTC Path: ..\build\Release\IPEFTC.exe
==================================================
[ControlServer] Server listening on port 9000
[ControlServer] Waiting for client connections...
[ControlServer] Client connected from 192.168.1.50:52341
[ControlServer] Processing CONFIG_REQUEST
[ControlServer] Test config received - Port: 60000, Packets: 10000, Size: 8192
[ProcessManager] Launching IPEFTC server: ...
[ProcessManager] Server is ready!
[ControlServer] SERVER_READY sent
[ControlServer] Processing RESULTS_REQUEST
[ControlServer] Results sent - Success: true, Throughput: 278.63 Mbps
```

### 클라이언트 출력

```
==================================================
Starting Single Port Test
Port: 60000
Packet Size: 8192 bytes
Num Packets: 10000
Interval: 0 ms
==================================================
[ControlClient] Connecting to 192.168.1.100:9000...
[ControlClient] Connected to server
[ControlClient] Sending CONFIG_REQUEST for port 60000
[ControlClient] Server ready on port 60000
[ProcessManager] Launching IPEFTC client: ...
[ControlClient] Waiting for IPEFTC client to complete...
[ControlClient] Client test completed
[ControlClient] Requesting results from server...
[ControlClient] Received server results

--- FINAL TEST SUMMARY ---
Role    Port    Duration (s)   Throughput (Mbps)   Total Bytes Rx        Total Packets Rx      Status
--------------------------------------------------------------------------------------------------------
Server  60000   2.35           278.63              81920000              10000                 PASS
Client  60000   2.36           277.42              81920000              10000                 PASS

--- Summary ---
Total Tests: 2
Passed: 2
Failed: 0

SUCCESS: All tests passed!
```

## 주의사항 및 제한사항

### 네트워크 설정

1. **방화벽**: 양쪽 머신 모두 필요한 포트를 오픈해야 합니다
   - 제어 포트: 기본 9000 (설정 가능)
   - 테스트 포트: 기본 60000+ (설정 가능)

2. **포트 충돌**: 제어 포트와 테스트 포트는 반드시 달라야 합니다

3. **네트워크 레이턴시**: 높은 레이턴시 환경에서는 타임아웃 값 조정이 필요할 수 있습니다

### 리소스 관리

1. **포트 정리**: 멀티 포트 테스트 후 포트가 완전히 해제될 때까지 약간의 시간이 필요합니다

2. **프로세스 정리**: 비정상 종료 시 IPEFTC 프로세스가 남아있을 수 있으므로 확인 필요

3. **로그 파일**: `--save-logs true` 사용 시 Log 디렉토리에 로그가 누적됩니다

### 플랫폼

- **Windows**: 완전 지원 (Windows Sockets 사용)
- **Linux**: 추후 지원 예정 (POSIX 소켓으로 확장 가능)

### 알려진 제한사항

1. 현재 버전은 TCP만 지원합니다 (UDP는 IPEFTC에서 지원 시 추가 가능)
2. 서버는 다중 클라이언트를 지원하지만, 동일 포트에 대한 동시 요청은 처리되지 않습니다
3. 무한 모드 (`numPackets = 0`)는 지원되지 않습니다

## 문제 해결

### 연결 실패

**증상**: "Connection failed" 또는 "Failed to connect to control server"

**해결 방법**:
1. 서버가 실행 중인지 확인
2. 서버 IP 주소가 올바른지 확인
3. 방화벽 설정 확인
4. 포트 번호가 일치하는지 확인

### 서버 시작 실패

**증상**: "IPEFTC server failed to start"

**해결 방법**:
1. IPEFTC.exe 경로가 올바른지 확인 (`--ipeftc-path` 옵션)
2. 테스트 포트가 이미 사용 중인지 확인
3. IPEFTC.exe가 정상 동작하는지 단독 실행으로 테스트

### 타임아웃

**증상**: "Timeout waiting for..."

**해결 방법**:
1. 네트워크 연결 상태 확인
2. 서버/클라이언트가 정상 동작 중인지 확인
3. 패킷 수가 많고 간격이 클 경우 타임아웃이 발생할 수 있음 (정상)

## TestRunner vs TestRunner2

| 기능 | TestRunner | TestRunner2 |
|------|-----------|-------------|
| 실행 환경 | 단일 머신 | 분산 (여러 머신) |
| 제어 방식 | 로컬 프로세스 생성 | TCP 원격 제어 |
| 동기화 | 로컬 파이프 | TCP 메시지 |
| 멀티 포트 | 지원 | 지원 |
| 결과 수집 | stdout 파싱 | JSON 메시지 |
| 사용 사례 | 로컬 성능 테스트 | 실제 네트워크 환경 테스트 |

## 개발자 정보

### 소스 파일 구조

```
TestRunner2/
├── Protocol.h          # 프로토콜 상수 및 메시지 타입
├── Message.h/cpp       # JSON 직렬화/역직렬화
├── ProcessManager.h/cpp # IPEFTC 프로세스 관리
├── ControlServer.h/cpp  # TCP 서버 구현
├── ControlClient.h/cpp  # TCP 클라이언트 구현
├── main.cpp             # 진입점
├── CMakeLists.txt       # 빌드 설정
└── README.md            # 이 문서
```

### 확장 가능성

1. **UDP 지원**: Protocol.h와 TestConfig에 UDP 옵션 추가
2. **인증**: 메시지에 인증 토큰 필드 추가
3. **통계**: 더 상세한 성능 메트릭 수집
4. **웹 인터페이스**: REST API 또는 WebSocket 추가

## 라이선스

이 프로젝트는 IPEFTC 프로젝트의 일부입니다.


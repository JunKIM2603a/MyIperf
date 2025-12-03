# TestRunner 빌드 및 실행 방법 (한국어)

아래는 `TestRunner.cpp` 와 해당 `CMakeLists.txt` 를 기반으로 한 **테스트 오케스트레이터(TestRunner)** 프로그램을 빌드하고 실행하는 방법을 한글로 정리한 문서입니다.

---

## 📁 1. TestRunner 빌드 디렉토리 생성

먼저 TestRunner 전용 빌드 폴더를 생성하고 그 폴더로 이동합니다.

```bash
mkdir TestRunner\build
cd TestRunner\build
```

---

## ⚙️ 2. CMake 로 빌드 파일 생성

`CMakeLists.txt` 가 있는 TestRunner 디렉토리를 기준으로 CMake 설정을 생성합니다.

```bash
cmake ..
```

---

## 🔨 3. 프로그램 컴파일

Release 모드로 빌드합니다.

```bash
cmake --build . --config Release
```

빌드가 완료되면 실행 파일이 아래 경로에 생성됩니다:

```
TestRunner\build\Release\TestRunner.exe
```

---

## ▶️ 4. TestRunner 실행 방법

프로젝트 루트 경로에서 실행합니다:

```
c:\Users\junkim\MyProjects\MyIperf\MyIperf
```

아래 형식으로 실행할 수 있습니다:

```
.\TestRunner\build\Release\TestRunner.exe <반복횟수> <패킷_사이즈> <패킷_개수> <전송간격_ms>
```

### 실행 예시

```
.\TestRunner\build\Release\TestRunner.exe 5 1500 10000 10
```

* **반복횟수:** 5회 반복 실행
* **패킷 사이즈:** 1500 bytes
* **패킷 개수:** 10000개
* **전송 간격:** 10ms

---

## 📊 5. 기능 설명

TestRunner는 IPEFTC 프로그램을 반복적으로 실행하고 결과를 자동 수집하는 테스트 오케스트레이터입니다:

### 핵심 기능
* **반복 테스트 실행**: 지정된 횟수만큼 자동으로 서버/클라이언트 프로세스 실행
* **결과 자동 수집**: 각 테스트의 stdout을 캡처하여 통계 추출
* **상세한 파싱**: "FINAL TEST SUMMARY" 섹션을 정밀하게 파싱하여 Phase별 통계 검증
* **엄격한 검증**: 1바이트, 1패킷 차이도 FAIL로 판정하는 엄격한 기준
* **종합 요약**: 전체 반복에 대한 PASS/FAIL 통계 및 총계 제공

### 최신 개선사항
* **동적 타임아웃**: `numPackets`와 `intervalMs` 기반으로 서버 대기 시간 자동 계산
* **무제한 모드 거부**: `numPackets == 0` (무한 전송)은 TestRunner에서 명시적으로 비지원
* **프로세스 조기 종료 감지**: 서버가 준비 메시지 출력 전에 종료되는 경우 즉시 감지
* **리소스 정리 대기**: 
  - 각 서버 프로세스 종료 후 200ms 대기
  - Iteration 간 3초 대기로 포트 및 리소스 완전 해제 보장
* **상세한 실패 사유**: 파싱 실패 시 정확한 위치와 원인 표시

### 출력 형식
```
--- FINAL TEST SUMMARY ---
Role   Port   Duration (s)   Throughput (Mbps)   Total Bytes Rx   Total Packets Rx   Status
--------------------------------------------------------------------------------------------
Server 60000  2.28           525.16              150000000        10                 PASS
Client 60000  2.29           524.82              150000000        10                 PASS
```

### 참고사항
* TestRunner는 프로젝트 루트에서 실행해야 합니다 (IPEFTC.exe 상대 경로 사용)
* 로그 파일은 `Log/` 디렉터리에 자동 저장됩니다
* 간헐적 실패 발생 시 로그 파일을 확인하여 디버깅하세요

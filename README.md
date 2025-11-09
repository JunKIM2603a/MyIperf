# MyIperf 프로젝트

이 저장소에는 `MyIperf`와 `MyIperf_Pipe`라는 두 가지 주요 프로젝트가 포함되어 있습니다.

## MyIperf

`MyIperf`는 `iperf`에서 영감을 받은 C++ 기반의 네트워크 성능 테스트 도구입니다. 클라이언트 또는 서버로 실행되도록 구성하여 네트워크 대역폭과 품질을 테스트할 수 있습니다.

### 주요 기능
- 클라이언트 및 서버 모드
- JSON 파일을 통한 구성 (`config_client.json`, `config_server.json`)
- 크로스플랫폼 네트워크 코드 (Windows IOCP 및 Linux 지원)
- 로깅 기능

## MyIperf_Pipe

`MyIperf_Pipe`는 `MyIperf` 도구와 함께 작동하도록 설계된 유틸리티입니다. 프로젝트 구조를 기반으로 볼 때, 명명된 파이프를 통해 `MyIperf`의 데이터나 로그를 읽어 처리하거나 표시하는 소비자 애플리케이션으로 보입니다.


## git Tag
Git 태그는 특정 릴리스 지점을 표시하는 데 사용됩니다. 다음은 Git 태그를 사용하는 방법입니다.

### 태그 생성

새로운 버전 릴리스를 위해 태그를 생성합니다. 일반적으로 `vX.Y.Z` 형식으로 태그 이름을 지정합니다.

```bash
git tag -a v1.0.0 -m "Release version 1.0.0"
```

- `-a`: Annotated 태그를 생성합니다. Annotated 태그는 태그 생성자, 이메일, 날짜, 태그 메시지 등을 포함하며, 릴리스에 대한 메타데이터를 저장하는 데 유용합니다.
- `-m`: 태그 메시지를 지정합니다.

### 태그 확인

생성된 태그 목록을 확인합니다.

```bash
git tag
```

특정 태그의 상세 정보를 확인합니다.

```bash
git show v1.0.0
```

### 태그 푸시

로컬에 생성된 태그는 자동으로 원격 저장소로 푸시되지 않습니다. 태그를 원격 저장소에 공유하려면 명시적으로 푸시해야 합니다.

```bash
git push origin v1.0.0
```

모든 태그를 한 번에 푸시하려면 다음 명령어를 사용합니다.

```bash
git push origin --tags
```

### 태그 삭제

로컬 태그를 삭제합니다.

```bash
git tag -d v1.0.0
```

원격 태그를 삭제합니다.

```bash
git push origin :v1.0.0
```

# MyIperf UML 및 설계 문서 안내

이 디렉토리는 `MyIperf` 프로젝트의 아키텍처와 핵심 로직을 이해하기 위한 다양한 UML 다이어그램과 상세 가이드를 포함하고 있습니다.

## 📌 주요 문서 바로가기

| 구분 | 문서명 | 설명 |
| :--- | :--- | :--- |
| **핵심 가이드** | [**CodeWalkthrough_StateMachine.md**](./CodeWalkthrough_StateMachine.md) | `TestController`의 상태 머신 로직 상세 분석 |
| | [**Troubleshooting_Guide.md**](./Troubleshooting_Guide.md) | 일반적인 문제 해결 및 설계 패턴 설명 |
| **다이어그램** | [ClassDiagram.md](./ClassDiagram.md) | 전체 클래스 구조 및 관계 |
| | [SequenceDiagram.md](./SequenceDiagram.md) | 클라이언트-서버 간 전체 상호작용 순서 |
| | [StateMachineDiagram.md](./StateMachineDiagram.md) | `TestController`의 상태 전이도 |
| | [ActivityDiagram.md](./ActivityDiagram.md) | 테스트 실행의 흐름도 |
| | [UseCaseDiagram.md](./UseCaseDiagram.md) | 시스템 사용 사례 |

## 📂 기타 다이어그램 목록

- [ComponentDiagram.md](./ComponentDiagram.md): 시스템 컴포넌트 구조
- [DeploymentDiagram.md](./DeploymentDiagram.md): 배포 아키텍처
- [ObjectDiagram.md](./ObjectDiagram.md): 런타임 객체 인스턴스 관계
- [OperationType_MessageType_State.md](./OperationType_MessageType_State.md): 핵심 원자적 개념 간 관계
- [ClientSequenceDiagram.md](./ClientSequenceDiagram.md): 클라이언트 내부 시퀀스
- [ServerSequenceDiagram.md](./ServerSequenceDiagram.md): 서버 내부 시퀀스

## 💡 개발자 참고사항

- 모든 다이어그램은 [Mermaid](https://mermaid-js.github.io/)를 사용하여 작성되었습니다.
- 코드를 수정할 경우 관련된 시퀀스 다이어그램과 상태 머신 문서도 함께 업데이트해 주세요.

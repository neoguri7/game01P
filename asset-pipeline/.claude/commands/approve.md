---
description: (사람 전용) QA 와 리뷰를 통과한 후보를 승인하고 잠근다
argument-hint: <id> <후보 파일 경로>
---

사람이 승인을 지시했다. 다음을 순서대로 실행해라.

대상: $ARGUMENTS

1. 후보 파일 옆에 `.qa.json` 이 있고 `passed: true` 인지 확인한다. 아니면 중단하고 이유를 알린다.
2. `python scripts/approve.py approve <id> <파일>` 을 실행한다.
3. 이어서 `python scripts/approve.py verify` 를 실행해 전체 무결성을 확인한다.
4. 마스터를 승인한 경우, manifest 의 `refs.character_id` 가 기록되어 있는지 확인하고 없으면 사람에게 묻는다.
5. 결과를 두세 줄로 보고한다. 다음 진행 가능한 파생 단계가 있으면 한 줄로 제안만 한다.

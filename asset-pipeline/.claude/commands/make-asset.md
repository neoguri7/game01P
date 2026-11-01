---
description: 에셋 1개를 스펙 작성부터 QA, 독립 리뷰, 사람 보고까지 정해진 절차로 만든다
argument-hint: <id> <kind> <설명>
---

다음 요청을 `docs/PROCESS.md` 의 절차대로 처리해라.

요청: $ARGUMENTS

순서:

1. `CLAUDE.md` 의 절대 규칙과 `docs/PROCESS.md` 를 읽는다. 지문(`style/fingerprints/<kind>.json`)과 앵커가 없으면 여기서 멈추고 Phase 0 이 필요하다고 보고한다.
2. `assets/manifest.json` 에서 같은 id 와 부모를 확인한다. 부모가 필요한 파생인데 `approved` 가 아니면 멈추고 보고한다.
3. `specs/<id>.json` 을 `specs/_template.json` 형식으로 작성한다. `acceptance` 는 관찰 가능한 문장으로 쓴다. 작성한 스펙을 사람에게 한 번 보여주고, **모호한 부분이 있으면 질문**한 뒤 진행한다.
4. 도구 문서를 확인하고 (CLAUDE.md 규칙 7), 스펙의 후보 수만큼 생성한다. 최종 프롬프트 = 스타일 바이블 고정 문구 + `prompt_asset`. 시도마다 manifest `attempts[]` 에 기록한다.
5. 후보마다 `python scripts/qa_gate.py <파일> --spec specs/<id>.json` 을 실행한다. 실패하면 PROCESS.md 11장 조치표 순서대로 처리하고 QA 를 처음부터 다시 돌린다.
6. 통과한 후보를 `contact_sheet.py` 로 앵커와 나란히 놓은 `compare.png` 로 만들고, `style-reviewer` 서브에이전트를 호출한다 (파일 경로만 전달). REVISE 면 FIXES 대로 국소 수정 후 4~5단계를 다시, FAIL 이면 재생성한다.
7. 리뷰어 PASS 인 후보를 `assets/candidates/<id>/` 로 옮기고 `REVIEW.md` 를 PROCESS.md 14장 형식으로 쓴다. status 를 `needs_review` 로 바꾼다.
8. 사람에게 한 번에 보고하고 **멈춘다.** 승인은 하지 않는다. 사람이 `/approve` 를 실행한다.

단계당 최대 시도 수를 넘으면 중단하고 시도 기록, 반복된 실패, 가설, 필요한 결정을 보고한다.

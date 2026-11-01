# Layer + PixelLab 에셋 파이프라인

AI 에이전트(Claude Code)가 PixelLab MCP 와 Layer MCP 를 함께 써서 스타일이 고정된 2D 에셋을 만들고,
**코드 검수 → 독립 리뷰어 → 사람 승인**의 3중 검증을 거치게 하는 프로젝트 템플릿이다.

핵심은 일관성과 정확성을 "부탁"이 아니라 **잠금과 게이트**로 강제하는 것이다. 자세한 절차는 `docs/PROCESS.md`.

## 구조

```
CLAUDE.md                    에이전트 절대 규칙 + 역할 분담 (docs/PROCESS.md 를 불러온다)
docs/PROCESS.md              Phase별 절차, 스펙 형식, 실패 조치표, manifest 스키마, 완료 정의
.mcp.json                    PixelLab / Layer MCP 연결 설정
.claude/
  settings.json              권한 (style/, approved/ 쓰기 금지, 검수 스크립트 자동 허용)
  agents/style-reviewer.md   독립 리뷰어 서브에이전트 (읽기 전용, 루브릭 채점)
  commands/make-asset.md     /make-asset  에셋 1개를 절차대로 진행
  commands/approve.md        /approve     사람 승인 (잠금 + 해시 기록)
style/
  STYLE_BIBLE.md  palette.json  rules.json     사람이 채우는 스타일 기준
  anchors/<kind>/            종류별 앵커 이미지 (character, tile, item, ui)
  fingerprints/<kind>.json   앵커에서 측정한 스타일 수치 (스크립트가 생성)
specs/_template.json         에셋 스펙 템플릿 (생성 전에 반드시 작성)
assets/
  raw/  candidates/  approved/   시도 원본 → 검수 통과 → 승인본(읽기 전용)
  manifest.json              모든 시도와 승인 이력
scripts/
  qa_gate.py                 한 번에: 부모 승인 확인 + 규칙 검사 + 스타일 수치 검사
  validate_asset.py          규칙 검사 (팔레트, 알파, 격자, 프레임 수/편차)
  style_fingerprint.py       앵커에서 스타일 지문 생성 / 후보와 비교
  snap_palette.py            미세한 색 오차 결정론적 보정 (팔레트가 다르면 거부)
  contact_sheet.py           후보와 앵커를 한 장에 나란히 (눈으로 비교)
  approve.py                 승인(읽기 전용 + sha256 기록) / 승인본 무결성 점검
```

## 네 가지 잠금

| 잠금 | 막는 것 | 방법 |
|---|---|---|
| 스타일 | 에셋마다 스타일이 조금씩 달라짐 | 앵커 + 바이블 + 팔레트 + 스타일 지문(수치) |
| 정체성 | 같은 캐릭터가 방향/프레임마다 달라짐 | 마스터 먼저 승인, 파생은 마스터의 `character_id` 에서만 |
| 파생 | 미승인 에셋 위에 다음 작업을 쌓음 | `qa_gate` 가 부모의 승인 여부 확인 |
| 무결성 | 승인 후 파일이 몰래 바뀜 | 읽기 전용 + sha256, QA 이후 변경도 승인 시 차단 |

## 시작하기

1. 의존성: `pip install pillow numpy`
2. PixelLab 토큰: `export PIXELLAB_API_TOKEN=...`
   (이미 PixelLab MCP 를 따로 등록했다면 `.mcp.json` 의 `pixellab` 항목은 지워도 된다.)
3. 프로젝트 폴더에서 Claude Code 실행 → `/mcp` 에서 Layer 인증 (OAuth).
4. **Phase 0 (스타일 잠금)** — 에이전트가 아니라 사람이 먼저 한다.
   - `style/anchors/<kind>/` 에 종류별 앵커 3장 이상 (권장 5~10장)
   - `STYLE_BIBLE.md`, `palette.json` 작성, `rules.json` 값 조정
   - 지문 생성: `python scripts/style_fingerprint.py build style/anchors/character/*.png --out style/fingerprints/character.json`
5. 에셋 만들기: `/make-asset hero_knight_master character_sprite 갑옷 입은 기사, 정면`
   마스터 승인 후 파생: `/make-asset hero_knight_walk character_sheet 걷기 8프레임 4방향`
6. 에이전트가 `assets/candidates/<id>/REVIEW.md` 로 보고하면 확인하고 `/approve <id> <파일>`.

## 알아둘 점

- **이 템플릿의 스크립트는 합성 이미지로 테스트했다.** 실제 PixelLab/Layer 출력으로는 아직 돌려보지 않았으므로,
  `rules.json` 의 수치와 스타일 지문의 허용폭(`style_fingerprint.py` 의 `FLOORS`)은 **시작값**이다.
  처음 몇 개 에셋에서 오탐(멀쩡한데 실패)이나 미탐(이상한데 통과)이 보이면 조정한다.
- 스타일 지문은 앵커가 적을수록 범위가 좁거나 부정확하다. 앵커가 서로 일관되지 않으면 지문도 느슨해진다.
- 코드 검수는 "규칙 위반"과 "수치상 스타일 이탈"을 잡는다. "같은 스타일로 보이는가"의 최종 판단은 독립 리뷰어와 사람의 몫이다.
- PixelLab/Layer 도구의 세부 파라미터는 서비스 쪽에서 바뀔 수 있다. 에이전트가 사용 전에 공식 문서/지침 도구로 확인하도록 되어 있다.
- `.claude/` 의 서브에이전트, 슬래시 명령, 권한 규칙 형식은 Claude Code 버전에 따라 다를 수 있다.
  처음 실행할 때 `/agents`, `/permissions` 로 인식되었는지 확인한다.
- `approve.py` 는 일부러 자동 허용 목록에 넣지 않았다. 실행할 때마다 확인 창이 떠서 사람이 한 번 더 확인하게 된다.
- `style/` 은 에이전트의 쓰기가 막혀 있다. 에이전트에게 바이블 작성을 도움받고 싶다면 `.claude/settings.json` 의 deny 규칙을 잠시 풀거나, 초안을 받아서 직접 붙여 넣는다.

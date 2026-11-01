# 상세 프로세스

일관성과 정확성은 "잘 만들어 달라"는 부탁이 아니라 **잠금(lock)과 게이트(gate)** 로 만든다.
이 문서의 모든 단계에는 통과 조건이 있고, 조건을 통과하지 못하면 다음 단계로 가지 않는다.

## 0. 네 가지 잠금

| 잠금 | 무엇을 막는가 | 어떻게 강제하는가 |
|---|---|---|
| 스타일 잠금 | 에셋마다 스타일이 조금씩 달라지는 것 | 앵커 + 스타일 바이블 + 팔레트 + **스타일 지문**(수치) |
| 정체성 잠금 | 같은 캐릭터가 방향/프레임마다 달라지는 것 | **마스터** 1개를 먼저 승인하고, 모든 파생은 마스터의 `character_id` 에서만 만든다 |
| 파생 잠금 | 승인 안 된 에셋 위에 다음 작업을 쌓는 것 | `qa_gate.py` 가 스펙의 `parent` 가 `approved` 인지 확인한다 |
| 무결성 잠금 | 승인된 에셋이 나중에 몰래 바뀌는 것 | `approve.py` 가 읽기 전용 + sha256 기록, `verify` 로 점검 |

## 1. 검증은 3중이고 서로 독립이다

1. **코드 검수** (`qa_gate.py`): 규칙(팔레트, 알파, 격자, 프레임 수/편차)과 스타일 수치를 측정한다. 사람이나 AI 의 판단이 들어가지 않는다.
2. **독립 리뷰어** (`style-reviewer` 서브에이전트): 생성 과정을 모르는 새 컨텍스트가 앵커와 나란히 놓고 루브릭으로 채점한다. 만든 쪽이 자기 결과를 채점하지 않는다.
3. **사람 승인**: 최종 판단. `/approve` 명령으로만 `approved/` 에 들어간다.

세 단계를 모두 통과해야 승인 후보가 된다. 앞 단계 실패를 뒤 단계가 "봐주지" 않는다.

## 2. 디렉터리와 약속

```
style/anchors/<kind>/        종류별 앵커 (character, tile, item, ui)
style/fingerprints/<kind>.json   앵커에서 측정한 스타일 수치 (스크립트가 생성)
specs/<id>.json              에셋 1개의 요구사항 (생성 전에 반드시 작성)
assets/raw/<id>/             시도별 원본: a1.png, a2.png ... (+ .qa.json, compare.png)
assets/candidates/<id>/      QA + 리뷰어 통과본과 REVIEW.md (사람 승인 대기)
assets/approved/<id>.png     승인본 (읽기 전용)
assets/manifest.json         모든 시도와 승인 이력
```

**스프라이트 시트 규약** (모든 시트에 동일 적용):
- 프레임 크기 = 스펙의 `canvas`, 프레임은 행 우선(row-major)으로 배치한다.
- 행 = 방향, 열 = 프레임. 4방향 행 순서: S, W, E, N. 8방향: S, SW, W, NW, N, NE, E, SE.
- 스펙의 `frames` = 방향 수 × 방향당 프레임 수 (시트 전체 프레임 수).
- 발 기준점(pivot)은 프레임 하단 중앙으로 통일한다.

## 3. Phase 0 — 스타일 잠금 (사람 주도, 에이전트 보조)

시작 조건: 앵커 후보 이미지가 있다.

1. 사람이 `style/anchors/<kind>/` 에 **종류별 3장 이상** (권장 5~10장) 넣는다. 서로 다른 에셋이되 같은 스타일이어야 한다.
2. 사람이 `STYLE_BIBLE.md`, `palette.json`, `rules.json` 을 채운다.
3. 에이전트가 지문을 만든다 (종류별):
   ```
   python scripts/style_fingerprint.py build style/anchors/character/*.png --out style/fingerprints/character.json
   ```
   시트 앵커는 `--frame-size W H` 를 준다. 앵커가 3개(프레임) 미만이면 스크립트가 거부한다.
   만든 지문은 `style/rules.json` 의 각 프로파일 `fingerprint` 필드로 연결된다 (스프라이트와 시트는 같은 캐릭터 지문을 공유한다).
   결과의 지표 범위를 사람에게 요약해서 보여준다.
4. 지표 범위가 비정상적으로 넓으면(예: `n_colors` 편차가 큼) 앵커가 일관되지 않다는 뜻이다. **앵커를 정리한 뒤 다시 만든다.** 지문은 앵커보다 일관될 수 없다.
5. (선택) Layer 스타일 학습. `estimate_training_price` 결과를 보고하고 승인을 받은 뒤 진행한다. 학습 결과 id 는 `style/style.lock.json` 에 사람이 기록한다.

완료 조건: 바이블, 팔레트, 지문이 모두 있고 사람이 "스타일 잠금 완료"라고 확인했다. 이 전에는 에셋을 만들지 않는다.

## 4. Phase 1 — 마스터(정체성) 확정

캐릭터, 몬스터, 주요 오브젝트마다 **마스터 1장**을 먼저 확정한다. 파생은 전부 마스터에서 나온다.

1. 스펙 작성: `specs/<id>_master.json` (`parent` 없음, `layout: single`).
2. 후보 **4~8개** 생성. 같은 프롬프트에서 seed 만 다르게 하지 말고, 설명의 변주도 섞는다.
3. 후보 전부 `qa_gate.py` → 통과한 것만 남긴다.
4. 남은 후보를 컨택트 시트로 앵커와 함께 놓고 에이전트가 상위 1~3개를 추린다.
5. `style-reviewer` 에게 추린 후보를 각각 검수시킨다.
6. 사람에게 REVIEW.md 로 보고 → 사람이 1개를 고르고 `/approve`.
7. 승인 후 manifest 의 `refs.character_id` (PixelLab) 를 기록한다. 이 값이 이후 모든 파생의 기준이다.

## 5. Phase 2 — 파생 (방향, 애니메이션)

순서를 지킨다. **앞 단계가 승인되기 전에 다음 단계를 시작하지 않는다.**

1. 방향 세트 (4 또는 8방향) → 승인
2. idle → 승인
3. walk (그다음 run, attack, hurt, death 등 하나씩) → 승인

규칙:
- 파생 스펙에는 `parent: <마스터 id>` 를 넣는다. 마스터가 `approved` 가 아니면 `qa_gate` 가 실패한다.
- 항상 마스터의 `character_id` 로 `animate_character` 를 호출한다. 새로 그려서 맞추려 하지 않는다.
- 후보는 2~3개면 충분하다. 정체성이 이미 잠겨 있기 때문이다.
- 프레임 정렬 편차, 첫/마지막 프레임 루프는 코드 + 리뷰어가 함께 본다.

## 6. Phase 3 — 환경 (타일, 맵 오브젝트)

- 타일셋은 이전 타일 id 를 이어서 체인한다 (`lower_base_tile_id`, `base_tile_id`). 지형 한 종류가 승인된 뒤 다음 지형을 만든다.
- 맵 오브젝트(나무, 바위 등)는 **승인된 맵 이미지를 배경(스타일 매칭)으로 넘겨서** 만든다.
- 이음새(seam)는 코드로 잡기 어렵다. 리뷰어와 사람이 **타일을 3×3 으로 이어 붙인 이미지**를 보고 확인한다 (컨택트 시트 말고 별도로 이어 붙여서 본다).
- 타일 id 와 구성(lower/upper/transition)을 `refs` 에 기록한다.

## 7. Phase 4 — UI, 아이콘

- 아이콘은 승인된 아이콘 3개 이상을 레퍼런스로 넣는다. 한 번에 세트(예: 포션 6종)를 같은 프롬프트 구조로 만든다.
- UI 요소(버튼, 프레임)는 규격(크기, 9-slice 여백)을 스펙에 적고, 가장 작은 상태(기본)를 먼저 승인한 뒤 hover, pressed 등 파생 상태를 만든다.

## 8. Phase 5 — 엔진 통합 테스트

승인만으로 끝나지 않는다. 실제 게임 조건에서 확인한다.

- 엔진 import 설정: 필터 **Nearest**, 압축/밉맵 끄기.
- 정수 배율로 확대한 화면에서 실제 배경 위에 놓고 스크린샷을 본다. (흐림, 가장자리 할로, 피벗 어긋남)
- 애니메이션은 지정 fps 로 재생해 루프 이음새를 확인한다.
- 문제가 있으면 해당 에셋의 `status` 를 `needs_rework` 로 바꾸고 사유를 manifest 에 기록한다 (승인본을 직접 고치지 않는다).

## 9. 에셋 스펙 (`specs/<id>.json`)

생성 전에 반드시 만든다. 스펙이 없으면 생성하지 않는다. 템플릿은 `specs/_template.json`.

| 필드 | 설명 |
|---|---|
| `id`, `kind` | 에셋 id, `rules.json` 의 프로파일 이름 |
| `layout` | `single` 또는 `sheet` |
| `parent` | 파생이면 마스터 id (아니면 생략) |
| `canvas` | 캔버스(시트면 프레임) 크기 `[w, h]` |
| `frames`, `directions`, `fps` | 시트의 총 프레임 수, 방향 수, 재생 속도 |
| `prompt_asset` | 에셋 고유 설명. 앞에 스타일 바이블의 고정 문구가 붙는다 |
| `references` | 레퍼런스로 넘길 **승인된** 파일 경로들 |
| `candidates`, `max_attempts` | 후보 수, 단계당 최대 시도 |
| `acceptance` | 완료 조건 목록. 리뷰어가 항목마다 근거와 함께 확인한다 |

`acceptance` 는 **측정/관찰 가능한 문장**으로 쓴다.
- 나쁜 예: "자연스러운 걷기"
- 좋은 예: "발이 바닥에 닿는 프레임이 좌우 교대로 나온다", "마지막 프레임에서 첫 프레임으로 팔 위치가 튀지 않는다"

## 10. 생성 규칙

- 최종 프롬프트 = `[STYLE_BIBLE 고정 문구]` + `[spec.prompt_asset]`. 순서와 문구를 임의로 바꾸지 않는다.
- 레퍼런스는 `spec.references` 의 승인 파일만 쓴다. 후보나 raw 를 레퍼런스로 쓰지 않는다.
- 모든 시도를 manifest `attempts[]` 에 기록한다 (아래 스키마). 기록 없는 시도는 없는 것으로 친다.
- 결과는 도구 응답에서 즉시 파일로 저장한다 (`assets/raw/<id>/a<N>.png`).

## 11. 실패 → 조치표

조치는 **위에서 아래로** 시도한다. 아래로 갈수록 정체성/스타일이 바뀔 위험이 크다.

`snap_palette.py` 는 전 픽셀이 조금씩 어긋난 "미세한 오차"만 고친다. 팔레트에서 먼 색이 많으면(기본 10% 초과) 색 체계가 다른 것이라 스냅하면 스타일이 망가지므로 스스로 거부(종료 코드 3)한다. 이때는 재생성으로 넘어간다.

| 실패 항목 | 1순위 (결정론적 보정) | 2순위 (국소 수정) | 3순위 |
|---|---|---|---|
| `alpha_binary` | `snap_palette.py` (알파 이진화 포함) | — | 재생성 |
| `palette`, `max_colors` | `snap_palette.py` | 벗어난 영역만 `inpaint_image` | 재생성 |
| `native_pixel_grid` | 정수배 나눗셈이 되면 최근접 다운스케일 후 재검수 | — | 네이티브 크기로 재생성 |
| `size`, `frame_grid`, `frame_count_exact` | (보정 불가) | — | 크기/프레임 수를 명시해 재생성 |
| `anchor_drift`, `centroid_drift` | (보정 불가) | 어긋난 프레임만 `edit_image` | 재생성 (후보 수 늘림) |
| `no_empty_frames`, `area_ratio` | — | 문제 프레임 재생성 후 시트에 교체 | 전체 재생성 |
| `palette_overlap` | — | — | 마스터 `character_id` 로 재생성 |
| `parent_approved` | 부모 승인부터 진행 | — | — |
| `fingerprint` 경고 | 어느 지표인지 확인 | 해당 요소 `edit_image` | 레퍼런스 추가 후 재생성 |
| `fingerprint` 실패 | — | — | 프롬프트 고정 문구와 레퍼런스 확인 후 재생성 |
| 리뷰어 `REVISE` | — | 지시된 영역만 `inpaint_image` / `edit_image` | — |
| 리뷰어 `FAIL` | — | — | 재생성 |

## 12. 재시도 정책

- 단계당 최대 시도는 `spec.max_attempts` (기본 3).
- **같은 실패 항목이 2번 연속**이면 같은 방식으로 다시 돌리지 않는다. 조치표의 다음 순위로 넘어가거나 레퍼런스/프롬프트를 바꾼다.
- 소진되면 멈추고 사람에게 보고한다: 시도 기록, 반복된 실패 항목, 가설 1~2개, 필요한 결정.
- **국소 수정 우선**: 결함이 일부 영역에 있으면 전체 재생성 대신 `inpaint_image` / `edit_image` 로 그 영역만 고친다. 정체성을 지키는 가장 안전한 방법이다.
- 고친 뒤에는 항상 `qa_gate.py` 를 처음부터 다시 돌린다 (고친 곳이 다른 곳을 깨뜨릴 수 있다).

## 13. manifest 스키마

```json
{
  "id": "hero_knight_walk",
  "kind": "character_sheet",
  "status": "requested | generating | qa_failed | needs_review | approved | rejected | needs_rework",
  "parent": "hero_knight_master",
  "spec": "specs/hero_knight_walk.json",
  "refs": { "character_id": "..." },
  "attempts": [
    {
      "n": 1,
      "tool": "animate_character",
      "prompt": "(실제로 보낸 최종 프롬프트 전문)",
      "params": { },
      "seed": 12345,
      "job_id": "...",
      "output": "assets/raw/hero_knight_walk/a1.png",
      "qa": "pass | fail",
      "failures": ["anchor_drift"],
      "fix_applied": "none | snap_palette | inpaint_image | regenerate",
      "reviewer": "PASS | REVISE | FAIL | not_run",
      "note": ""
    }
  ],
  "file": "assets/approved/hero_knight_walk.png",
  "sha256": "(approve.py 가 기록)",
  "approved_at": "(approve.py 가 기록)"
}
```

`approve.py` 는 `status`, `file`, `sha256`, `approved_at` 만 건드린다. 나머지는 에이전트가 시도마다 갱신한다.

## 14. 사람에게 보고하는 형식 (`assets/candidates/<id>/REVIEW.md`)

```
# <id> 검수 요청
- 종류 / 스펙: ...
- 후보 파일: a2.png (QA 통과, 리뷰어 PASS)
- 비교 이미지: assets/raw/<id>/compare.png (후보 + 앵커 3장)
- QA 요약: 통과 항목 N개 / 경고 K개 (경고 내용 한 줄씩)
- 리뷰어 판정: PASS (핵심 근거 2~3줄)
- 시도 이력: 총 3회 (1회차 palette 실패 → snap 보정, 2회차 anchor_drift → 재생성)
- 사람이 결정할 것: (있으면)
승인 명령: /approve <id> assets/candidates/<id>/a2.png
```

## 15. 완료 정의 (Definition of Done)

에셋 1개는 아래를 **모두** 만족할 때만 `approved` 다.

- [ ] 스펙이 있고 `acceptance` 의 모든 항목에 근거가 있다
- [ ] `qa_gate.py` 통과 (`.qa.json` 이 최종 파일과 짝을 이룬다)
- [ ] `style-reviewer` 판정이 PASS 다
- [ ] 사람이 `/approve` 했고 `approve.py verify` 가 통과한다
- [ ] manifest 에 모든 시도와 최종 선택이 기록되어 있다
- [ ] (파생인 경우) 부모가 `approved` 였다

프로젝트 전체는 Phase 5 의 엔진 통합 테스트까지 끝나야 완료다.

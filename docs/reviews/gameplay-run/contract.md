# Contract — gameplay-run (slice 3: 던전 런 + 몬스터 행동트리)

기획: `docs/design/dungeon-run.md` §1·§2. 상위 기획: `docs/design/game-design.md` §2·§3·§4.
규칙: `docs/guidelines/game-code-changeability.md` (R1~R33).

## In scope (이 슬라이스가 구현하는 것)

1. **런 상태** — `FRunState`(registry.ctx 서비스): 현재 던전, 룸 인덱스, 시드, 클리어한 던전 수, 단계(허브/던전/런 종료).
2. **던전 데이터** — `assets/data/dungeons.json` + `FDungeonContent` + 스키마/참조 검증.
3. **행동트리 데이터** — `assets/data/behaviors.json` + `FBehaviorContent` + units.json의 `behavior` 필드
   (규칙 id 목록) + 스키마/참조 검증.
4. **행동 판단 규칙** — `rules/FBehaviorTree.h`: 조건 평가 → 첫 일치 규칙. 순수 함수.
5. **적 턴 데이터화** — `FEnemyTurnSystem`이 하드코딩 대신 규칙 결과를 실행한다.
6. **런 진행** — `FRunFactory`(룸/파티 스폰, 턴 구조 변경은 기존 팩토리 위임) + `FRunProgressionSystem`
   (룸 승리 → 다음 룸, 던전 클리어 → 허브, 전멸 → 런 종료).
7. **텍스트 표시** — `FBattleTextView`가 런 헤더(던전/룸/시드/결과)를 함께 출력한다.
8. **등록·검증** — `GameplayServices` 등록(R21 단일 지점), `scripts/verify-changeability.sh` 통과,
   `tests/changeability/fixtures/` 갱신.

## Anti-goals (이 슬라이스가 하지 않는 것)

- **파밍 드롭·장비·룬 없음** — slice 4. `loot.json`/`items.json`/`runes.json`을 만들지 않는다.
- **이벤트 룸 없음** — 데이터 스키마의 `kind` 어휘에 자리만 두고, `monster` 외 kind는 로드 시 거부한다.
- **저장/이어하기, 세션 밖 파일 영속 없음** (상위 §2의 정비 지점 저장은 후속 슬라이스).
- **보스 다단계 목표·테마·파벌 없음** (상위 §3·§4 미결).
- **시각 표현 없음** — SDL/이미지/애니메이션 접합 없이 텍스트 오버레이만 (요청: "텍스트로 구현").
- **밸런스 수치 확정 없음** — 룸 수·HP·AP·사거리는 잠정값이며 데이터 파일에서만 조정한다.

## Acceptance criteria

- AC1: 허브 단계에서 `Confirm`을 누르면 첫 던전의 첫 룸 전투가 시작되고, 텍스트에 던전/룸 정보가 보인다.
- AC2: 몬스터의 행동이 `behaviors.json` 순서로 결정된다 — 같은 시드·같은 상태 = 같은 선택(결정론).
- AC3: 룸 승리 → 다음 룸, 마지막 룸 승리 → 던전 클리어(허브 복귀, `clearedDungeons` +1).
- AC4: 파티 전멸 → 런 종료 → 허브 복귀, 클리어한 던전 수는 유지된다.
- AC5: 잘못된 데이터(미지의 룸 id, 없는 행동 id, 잘못된 kind)로는 **부팅이 실패**한다 — 런타임 열화 없음 (R19/R22).
- AC6: `scripts/verify-changeability.sh` → `changeability bundle: OK (repo)`.
- AC7: 빌드 게이트는 **PENDING (owner-run)** — 아래 명령을 소유자가 실행한다.

빌드 게이트 명령 (소유자 실행, 에이전트는 실행 금지 — `DEV_WORKFLOW.md` §"Builds Are Human-Run"):

```sh
pwsh scripts/check-windows.ps1
```

## Implementation choices (설계가 확정하지 않은 슬라이스 국소 결정)

- 입력 매핑: 허브 진입 = `Confirm`, 런 종료 후 복귀 = `Confirm`. (기존 `EInputAction` 재사용, 신규 액션 없음)
- 룸 시작 시 파티 HP/AP 리셋 (기획 §1 근거).
- 시드 = 0 고정, 드롭 롤은 `random(seed, counter)` (slice 4에서 사용).
- 룸 종류는 `monster`만 허용; `event`는 스키마 어휘만.
- 던전 목록 순환: 마지막 던전 다음은 첫 던전 (미결 표기).

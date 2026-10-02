# 1단계(그리드 전장) 구현 PLAN 위임 프롬프트

> 다른 AI(코딩 에이전트)에게 그대로 붙여넣어 **"계획 수립"**을 의뢰하기 위한 자기완결 프롬프트다.
> 이 프롬프트의 산출물은 **코드가 아니라 결정 가능한 구현 계획(decision-ready plan)**이다.
> 아래 `---` 사이 내용 전체를 전달한다.

---

## 1. 역할과 임무

너는 C++23 게임 프로젝트 `game01P`의 시니어 엔진/게임플레이 엔지니어다.
임무: 설계 문서 `docs/design/playable-roadmap.md`의 **1단계 — 그리드 전장**을 구현하기 위한
**의존성 순서를 반영한 구체적 구현 계획**을 작성한다.

**이 턴에서는 코드를 작성/수정하지 않는다.** 파일 편집, 커밋, 빌드 실행 금지.
오직 저장소를 읽고 분석한 뒤 계획을 문서/텍스트로 제출한다.

저장소 루트: `D:/repos/game01P`.

---

## 2. 무시할 것 (로컬에 없는 파일/스크립트)

다음은 문서에서 참조되지만 **저장소에 실제로 없다.** 존재를 가정하거나 의존하지 말 것.
없다고 보고하거나, 필요하면 "현재 없음" 전제로 계획에 반영한다.

- `docs/guidelines/game-code-changeability.md` (R1~R33) — 없음
- `docs/guidelines/agents/*` — 없음
- `scripts/check.sh`, `scripts/remote-windows.sh`, `scripts/` 디렉터리 — 없음
- `DEV_WORKFLOW.md`, `PROJECT_VISION.md` — 없음
- 테스트 프레임워크/ctest 타깃 — 없음 (`tests/`는 사실상 비어 있음, CMake에 `enable_testing()`/ctest 없음)

따라서 검증 절차는 **CMake 프리셋 직접 호출**로 계획한다.
```bat
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
out\build\windows-msvc-debug\Debug\game01P.exe
```
WSL/Linux: `cmake --preset linux-clang-debug && cmake --build --preset linux-clang-debug`.

---

## 3. 먼저 읽을 문서와 코드

문서:
1. `docs/design/playable-roadmap.md` — §2 하드 제약, §3 현재 구현 상태, §5 **1단계**의 작업/DoD/게이트, §6 함정/게이트.
2. `docs/design/game-design.md` — 8x8 그리드, 파티 4명, AP 2종, 턴 규칙. 충돌 시 이 문서 우선.
3. `docs/design/turn-system.md` — 턴 스케줄러(구현 완료). **재작성 금지.**

코드(반드시 확인):
- `src/core/Engine.{h,cpp}` — 특히 `Engine::update`, `applyStateTransition`, `syncStateSystemPolicy`, `render`.
- `src/core/SystemManager.h` — `setEnabled`/`disableAll`/`updateEnabled`/`renderEnabled`/`isEnabled`.
- `src/states/FBaseState.h`, `src/states/GameStateMachine.h`, `src/states/FBattleState.{h,cpp}`.
- `src/gameplay/factory/FScene.h`, `FEntityFactory.{h,cpp}`, `FEntityDefinitions.h`.
- `src/core/JsonLoader.h`, `assets/data/*.json`.
- `src/ecs/components/` 전체, `src/ecs/systems/` 전체.
- `src/core/InputState.h`, `src/core/events/FEventBus.h`, `src/debug/DebugOverlay.cpp`.
- `src/gameplay/turn/TurnController.h`, `src/gameplay/turn/TurnTypes.h`, `src/ecs/systems/TurnSystem.h`.
- `CMakeLists.txt`, `vcpkg.json`, `src/main.cpp`.

---

## 4. 현재 저장소 사실 (0단계가 방금 구현됨 — 미커밋 상태)

계획은 아래 **현존 API 위에** 세워야 한다. 없는 API를 전제로 계획하지 말 것.

빌드/의존성:
- C++23, CMake 3.21+, vcpkg manifest. `CMakeLists.txt`는 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`로
  `src/*.cpp|h|hpp` 자동 수집 → `src/` 아래 파일 추가만으로 빌드 포함.
- `nlohmann-json`이 `vcpkg.json` + `CMakeLists.txt`에 추가되어 있음(`find_package(nlohmann_json CONFIG REQUIRED)`,
  `nlohmann_json::nlohmann_json`). JSON 파서 자체 작성 불필요.
- Windows 프리셋 `windows-msvc-debug`, Linux `linux-clang-debug`.

상태 머신(0단계):
- `FBaseState` 시그니처(확장 완료):
  - `virtual EStateTransition Update(entt::registry&, float) = 0;`
  - `virtual std::string_view GetName() const noexcept = 0;`
  - `virtual void OnEnter(entt::registry&)`, `OnExit(entt::registry&)`.
- `EStateTransition { None, ToTitle, ToHub, ToBattle, ToPrototype, ToGameOver, Quit }`.
- `GameStateMachine::pushState/switchState(unique_ptr<FBaseState>, entt::registry&)`,
  `update(reg, dt)`, `popState(reg)`, `getCurrentStateName()`.
- `FAppFlowRuntimeState { std::string currentState; uint64_t transitionCount; }`가 registry ctx에 있음.
- `Engine`이 `GameStateMachine stateMachine` 멤버를 소유. `Engine::update`가
  `applyStateTransition(stateMachine.update(registry, dt))` → `syncStateSystemPolicy()` →
  `systemMgr.updateEnabled(...)` 순으로 실행.
- `Engine::render`은 `ApplyEngineWorldRenderEffect`(=`systemManager.renderEnabled`) →
  `ApplyEngineOverlayRenderEffect`(=`overlayRenderer` = `RenderDebugOverlay`) 순.
- Enum→상태 매핑: `ToHub`→`FHubState`, `ToBattle`/`ToPrototype`→`FBattleState`.
  시작 상태는 `FTitleState` (`Engine::initialize`에서 `switchState(std::make_unique<FTitleState>(), registry)`).
- 입력 전이: Title에서 `Confirm`(Enter)→ToHub, Hub에서 `Confirm`→ToBattle / `Cancel`(Esc)→ToTitle,
  Battle(dummy)에서 `Cancel`→ToHub.
- **핵심 함정**: `syncStateSystemPolicy()`가 매 프레임 `systemMgr.disableAll()` 후
  `"Battle (dummy)"` 상태에서만 `setEnabled("Stage0BattleSystem", true)` 한다.
  → 현재 Title/Hub/Battle 어디에서도 렌더 시스템이 비활성이라 화면에 아무것도 안 그려진다.
  **1단계는 이 정책을 확장해야 한다.** (상태 이름 문자열 비교에 의존하는 현재 구조도 개선 후보.)

데이터/씬/factory(0단계):
- `FJsonLoader::Load(path, expectedSchemaVersion = 1) -> std::expected<nlohmann::json, FJsonLoadError>`.
  `schema_version` 누락/불일치/파싱 실패 시 `LOG_ERROR` 후 `unexpected`.
- `FScene(entt::registry&, std::string name)`: `Load()`, `Unload()`(자기 소유 엔티티만 destroy),
  `CreateEntity()`, `Emplace<T>(entity, args...)`. 소유 엔티티를 vector로 추적.
- `FEntityFactory`: `CreateUnit(FScene&, const json&) -> expected<entt::entity, FFactoryError>`,
  `CreateSkill(const json&)`, `CreateRoom(const json&)`, `CreateEquipment(const json&)` → 각각 정의 구조체 반환.
- `FUnitDefinition { id, displayName, EUnitFaction faction }`, `FSkillDefinition`, `FRoomDefinition`,
  `FEquipmentDefinition` (`FEntityDefinitions.h`). 유닛 엔티티에는 `FTag{displayName}` + `FUnitData`가 붙는다.
- `assets/data/{unit,skill,room,equipment}.json` 존재. 스키마: `schema_version`, `id`, `display_name`,
  `faction`(unit: "party"/"enemy"), `room_type`(room), `slot`(equipment).
- `main.cpp`가 시작 시 `FScene startupScene(registry, "Stage0Startup")`을 Load하고 위 4종을 생성,
  `run()` 후 Unload. **상태별 씬이 아니라 전역 1회 로드 구조.**

ECS 현황:
- 컴포넌트: `FPosition{float x,y}`, `FVelocity{vx,vy}`, `FSpeed{int32}`, `FTurnParticipant{}`,
  `FSprite`, `FSprFrame`, `FAnimation`, `FCollider`, `FLayer{int depth}`, `FCamera{vec2 position; float zoom}`,
  `FDebugPrimitive{width,height,fill/outline RGBA,filled,drawCollider}`, `FText`, `FTag`.
- `FPosition`은 **float**이고 `namespace` 밖(전역)에 정의됨. 나머지는 `namespace game`.
- 시스템: `MoveSystem`(`pos += vel*dt`), `CollisionSystem`(cute_c2), `AnimationSystem`,
  `DebugPrimitiveRenderSystem`, `SpriteRenderSystem`(헤더 전용), `TurnDemoSystem`, `TurnSystem`,
  `Stage0BattleSystem`(더미, `FStage0BattleRuntime.updateCount`만 증가).
- **`FCamera`는 어떤 렌더 시스템에서도 사용되지 않는다.** 월드→스크린 변환 코드가 현재 없다.
  `DebugPrimitiveRenderSystem`은 `FPosition`을 화면 좌표로 그대로 사용.
- 턴: `TurnController`(속도 기반 라운드, 동률 Fisher–Yates, `FTurnSnapshot`/`FTurnEvent`),
  `TurnSystem`(이벤트 버스 브리지, `FTurnRuntime.requests`의 `variant<FStartTurnSession,FEndTurn,FStopTurnSession>`).
  `FSpeed`+`FTurnParticipant`가 참가 조건.
- 입력: `FInputState` ctx 서비스. 액션 `EInputAction { MoveUp/Down/Left/Right(WASD), Confirm(Enter),
  Cancel(Esc), DebugToggle(grave) }` + 마우스 `mousePos`, `mouseLeftPressed`, `mouseLeftHeld`,
  `uiCapturesMouse`. `isActionPressed`/`isActionHeld`. **마우스 클릭 스크린 좌표만 있고 월드 좌표 변환 없음.**

---

## 5. 1단계 요구사항 (roadmap §5에서 발췌)

목표: **전장이 보이고 유닛이 칸 단위로 놓인다.**

작업:
- 정수 격자 컴포넌트(`FGridPosition` 등), 타일 점유 조회(`FGridOccupant` 또는 그리드 ctx).
- 진영 태그(`FPartyMember` / `FEnemy`).
- 그리드 렌더 시스템(8x8, 칸↔월드 변환). `FCamera` 이동/줌 실사용.
- 마우스/키보드 유닛 선택, 이동 가능 칸 하이라이트.

DoD:
- 8x8 격자가 렌더되고, 유닛이 정수 칸에 배치된다.
- 유닛을 선택해 이동 가능 칸이 표시된다.

의존: 0단계(완료).

**게이트(반드시 계획에서 해결)**: `FPosition`(float) ↔ 그리드(정수) 변환 경계를 정해 **문서화**한다.
전투에서는 cute_c2 물리를 쓰지 않는다.

하드 제약(로드맵 §2): 턴제, 8x8, 파티 4명, AP 2종(이동 1/스킬 2), 속도 기반 이니셔티브,
수치/스킬/룸은 JSON+`schema_version`(하드코딩 금지), 시스템 간 통신은 typed event(문자열 디스패치 금지).

---

## 6. 계획에 반드시 포함할 것

### 6.1 의존성 분석 (핵심 요구)

각 작업을 **의존성 그래프**로 정리하고, 위상 순서(topological order)로 배치한다.
최소한 다음 의존 관계를 명시적으로 다룬다.

- 0단계 산출물 의존: 상태 머신 시그니처, `SystemManager` on/off API, `FScene`, `FEntityFactory`,
  `FJsonLoader`, `assets/data/`.
- **시스템 활성화 정책** 의존: 1단계의 그리드 시스템·렌더 시스템이 `syncStateSystemPolicy`에서
  Battle 상태에 켜지도록 하는 선행 수정. 이것이 없으면 DoD를 눈으로 검증할 수 없다.
  (현재 문자열 이름 비교 구조를 유지할지, 상태→시스템 목록 표로 바꿀지 결정.)
- **좌표계 경계** 의존(게이트): `FPosition` float ↔ `FGridPosition` int. 변환 책임 위치와
  단일 진실 공급원(single source of truth) 결정. 이동 AP/전투가 이 경계에 얹힌다는 점(2단계) 반영.
- **카메라/좌표 변환** 의존: 렌더 시스템들이 현재 `FCamera`를 무시한다. 그리드 렌더와
  마우스 픽킹이 같은 world↔screen 변환을 공유하도록 변환 API를 어디에 둘지 결정.
  기존 `SpriteRenderSystem`/`DebugPrimitiveRenderSystem`을 카메라 경유로 바꿀지,
  별도 그리드 렌더 시스템만 변환을 갖게 할지 트레이드오프 제시.
- **턴 스케줄러** 의존: 1단계에서 턴을 켤지(스케줄러는 있으나 액션/HP는 2단계).
  "선택만 하고 턴은 2단계"로 둘지, 최소 배선만 할지 명시.
- **씬 수명** 의존: 현재 startup `FScene`이 전역 1회 로드. 상태별 씬으로 옮길지,
  Battle 진입 시 로드/이탈 시 Unload할지 결정. `FBattleState::OnEnter/OnExit`가 그 지점.
- **입력** 의존: 마우스 픽킹에 `mouseLeftPressed` + 월드 좌표 변환 필요. UI 캡처(`uiCapturesMouse`) 고려.
- **2단계 선행 금지**: HP/AP/데미지/AI/승패/이동 실행은 범위 밖. 다만 2단계가 얹힐 인터페이스
  (액션 요청 큐, 이동 가능 칸 계산 함수 시그니처 등)는 **설계만** 제시.
- 1단계 내부 컴포넌트 간 의존: `FGridPosition` → 점유 조회 → 이동 가능 칸 계산 → 하이라이트 렌더 →
  선택 입력. 순서와 각 단계가 다음 단계에 제공하는 인터페이스를 명시.

### 6.2 작업 패키지(Work Package)

각 WP마다:
- 목적
- 변경/신규 파일의 정확한 경로(예: `src/ecs/components/FGridPosition.h`)
- 제공 인터페이스(타입/함수 **시그니처 수준**)
- 의존 WP (선행 완료 필요)
- 검증 방법(빌드 + 눈으로/로그로 무엇을 확인)
- 위험/미지수

권장 WP 씨앗(네가 검토 후 조정 가능):
- WP-1 상태별 시스템 정책 확장 + Battle 상태 씬 훅
- WP-2 그리드 좌표계/변환 API + `FGridPosition` + 문서화(게이트 해결)
- WP-3 그리드 점유 조회(ctx 서비스 또는 태그)
- WP-4 진영 태그(`FPartyMember`/`FEnemy`) + 유닛 배치(JSON에서 칸 좌표)
- WP-5 그리드 렌더 시스템 + `FCamera` 이동/줌 실사용
- WP-6 마우스/키보드 선택 + 이동 가능 칸 하이라이트
- WP-7 데이터: `assets/data/`에 배치/그리드 스키마 추가 + factory 확장(`FGridPosition` 포함 생성)

### 6.3 결정 필요 항목(Open Decisions)

각 항목에 **선택지 + 추천 + 근거 + 되돌리기 비용**을 제시한다. 최소:
- 그리드 좌표 진실 공급원: `FGridPosition`만 vs `FPosition`만 vs 둘 다+동기화.
- world↔screen 변환 위치: 렌더 시스템 내부 vs ctx 서비스 vs 카메라 유틸 함수.
- 시스템 활성화 정책: 문자열 이름 매칭 유지 vs 상태→시스템 집합 선언형 테이블.
- 그리드 렌더 방식: `FDebugPrimitive` 재사용 vs 전용 그리드 렌더 시스템.
- 이동 가능 칸 계산: 지금 stub vs 2단계용 실제 순수 함수 미리 분리.
- 상태별 씬 수명: Battle 진입 로드 vs 전역 유지.
- 선택/하이라이트 상태 표현: ECS 태그/컴포넌트 vs `FBattleState` 내부 상태 vs ctx 서비스.

### 6.4 검증 계획

- 빌드 검증 명령(위 §2).
- DoD를 사람이 눈으로 확인하는 시나리오(어떤 키/클릭으로 무엇이 보여야 하는가).
- 실패 시 무엇을 로그로 확인할지.
- 회귀 위험: `TurnDemoSystem`, `TurnSystem`, `syncStateSystemPolicy` 변화가 기존 턴 프로토타입을
  깨지 않는지 확인 방법.

### 6.5 리스크

- 상태 이름 문자열 비교 기반 정책의 취약성.
- `FPosition`이 전역 namespace(비 `game`)인 점과 `FGridPosition` 신설 시 일관성.
- 렌더 시스템 다수가 현재 비활성 → 카메라 도입 시 기존 렌더 경로 회귀.
- 마우스 좌표/줌 변환 부호·스케일 실수(픽킹 어긋남).
- 2단계 인터페이스를 과설계해 1단계 DoD를 넘기는 것.

---

## 7. 산출물 형식

Markdown 문서 1개(예: 제안 이름 `docs/design/stage1-plan.md`). 아래 순서로:

1. 요약 (1단계 목표, DoD, 게이트 해결 방식 1문단)
2. 비목표(2단계 이상 선구현 금지 목록)
3. 의존성 그래프 (텍스트 다이어그램 또는 표: WP → 선행 WP, 그리고 0단계 의존 표시)
4. 작업 패키지 WP-1..N (§6.2 형식)
5. 파일/디렉터리 변경 예상 맵 (신규 vs 수정)
6. 결정 필요 항목 + 추천안 (§6.3 형식)
7. 검증 계획 (§6.4)
8. 리스크와 완화
9. 착수 순서 체크리스트 (첫 커밋부터 DoD까지, 커밋 단위 제안)

## 8. 규칙

- 코드 편집/실행 금지. 읽기와 계획만.
- 없는 파일/스크립트/테스트 인프라를 전제하지 말 것 (§2).
- `TurnController`/`TurnSystem` 재작성 제안 금지. 재사용/확장만.
- 문서와 코드 사실이 충돌하면 코드 사실을 우선하고, 계획에 그 불일치를 명시.
- 추측한 값은 "가정"으로 표시. 게이트(좌표계 경계)는 반드시 결정안을 제시.

---

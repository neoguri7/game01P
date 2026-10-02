# 0단계 구현 위임 프롬프트 — 토대 정리 (GameStateMachine + 데이터 레이어 + factory/씬 규약)

> 이 파일은 다른 AI(코딩 에이전트)에게 그대로 붙여넣어 작업을 의뢰하기 위한 **자기완결 프롬프트**다.
> 아래 `---` 구분선 사이의 내용 전체를 프롬프트로 전달한다.

---

## 1. 역할과 목표

너는 C++23 게임 프로젝트 `game01P`의 시니어 엔진/게임플레이 엔지니어다.
목표는 설계 문서 `docs/design/playable-roadmap.md`의 **0단계(토대 정리)**를 구현하는 것이다.
한 줄 목표: **이후 모든 콘텐츠가 하드코딩 없이 얹힐 수 있는 바닥을 만든다.**

작업 저장소 루트: `D:/repos/game01P` (Windows 기준. macOS/Linux/WSL 병행 checkout 가능).

구현할 것은 정확히 다음 3가지다.

- **A. `GameStateMachine`을 `Engine::update`/`render` 경로에 실제로 연결**하고
  `Boot → Title → Hub → Battle` 전이를 배선한다. 상태별로 필요한 시스템만 실행되는 구조를 도입한다.
- **B. 데이터 레이어**: `assets/data/` 디렉터리 + `schema_version` 규약 + JSON 로더 유틸(시스템 아님).
  로딩 실패는 진단 메시지로 표면화한다.
- **C. 엔티티 factory(유닛/스킬/룸/장비 생성)와 씬 load/unload 규약**.

---

## 2. 먼저 읽어야 할 문서 (반드시 순서대로)

1. game-programming-check skill
2. `docs/design/playable-roadmap.md` — 특히 §2 하드 제약, §3 현재 구현 상태, §5 **0단계**의 작업/DoD.
3. `docs/design/game-design.md` — 장르/런/전투/빌드/메타 계약. 충돌 시 이 문서가 우선한다.


주의: `docs/guidelines/game-code-changeability.md`(R1~R33)와 `scripts/check.sh`는 문서에서
참조되지만 **현재 저장소에 파일이 없다**. 있으면 지침을 따르고, 없으면 "하드코딩 금지 /
시스템 간 통신은 typed event / 데이터는 JSON" 규칙을 준수한다. 새 지침 파일을 임의로 만들지 말 것.

---

## 3. 현재 코드 사실 (추측 금지, 이 사실에 맞춰 작업)

빌드:
- CMake 3.21+, C++23 (`CXX_STANDARD 23`), vcpkg manifest 모드.
- 의존성: SDL3, SDL3_image, SDL3_ttf, EnTT, glm, imgui, Tracy, miniaudio, spdlog.
  **JSON 라이브러리 없음** (nlohmann_json 미포함).
- `CMakeLists.txt`는 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS "src/*.cpp" "src/*.h" "src/*.hpp")`로
  소스를 자동 수집한다. `src/` 아래에 파일만 추가하면 빌드에 포함된다.
- `tests/`는 비어 있고 ctest 타깃 없음. (이번 단계에서 테스트 인프라 구축은 선택)
- Windows 프리셋: `windows-msvc-debug` / `windows-msvc-release`. Linux: `linux-clang-*`.

코드 구조(핵심):
- 네임스페이스 `game`. 타입은 `F` 접두어(`FBaseState`, `FTitleState`, `FAssetManager`,
  `FResourceManager`). ECS 시스템은 `game::ecs`, 파일은 `.h`(헤더 전용 시스템) 또는 `.h/.cpp`.
- `src/core/Engine.{h,cpp}`: SDL 창/렌더러, `entt::registry registry`, `Time frameTime`,
  `SystemManager systemMgr`, `running`, `overlayRenderer` 소유.
  - `Engine::update(float dt)`는 현재 `systemMgr.updateAll(registry, dt)`만 호출하고
    `// TODO: optional state machine update` 주석이 있다. **여기가 A의 연결 지점.**
  - `Engine::render()`는 `ApplyEngineWorldRenderEffect(registry, systemMgr)`를 호출하며,
    그 구현(`src/core/EngineAbilityPipeline.cpp:248`)이 `systemManager.renderAll(registry)`을 부른다.
    그 뒤 `ApplyEngineOverlayRenderEffect(...)`가 `overlayRenderer`를 호출한다.
  - `Engine`은 `getRegistry()`, `getRenderer()`, `getSystemManager()`를 노출한다.
  - ctx 서비스는 `Engine::initializeContextServices()`에서 순서대로 emplace된다:
    `FInputState` → `FAudioManager` → `FEventBus` → `FAssetManager` → `FEngineWindowState`
    → `FEngineFrameState` → `FEngineRuntimeState` → `SDL_Renderer*` → `FResourceManager`.
    종료 순서는 `shutdownContextServices()`에 역순으로 있다. **이 순서를 깨지 말 것.**
- `src/core/SystemManager.h`: `addSystem<T>()`(타입 안전), `addSystem(unique_ptr<ISystem>)`,
  `onAllSystemsRegistered(reg)`, `updateAll(reg, dt)`, `renderAll(reg)`, `getSystemNames()`, `clear()`.
  **활성/비활성 개념이 없다.** 등록 순서 == 실행 순서.
- `src/ecs/systems/ISystem.h`: `ISystem`(update/render/onRegister/name) + `SystemWrapper<T>`,
  `SystemConcept` = `update(reg, dt)` + `name()`.
- `src/states/FBaseState.h`:
  - `enum class EStateTransition { None, ToTitle, ToHub, ToPrototype, ToGameOver, Quit };`
  - `FBaseState`는 `virtual EStateTransition Update(float DeltaTime)`, `OnEnter()`, `OnExit()`.
    **registry/dt를 받지 않는다.** 상태가 registry에 접근하려면 시그니처 확장이 필요하다.
- `src/states/GameStateMachine.h`: `pushState(unique_ptr<FBaseState>, entt::registry&)`,
  `switchState(...)`, `popState()`, `EStateTransition update(float dt)`를 가진
  `std::stack<unique_ptr<FBaseState>>` 껍데기. 첫 인자는 registry를 받지만 내부에서 쓰지 않는다.
- `src/states/FTitleState.cpp`: `Update`가 `return EStateTransition::None;` (TODO).
- `src/states/FHubState.{h,cpp}`: `OnEnter`/`OnExit`/`Update` 모두 빈 껍데기.
- `src/core/AssetManager.h`: `FAssetManager`는 로딩 API 없는 placeholder ctx 서비스
  (`Initialize`/`Shutdown`만).
- `src/core/events/FEventBus.h`: `publish`/`queueFrame` typed event 버스. 시스템 간 통신은 이걸 쓴다.
- `src/core/Logger.h`: `LOG_INFO`/`LOG_WARN`/`LOG_ERROR` 매크로 + `FLogger` ctx 서비스.
- `src/main.cpp`: `Engine` 생성 → `initialize("Game01P - ECS Prototype", 1280, 720)` →
  `RegisterSystems(engine.getSystemManager())`에서 `MoveSystem`, `CollisionSystem`,
  `AnimationSystem`, `DebugPrimitiveRenderSystem`, `SpriteRenderSystem`, `TurnDemoSystem`,
  `TurnSystem` 등록 → `onAllSystemsRegistered` → `setOverlayRenderer(RenderDebugOverlay)` →
  `run()` → 종료 시 `ShutdownTurnDemo`/`ShutdownTurnRuntime`/`shutdown`.
  **현재 `GameStateMachine`은 어디서도 생성되지 않는다.**
- 턴 스케줄링은 구현 완료: `src/gameplay/turn/TurnController.{h,cpp}`, `TurnTypes.h`,
  `src/ecs/systems/TurnSystem.{h,cpp}`, `src/debug/TurnDemo.{h,cpp}`.
  **재작성/리팩터 금지. 그대로 둔다.**

---

## 4. 구현 요구사항

### A. 상태 머신 연결 + 상태별 시스템 실행

- `Engine`이 `GameStateMachine`을 소유하게 한다(멤버 또는 registry ctx 중 택1, 근거를 보고에 적을 것).
  상태 Update가 registry/dt에 접근할 수 있도록 `FBaseState` 시그니처를 확장한다.
  권장: `virtual EStateTransition Update(entt::registry&, float)`, `OnEnter(entt::registry&)`,
  `OnExit(entt::registry&)`. 기존 서브클래스(`FTitleState`, `FHubState`)도 함께 수정한다.
- `Engine::update`에서 상태 머신을 갱신하고 반환된 `EStateTransition`을 실제 전이로 처리한다.
  - `None` → 유지, `ToTitle`/`ToHub`/`ToPrototype` → 해당 상태로 전환, `Quit` → `running = false`.
  - enum에 `Boot`/`ToBattle`이 없다. 로드맵의 `Boot → Title → Hub → Battle`을 표현할 방법을 정하되
    기존 enum/주석의 "게임플레이 상태는 ECS 태그 + 이벤트로" 규칙을 깨지 말 것.
    초기 상태는 Title로 두고, 필요하면 `EStateTransition::ToBattle`을 추가한다(추가 시 근거 기록).
- **상태별 시스템 on/off 구조**를 도입한다(`updateAll` 일괄 실행 제거).
  `SystemManager`에 활성/비활성(예: `setEnabled(name/index, bool)`, `updateEnabled`, 혹은
  "현재 상태가 요구하는 시스템 목록"을 상태가 등록) 중 하나를 구현하고, `Engine::update`는
  활성 시스템만 실행한다. 어떤 설계를 택했는지와 이유를 보고에 명시할 것.
  - 최소 요건: Title에서는 전투/턴 시스템이 돌지 않고, Battle(더미)에서만 더미 시스템이 돈다.
- 전이는 `switchState`/`pushState` 중 의미에 맞는 것을 쓰고, `OnExit`→`OnEnter` 순서를 보존한다.
- 상태 전이를 디버그 UI(기존 `RenderDebugOverlay` 경로 또는 상태 HUD)에서 확인 가능하게 한다.

### B. 데이터 레이어

- `assets/data/` 디렉터리를 만들고, 각 JSON 파일에 `"schema_version"` 필드를 **필수**로 둔다.
- JSON 로더 유틸을 만든다. **ECS 시스템이 아니라 순수 유틸**(예: `src/core/JsonLoader.h`,
  또는 `src/core/assets/`)로 둔다.
  - nlohmann_json을 쓰려면 `vcpkg.json` dependencies에 `nlohmann-json`을 추가하고
    `CMakeLists.txt`에 `find_package(nlohmann_json CONFIG REQUIRED)` +
    `target_link_libraries(game01P PRIVATE nlohmann_json::nlohmann_json)`를 추가한다.
    이 경우 "새 의존성 추가" 사실과 이유를 보고에 반드시 적는다.
  - 외부 의존성을 늘리지 않으려면 최소 파서를 직접 작성해도 된다. 이 경우 지원 범위를 명시한다.
- 로딩 실패(파일 없음/파싱 실패/`schema_version` 없음/버전 불일치)는
  `LOG_ERROR`/`LOG_WARN` + `std::optional`/`expected`/에러 구조체 같은 호출자가 감지 가능한 형태로
  **표면화**한다. 조용한 실패/빈 기본값 대체 금지.
- `FAssetManager`는 placeholder이므로, 로더를 여기에 얹을지 별도 유틸로 둘지 결정하고 근거를 적는다.
  최소 하나의 실제 데이터 JSON(예: 더미 유닛 정의)이 `assets/data/`에 있어야 한다.

### C. 엔티티 factory + 씬 load/unload 규약

- factory: 유닛(아군/적), 스킬, 룸, 장비를 **데이터(JSON)에서** 엔티티/구조체로 생성하는 경로.
  하드코딩된 수치를 factory에 박지 말고 데이터에서 읽는다.
- 씬 규약: 씬 로드/언로드 API와 수명 규칙을 정한다. 언로드 시 registry에서 해당 엔티티를
  제거하는 책임 소재를 명확히 한다(예: 씬 스코프 태그 컴포넌트 + teardown 함수).
- 0단계에서는 **더미 Battle 씬** 하나로 충분하다. 그리드/전투 로직은 1·2단계이므로 금지.

---

## 5. 하드 제약 (위반 금지)

- 턴제, 8x8 그리드, 파티 4명, AP 2종(이동 1/스킬 2), 속도 기반 이니셔티브 — 설계 확정. 편의로 바꾸지 말 것.
- 수치·아이템·스킬·룸 정의는 `assets/data/*.json`(+`schema_version`). 하드코딩 금지.
- 시스템 간 통신은 **typed event**(`FEventBus`). 문자열 디스패치 금지.
- `TurnController`/`TurnSystem`을 재작성하거나 인터페이스를 바꾸지 말 것.
- ctx 서비스 init/shutdown 순서(`Engine::initializeContextServices` / `shutdownContextServices`)를
  깨지 말 것. 새 서비스를 추가하면 양쪽에 순서를 맞춰 반영한다.
- `EngineAbility*` 파이프라인의 effect 분리 패턴(init/frame/render/input)을 존중한다.
- 1·2단계 내용(그리드, HP, 데미지, 스킬, AI, 승패)을 미리 구현하지 말 것. 더미 Battle은 껍데기면 된다.
- 접미어 없는 struct/enum 전면 도입 금지. 기존 `F` 접두어 관례를 따른다.
- 빌드가 깨진 상태로 끝내지 말 것. `windows-msvc-debug` 프리셋 기준으로 configure+build 성공이 필수다.

---

## 6. DoD (완료 기준) — 스스로 검증할 것

1. Title에서 Hub로, Hub에서 더미 Battle 씬으로 **실제로 전이**된다.
2. `assets/data/*.json` 하나를 로드해 엔티티를 factory로 생성하는 경로가 실제로 동작한다.
3. 상태 전이 후 이전 상태의 시스템이 더 이상 실행되지 않는다(로그 또는 디버그 UI로 확인 가능).
4. schema_version 누락/파싱 실패 시 진단 메시지가 나오고 크래시하지 않는다.
5. Windows 프리셋으로 configure + build 성공. (가능하면 실행해 3번을 눈으로 확인)
6. 변경 파일 목록과 각 변경의 이유, 선택한 설계와 근거를 보고한다.


## 7. 범위 밖 (하지 말 것)

- 그리드/타일, HP/데미지, AP, 스킬, AI, 승패 (1·2·3단계)
- 런/던전 루프, 거점, 파밍, 메타, 세이브 (4~7단계)
- 테스트 프레임워크 대규모 도입 (선택. 도입 시 최소 1개 스모크 테스트만)
- 문서 대규모 재작성. 필요한 경우 `playable-roadmap.md`의 0단계 상태 표기만 갱신.

---

## 8. 보고 형식 (작업 완료 시)

다음을 순서대로 제출한다.

1. **변경 파일 목록** — 경로 + 한 줄 이유.
2. **핵심 diff 요약** — 상태머신 시그니처, 시스템 활성화 API, 로더 API, factory API.
3. **선택한 설계와 근거** — (a) 상태머신 소유 위치, (b) 시스템 on/off 방식,
   (c) JSON 라이브러리 선택 또는 자체 파서 범위, (d) 로더 위치.
4. **검증 결과** — 빌드 명령/출력 요약, 실행 확인 내용.
5. **미해결/후속** — 1단계 착수 전에 남은 결정 사항.
6. **가정** — 문서에 없어 임의로 정한 값/이름.

작업 전에 위 §2 문서를 읽고, §4 구현 요구사항 중 애매한 점이 있으면 추측하지 말고
"가정"으로 표시한 뒤 최소 합리적 선택을 하고 진행한다.

---

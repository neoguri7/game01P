# 1단계(그리드 전장) 구현 실행 프롬프트 — 이 파일 하나로 착수

> 이 파일은 **구현 지시서**다. 다른 AI에게 이 파일 전체를 주고 "이대로 구현해"라고 하면,
> 추가 계획 수립 없이 바로 코드를 작성할 수 있게 작성되었다.
> 저장소 루트: `D:/repos/game01P`. 브랜치: 새 feature 브랜치에서 작업.

---

## 0. 임무

`docs/design/playable-roadmap.md`의 **1단계 — 그리드 전장**을 구현한다.

목표: **8x8 전장이 보이고, 유닛이 정수 칸에 놓이며, 유닛을 선택하면 이동 가능 칸이 하이라이트된다.**

산출물:
1. 동작하는 코드 (아래 §4 명세대로).
2. 좌표계 계약 문서 `docs/design/grid-coordinate-contract.md`.
3. 빌드 성공 + 눈으로 확인한 DoD.

**먼저 `docs/design/playable-roadmap.md`(§2 하드 제약, §5 1단계, §6 게이트)와
`docs/design/game-design.md`(8x8/파티4/AP2종)를 읽어라.** 충돌 시 game-design.md 우선.

---

## 1. 무시할 것 (로컬에 없는 파일/스크립트)

아래는 문서에서 참조되지만 저장소에 **없다**. 존재를 가정하거나 의존하지 마라. 새로 만들지도 마라.

- `docs/guidelines/game-code-changeability.md`, `docs/guidelines/agents/*`
- `scripts/` 디렉터리 전체 (`check.sh`, `remote-windows.sh`)
- `DEV_WORKFLOW.md`, `PROJECT_VISION.md`
- ctest/테스트 프레임워크 (`tests/`는 사실상 비었고 CMake에 `enable_testing()` 없음)

검증은 CMake 프리셋 직접 호출로만 한다 (§9).

---

## 2. 현재 코드베이스 사실 (0단계 구현 완료·미커밋)

이미 존재하는 API 위에 구현한다. 아래를 바꾸는 것은 §8에서 명시한 경우만 허용한다.

빌드:
- C++23, CMake 3.21+, vcpkg manifest. `CMakeLists.txt`의 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS "src/*.cpp|h|hpp")`
  → `src/` 아래 파일 추가만으로 빌드 포함. `nlohmann-json` 이미 의존성에 있음.
- 프리셋: `windows-msvc-debug`, `linux-clang-debug`.

상태 머신:
- `FBaseState` (`src/states/FBaseState.h`): `Update(entt::registry&, float) -> EStateTransition`(순수 virtual),
  `GetName() const noexcept -> string_view`(순수 virtual), `OnEnter(registry&)`, `OnExit(registry&)`.
- `EStateTransition { None, ToTitle, ToHub, ToBattle, ToPrototype, ToGameOver, Quit }`.
- `GameStateMachine` (`src/states/GameStateMachine.h`): `switchState(unique_ptr<FBaseState>, registry&)`,
  `update(reg,dt)`, `popState(reg)`, `getCurrentStateName()`. `std::stack` 보관. **`currentState()` 접근자 없음.**
- `Engine` (`src/core/Engine.{h,cpp}`)이 `GameStateMachine stateMachine` 멤버 소유.
  - `Engine::update`: `applyStateTransition(stateMachine.update(registry, dt))` → `syncStateSystemPolicy()` → `systemMgr.updateEnabled(registry, dt)`.
  - `Engine::applyStateTransition`: `ToHub`→`FHubState`, `ToBattle`/`ToPrototype`→`FBattleState`.
  - `Engine::syncStateSystemPolicy()`: 매 프레임 `systemMgr.disableAll()` 후
    `stateMachine.getCurrentStateName() == "Battle (dummy)"`이면 `setEnabled("Stage0BattleSystem", true)`만. **이 함수를 §4-D에서 교체한다.**
  - `Engine::render`: `ApplyEngineWorldRenderEffect`(=`systemManager.renderEnabled`) → `overlayRenderer`(=`RenderDebugOverlay`).
- `FBattleState` (`src/states/FBattleState.{h,cpp}`): `GetName()=="Battle (dummy)"`, `Update`가 `Cancel`(Esc) 시 `ToHub` 반환. **§4-D에서 확장.**
- `FHubState`: `Confirm`→`ToBattle`, `Cancel`→`ToTitle`. `FTitleState`: `Confirm`→`ToHub`.

시스템 관리:
- `SystemManager` (`src/core/SystemManager.h`): `addSystem<T>()`, `onAllSystemsRegistered`, `updateEnabled(reg,dt)`,
  `renderEnabled(reg)`, `setEnabled(string_view name, bool)`, `disableAll()`, `isEnabled(name)`,
  `getSystemNames()`, `getEnabledSystemNames()`. 등록 순서 = 실행 순서.
- 시스템 컨셉(`src/ecs/systems/ISystem.h`): `update(reg,dt)`, `name()`; 선택적으로 `render(reg)`, `onRegister(reg)`.
  헤더 전용 시스템은 `std::string name() const` 만 있으면 됨(예: `src/ecs/systems/MoveSystem.h` 참고).

데이터/씬/factory:
- `FJsonLoader::Load(path, expectedSchemaVersion=1) -> std::expected<nlohmann::json, FJsonLoadError>`
  (`src/core/JsonLoader.h`). top-level은 object여야 하고 `schema_version`(int) 필수.
- `FScene` (`src/gameplay/factory/FScene.h`): `FScene(entt::registry&, std::string name)`, `Load()`, `Unload()`,
  `IsLoaded()`, `CreateEntity() -> std::optional<entt::entity>`, `Emplace<T>(entity, args...)`. 자신이 만든 엔티티만 destroy. **비복사·비이동.**
- `FEntityFactory` (`src/gameplay/factory/`):
  `CreateUnit(FScene&, const json&) -> expected<entt::entity, FFactoryError>` (`FTag`+`FUnitData` 부착),
  `CreateSkill(const json&)`, `CreateRoom(const json&)`, `CreateEquipment(const json&)`.
  `FFactoryError{ std::string message; }`.
- `FUnitDefinition{ id, displayName, EUnitFaction faction }`, `EUnitFaction{ Party, Enemy }` (`FEntityDefinitions.h`).
- `assets/data/{unit,skill,room,equipment}.json` 존재 (스키마: `schema_version`,`id`,`display_name`, unit `faction`, room `room_type`, equipment `slot`).
- `src/main.cpp`: `RegisterSystems(systemMgr)`에 8개 시스템 등록 후 `onAllSystemsRegistered`; 시작 시
  `FScene startupScene(registry,"Stage0Startup")`을 Load하고 4종 정의를 생성; `run()` 후 Unload.

ECS:
- `FPosition{float x,y}` — **전역 namespace(비 `game`)** (`src/ecs/components/FPosition.h`).
- `FCamera{glm::vec2 position{0,0}; float zoom{1.f}}` (`src/ecs/components/FCamera.h`). **현재 어떤 렌더 시스템도 사용 안 함.**
- `FLayer{int depth}`, `FDebugPrimitive{...}`, `FTag{std::string value}`, `FSpeed{int32 value}`, `FTurnParticipant{}`, `FVelocity{vx,vy}`, `FCollider`, `FSprite`, `FAnimation`, `FText`.
- `src/core/EngineState.h`: `FEngineWindowState{int width{0}, height{0}; bool windowOpen, minimized, focused}` 가 registry ctx에 있음.
- `FInputState` (`src/core/InputState.h`): `mousePos`, `mouseLeftPressed`, `mouseLeftHeld`, `mouseLeftHeld`,
  `uiCapturesKeyboard/Mouse`, `keysHeld[256]`, `keysJustPressed[256]`,
  `isActionPressed/isActionHeld(EInputAction)`. 액션: `MoveUp/Down/Left/Right`(WASD), `Confirm`(Enter), `Cancel`(Esc), `DebugToggle`(grave).
  **마우스 휠 필드 없음.**
- 턴: `TurnController`, `TurnSystem` (`src/gameplay/turn/`, `src/ecs/systems/TurnSystem.h`).
  **재작성/인터페이스 변경 금지.** 1단계에서 켜지 않는다(2단계).
- `RenderDebugOverlay` (`src/debug/DebugOverlay.cpp`): Engine Stats + Entity Inspector + TurnDemo. 시스템 active/inactive 목록 표시.

---

## 3. 확정 설계 결정 (이대로 구현, 재론 금지)

### D1. 좌표계 계약 (로드맵 §6 게이트 해결)

- **tactical 진실 공급원 = `FGridPosition{int x,y}`** (정수 칸). 그리드 원점은 좌상단 (0,0), x는 오른쪽, y는 아래.
- **`FPosition{float}` = 표현 전용(presentation).** 절대 권위가 아니다. 유닛 배치/이동 시
  `FGridPosition`에서 파생해 갱신한다. 전투 로직은 `FPosition`을 읽지 않는다.
- 변환 함수는 **한 곳에만** 둔다: `src/gameplay/grid/FGridTypes.h`.
- 전투에서는 **cute_c2 미사용.** `CollisionSystem`은 Battle에서 활성화하지 않는다.
- 이 규칙을 `docs/design/grid-coordinate-contract.md`에 문서화한다.

### D2. 그리드 상수/기하

- `kGridWidth=8`, `kGridHeight=8`, `kTileSize=64.f`.
- World 좌표 = `origin + (x*tileSize, y*tileSize)` (칸 좌상단 기준). 렌더/픽킹 모두 동일 함수 사용.
- 좌표 변환과 카메라 변환을 분리: grid↔world는 `FGridTypes`, world↔screen은 카메라 수학.

### D3. 시스템 활성화 정책

- 문자열 상태 이름 비교를 버리고, **상태가 필요한 시스템 목록을 선언**하게 한다.
- `FBaseState`에 `virtual std::span<const std::string_view> RequiredSystems() const noexcept { return {}; }` 추가.
- `Engine::syncStateSystemPolicy()`는 `currentState()->RequiredSystems()`를 순회해 `setEnabled`한다.
- 이를 위해 `GameStateMachine::currentState() -> FBaseState*` 접근자를 추가한다.

### D4. 씬 수명

- Battle 씬은 `FBattleState`가 소유한다. `OnEnter`에서 `FScene` 생성+`Load()`+데이터 로드+카메라/유닛 생성,
  `OnExit`에서 ctx 정리 후 `scene.reset()`.
- 기존 `main.cpp`의 `startupScene`(Stage0Startup)은 **그대로 둔다**(회귀 방지). Battle 씬은 별도.

### D5. 렌더 방식

- Battle에서 켜지는 렌더 시스템은 **새 `GridRenderSystem` 하나**. 그리드 선, 하이라이트, 유닛을 모두 이 시스템이 그린다.
- 기존 `DebugPrimitiveRenderSystem`/`SpriteRenderSystem`의 카메라 개조는 **하지 않는다**(Battle에서 비활성).
  카메라 변환은 `GridRenderSystem`이 담당한다.
- `FCamera`는 Battle 씬의 카메라 엔티티에 붙인다 (`FTag{"battle_camera"}` + `FCamera`).

### D6. 선택 상태

- registry ctx 서비스 `FGridSelection`에 보관한다. Battle `OnEnter`에서 emplace, `OnExit`에서 erase.
- 선택/하이라이트는 매 프레임 `GridSelectSystem`이 갱신한다.

### D7. 카메라 조작

- 팬: `MoveUp/Down/Left/Right`(WASD) held. `Left` = 월드 x 감소, `Up` = 월드 y 감소.
- 줌: **마우스 휠.** `FInputState`에 `float mouseWheelDelta{0.f}` 추가, `SDL_EVENT_MOUSE_WHEEL` 처리,
  `beginFrame`에서 리셋. zoom 범위 clamp `[0.25f, 4.0f]`.

---

## 4. 구현 명세 (파일별)

### A. 그리드 좌표계

**신규** `src/ecs/components/FGridPosition.h`
```cpp
#pragma once
namespace game {
struct FGridPosition { int x{0}; int y{0}; };
inline bool operator==(const FGridPosition& a, const FGridPosition& b) noexcept { return a.x==b.x && a.y==b.y; }
}
```

**신규** `src/gameplay/grid/FGridTypes.h` + `.cpp`
```cpp
#pragma once
#include <glm/vec2.hpp>
#include <vector>
#include "ecs/components/FGridPosition.h"
namespace game {
inline constexpr int   kGridWidth  = 8;
inline constexpr int   kGridHeight = 8;
inline constexpr float kTileSize   = 64.f;

struct FGridMetrics {
    int   width {kGridWidth};
    int   height{kGridHeight};
    float tileSize{kTileSize};
    glm::vec2 origin{0.f, 0.f};
};

[[nodiscard]] bool       IsInsideGrid(const FGridMetrics&, int x, int y) noexcept;
[[nodiscard]] glm::vec2  GridCellTopLeftToWorld(const FGridMetrics&, int x, int y) noexcept;
[[nodiscard]] glm::vec2  GridCellCenterToWorld(const FGridMetrics&, int x, int y) noexcept;
[[nodiscard]] bool       WorldToGridCell(const FGridMetrics&, glm::vec2 world, int& outX, int& outY) noexcept;
[[nodiscard]] std::vector<FGridPosition> OrthogonalNeighbors(FGridPosition cell);
}
```
`.cpp`에 구현. `WorldToGridCell`은 `floor((world-origin)/tileSize)`, 범위 밖이면 `false`.

**신규** `src/gameplay/grid/FCameraMath.h` + `.cpp`
```cpp
#pragma once
#include <glm/vec2.hpp>
#include "ecs/components/FCamera.h"
namespace game {
struct FViewport { float width{1280.f}; float height{720.f}; };
[[nodiscard]] glm::vec2 WorldToScreen(glm::vec2 world, const FCamera&, const FViewport&) noexcept;
[[nodiscard]] glm::vec2 ScreenToWorld(glm::vec2 screen, const FCamera&, const FViewport&) noexcept;
}
```
계약: `screen = (world - cam.position) * cam.zoom + viewport*0.5f`, 역변환은 그 반대.
`FViewport`는 `registry.ctx()`의 `FEngineWindowState.width/height`에서 채운다.

### B. 진영 태그 + 점유 조회

**신규** `src/ecs/components/FFaction.h`
```cpp
#pragma once
namespace game { struct FPartyMember {}; struct FEnemy {}; }
```

**신규** `src/gameplay/grid/FGridQuery.h` + `.cpp`
```cpp
#pragma once
#include <entt/entt.hpp>
#include <vector>
#include "ecs/components/FGridPosition.h"
namespace game {
[[nodiscard]] entt::entity FindGridOccupant(entt::registry&, int x, int y);
[[nodiscard]] bool         IsGridCellOccupied(entt::registry&, int x, int y);
// 1단계: 이동 AP 1 → 직교 인접 중 범위 내·비점유 칸만. 2단계에서 AP 기반 flood로 대체.
[[nodiscard]] std::vector<FGridPosition> ComputeReachableCells(
    entt::registry&, entt::entity unit, int moveAp);
}
```
`FindGridOccupant`: `view<FGridPosition>` 선형 탐색(그리드가 작아 충분). 동일 칸 중복은 배치 단계에서 금지.

### C. 데이터 + factory 확장

**신규** `assets/data/battlefield.json`
```json
{
  "schema_version": 1,
  "width": 8,
  "height": 8,
  "tile_size": 64,
  "origin_x": 0,
  "origin_y": 0,
  "units": [
    { "id": "party.a", "display_name": "A", "faction": "party", "grid_x": 0, "grid_y": 5 },
    { "id": "party.b", "display_name": "B", "faction": "party", "grid_x": 1, "grid_y": 5 },
    { "id": "party.c", "display_name": "C", "faction": "party", "grid_x": 2, "grid_y": 5 },
    { "id": "party.d", "display_name": "D", "faction": "party", "grid_x": 3, "grid_y": 5 },
    { "id": "enemy.a", "display_name": "EA", "faction": "enemy", "grid_x": 5, "grid_y": 2 },
    { "id": "enemy.b", "display_name": "EB", "faction": "enemy", "grid_x": 6, "grid_y": 2 }
  ]
}
```

**신규** `src/gameplay/grid/FBattlefieldData.h`
```cpp
#pragma once
#include <string>
#include <vector>
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/grid/FGridTypes.h"
namespace game {
struct FUnitPlacement {
    std::string id;
    std::string displayName;
    EUnitFaction faction{EUnitFaction::Party};
    FGridPosition cell;
};
struct FBattlefieldData {
    FGridMetrics metrics;
    std::vector<FUnitPlacement> units;
};
}
```

**수정** `src/gameplay/factory/FEntityFactory.h/.cpp`
- 추가:
  ```cpp
  [[nodiscard]] static std::expected<FBattlefieldData, FFactoryError>
  CreateBattlefield(FScene& scene, const nlohmann::json& document);
  ```
- 동작: `width/height/tile_size/origin_x/origin_y`(정수/숫자) 파싱 → `FGridMetrics`.
  `units` 배열 각 원소에서 `id/display_name/faction/grid_x/grid_y` 파싱 → 유닛 엔티티 생성:
  - `FTag{display_name}`, `FUnitData{FUnitDefinition{id,display_name,faction}}`,
  - `FGridPosition{grid_x,grid_y}`, `FPosition = GridCellTopLeftToWorld(metrics, x, y)`,
  - `FPartyMember` 또는 `FEnemy` (faction에 따라),
  - `FLayer{1}`.
  - 중복 셀/범위 밖은 `std::unexpected(FFactoryError{...})`.
- 기존 `ReadString` 헬퍼 재사용. 실패 메시지는 한국어/영어 무관하되 원인을 명시.

### D. 상태/정책 배선

**수정** `src/states/FBaseState.h`
```cpp
#include <span>   // 추가
// 클래스에 추가:
[[nodiscard]] virtual std::span<const std::string_view> RequiredSystems() const noexcept { return {}; }
```

**수정** `src/states/GameStateMachine.h`
```cpp
[[nodiscard]] FBaseState* currentState() noexcept {
    return stateStack.empty() ? nullptr : stateStack.top().get();
}
[[nodiscard]] const FBaseState* currentState() const noexcept {
    return stateStack.empty() ? nullptr : stateStack.top().get();
}
```

**수정** `src/core/Engine.cpp` `syncStateSystemPolicy()`
```cpp
void Engine::syncStateSystemPolicy() {
    systemMgr.disableAll();
    const FBaseState* state = stateMachine.currentState();
    if (!state) return;
    for (const auto name : state->RequiredSystems()) {
        if (!systemMgr.setEnabled(name, true)) {
            LOG_WARN("Required system not registered: {}", name);
        }
    }
}
```

**수정** `src/states/FBattleState.h/.cpp`
- 헤더:
  ```cpp
  #include <memory>
  #include <span>
  #include <string_view>
  #include <vector>
  #include "gameplay/factory/FScene.h"
  class FBattleState final : public FBaseState {
  public:
      EStateTransition Update(entt::registry&, float) override;
      [[nodiscard]] std::string_view GetName() const noexcept override { return "Battle (grid)"; }
      [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override;
      void OnEnter(entt::registry&) override;
      void OnExit(entt::registry&) override;
  private:
      std::unique_ptr<FScene> scene;
  };
  ```
- `GetName()` 문자열을 `"Battle (grid)"`로 변경. (Engine의 문자열 비교는 D3로 제거됨.)
- `RequiredSystems()` → `static constexpr std::string_view k[] = {"GridCameraSystem","GridSelectSystem","GridRenderSystem"}; return k;`
- `OnEnter(registry)`:
  1. `scene = std::make_unique<FScene>(registry, "BattleGrid"); scene->Load();`
  2. `auto doc = FJsonLoader::Load("assets/data/battlefield.json");` 실패 시 `LOG_ERROR` + `return` (Battle 빈 화면).
  3. `auto field = FEntityFactory::CreateBattlefield(*scene, *doc);` 실패 시 `LOG_ERROR`.
  4. 성공 시 `registry.ctx().emplace_or_replace<FGridMetrics>(field->metrics);`
  5. 카메라 엔티티: `scene->CreateEntity()` → `Emplace<FTag>(e,FTag{"battle_camera"})`, `Emplace<FCamera>(e,FCamera{})`.
     초기 카메라 `position`을 그리드 중앙 월드좌표로 설정.
  6. `registry.ctx().emplace_or_replace<FGridSelection>(FGridSelection{});`
- `OnExit(registry)`: `registry.ctx().erase<FGridSelection>(); registry.ctx().erase<FGridMetrics>();`
  (없으면 no-op) 그 후 `scene.reset();`
- `Update`: `Cancel`(Esc) 시 `ToHub` 반환 유지.

**신규** `src/gameplay/grid/FGridSelection.h`
```cpp
#pragma once
#include <entt/entt.hpp>
#include <vector>
#include "ecs/components/FGridPosition.h"
namespace game {
struct FGridSelection {
    entt::entity selected{entt::null};
    std::vector<FGridPosition> reachable;
};
}
```

### E. 입력 휠

**수정** `src/core/InputState.h`
- `float mouseWheelDelta{0.f};` 필드 추가.
- `beginFrame(...)`에서 `mouseWheelDelta = 0.f;`.
- `processEvent`에 case 추가:
  ```cpp
  case SDL_EVENT_MOUSE_WHEEL:
      if (!uiCapturesMouse) mouseWheelDelta += ev.wheel.y;
      return true;
  ```
(`<SDL3/SDL_events.h>` 이미 include됨.)

### F. 시스템 3종 (헤더 전용)

**신규** `src/ecs/systems/GridCameraSystem.h` — `name()=="GridCameraSystem"`
- `update(reg, dt)`:
  - `auto view = reg.view<FCamera>();` 첫 엔티티(또는 `FTag=="battle_camera"`) 카메라.
  - 입력 held: `MoveLeft/MoveRight/MoveUp/MoveDown` → `camera.position.x/y ∓= panSpeed * dt / camera.zoom`, `panSpeed=480.f`.
  - `input->mouseWheelDelta` → `camera.zoom = clamp(camera.zoom * std::pow(1.1f, wheel), 0.25f, 4.0f);`
  - `FInputState`/`FCamera` 없으면 조용히 return.

**신규** `src/ecs/systems/GridSelectSystem.h` — `name()=="GridSelectSystem"`
- `update(reg, dt)`:
  - `FGridSelection* sel = reg.ctx().find<FGridSelection>();` 없으면 return.
  - `FGridMetrics` ctx, `FCamera`, `FEngineWindowState`, `FInputState` 조회.
  - `input->mouseLeftPressed`면:
    - `screen = input->mousePos` → `world = ScreenToWorld(screen, cam, viewport)` → `WorldToGridCell(metrics, world, gx, gy)`.
    - 성공 시 `sel->selected = FindGridOccupant(reg, gx, gy)` (빈 칸이면 `entt::null`).
  - `sel->reachable = sel->selected != entt::null ? ComputeReachableCells(reg, sel->selected, 1) : {}`.
  - 선택 엔티티가 파괴됐으면 `sel->selected = entt::null`.

**신규** `src/ecs/systems/GridRenderSystem.h` — `name()=="GridRenderSystem"`, `render(reg)` 구현(update는 빈 함수)
- `reg.ctx().find<SDL_Renderer*>()` 없으면 return. `SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND)`.
- `FGridMetrics` ctx(없으면 기본), 카메라, `FEngineWindowState` viewport.
- 그리드: 각 칸 `GridCellTopLeftToWorld` → 네 꼭짓점 4개 `WorldToScreen` → `SDL_RenderLine` (색 70,70,90,255).
- 하이라이트: `FGridSelection::reachable` 각 칸 → 반투명 채움 (0,200,255,90) `SDL_RenderFillRect`.
- 유닛: `view<FGridPosition>` 순회. `FPartyMember`→(80,140,255), `FEnemy`→(230,80,80), 기본 회색. 칸 안쪽 여백 6px 사각형 채움+외곽선.
- 선택 유닛: 노란 외곽선 (255,220,60,255).
- 카메라 없으면 스크린=월드(zoom1, origin0)로 폴백.

### G. 등록

**수정** `src/main.cpp` `RegisterSystems`에 추가(등록 순서 유지: update 순서):
```cpp
systems.addSystem<game::ecs::GridCameraSystem>();
systems.addSystem<game::ecs::GridSelectSystem>();
systems.addSystem<game::ecs::GridRenderSystem>();
```
`Stage0BattleSystem` 등록은 유지(참조 제거하지 않음). 필요 include 추가.

### H. 문서

**신규** `docs/design/grid-coordinate-contract.md` — 아래를 명시:
- `FGridPosition`(int) = 전투 진실, `FPosition`(float) = 표현 전용 파생값.
- 칸↔월드 변환식, 원점/축 방향, `kTileSize`.
- 변환 함수 소유 위치(`src/gameplay/grid/FGridTypes.h`, `FCameraMath.h`)와 중복 금지.
- 전투에서 cute_c2 미사용.
- 2단계 이동 시 `FGridPosition` 갱신 → `FPosition` 재파생 규칙.

---

## 5. 하드 제약 (위반 금지)

- 8x8, 파티 4명, AP 2종(이동 1/스킬 2), 턴제, 속도 기반 이니셔티브 — 확정, 변경 금지.
- 수치/정의는 JSON + `schema_version`. 하드코딩된 배치/수치 금지.
- 시스템 간 통신은 typed event(`FEventBus`). 문자열 디스패치 금지(시스템 활성화 이름은 예외적으로 허용, D3).
- `TurnController`/`TurnSystem` 재작성·인터페이스 변경 금지.
- ctx 서비스 init/shutdown 순서(`Engine::initializeContextServices`/`shutdownContextServices`) 변경 금지.
- HP/데미지/AP 소비/AI/승패/스킬/런 — **구현 금지**(2단계+). `ComputeReachableCells`는 AP1 stub 유지.
- 기존 `DebugPrimitiveRenderSystem`/`SpriteRenderSystem`에 카메라를 끼워 넣지 말 것(D5).
- 빌드 깨진 상태로 종료 금지.

---

## 6. 구현 순서 (의존성 위상 순서 — 이 순서대로 커밋)

1. **A** 그리드 좌표계 (`FGridPosition`, `FGridTypes`, `FCameraMath`) — 의존 없음.
2. **H** 좌표계 계약 문서 — A 확정 직후.
3. **B** 진영 태그 + `FGridQuery` — A 의존.
4. **C** `battlefield.json` + `FBattlefieldData` + `CreateBattlefield` — A,B 의존(스폰이 변환 사용).
5. **E** 입력 휠 — 독립(작음).
6. **F** `GridCameraSystem`, `GridRenderSystem` — A,B 의존.
7. **D** `FBaseState::RequiredSystems`, `GameStateMachine::currentState`, `Engine::syncStateSystemPolicy`,
   `FBattleState` 씬 로드/`FGridSelection` — C,F 의존(씬이 시스템 이름을 켬).
8. **F** `GridSelectSystem` — D(선택 ctx)와 F(렌더) 의존.
9. **G** main 등록 → 빌드/실행 검증 (§9).

순서를 바꿔야 하면 의존 이유를 보고에 적는다.

---

## 7. DoD (완료 기준)

1. Title → Enter → Hub → Enter → Battle 전이가 유지된다.
2. Battle에서 **8x8 격자가 화면에 렌더**된다.
3. 유닛이 **정수 칸**에 배치되고(JSON 좌표 그대로), 파티=파랑 / 적=빨강으로 보인다.
4. WASD로 카메라가 팬되고, 마우스 휠로 줌이 된다(clamp 동작).
5. 유닛을 클릭하면 선택 외곽선이 표시되고, **이동 가능한 인접 칸이 하이라이트**된다.
6. 빈 칸 클릭 시 선택 해제.
7. Esc로 Hub 복귀, 재진입 시 Battle 씬이 깨끗이 재생성된다(엔티티 누수 없음; DebugOverlay의 Entity count로 확인).
8. 빌드 성공.

---

## 8. 변경 허용 파일 요약

수정: `src/core/Engine.cpp`, `src/core/InputState.h`, `src/states/FBaseState.h`,
`src/states/GameStateMachine.h`, `src/states/FBattleState.{h,cpp}`, `src/main.cpp`,
`src/gameplay/factory/FEntityFactory.{h,cpp}`.
신규: `src/ecs/components/FGridPosition.h`, `src/ecs/components/FFaction.h`,
`src/gameplay/grid/{FGridTypes.h,FGridTypes.cpp,FCameraMath.h,FCameraMath.cpp,FGridQuery.h,FGridQuery.cpp,FBattlefieldData.h,FGridSelection.h}`,
`src/ecs/systems/{GridCameraSystem.h,GridSelectSystem.h,GridRenderSystem.h}`,
`assets/data/battlefield.json`, `docs/design/grid-coordinate-contract.md`.
그 외 파일 수정 금지.

---

## 9. 검증 (필수 실행)

game-programming-check skill 호출

## 10. 보고 형식

1. 변경/신규 파일 목록(경로 + 한 줄 이유).
2. 각 파일의 핵심 API 시그니처 요약.
3. DoD 1~8 확인 결과(빌드 출력 요약 + 실행 관찰).
4. 가정·임의 결정(문서에 없던 값/이름).
5. 남은 이슈/2단계 인계 사항(예: `ComputeReachableCells`를 실제 AP flood로 교체 지점).


<!-- doc-verify subsystem=tactical-combat commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# tactical-combat

> The D&D-5.5e-inspired turn-based combat machine: its tag components, conditions, turn-order state, and the **intended** transition table (the largest unbuilt gap in the codebase).

## As-built

### Combat state tags

Eleven empty tag components exist under `src/ecs/components/` — the enumerated states of the combat state machine. They are **data only**; no system on disk currently advances them.

| Tag | File line |
| --- | --- |
| `FCombatStateSetup` | `src/ecs/components/FCombatStateSetup.h:5` |
| `FCombatStateInitiativeRolling` | `src/ecs/components/FCombatStateInitiativeRolling.h:5` |
| `FCombatStateRoundStart` | `src/ecs/components/FCombatStateRoundStart.h:5` |
| `FCombatStateTurnStart` | `src/ecs/components/FCombatStateTurnStart.h:5` |
| `FCombatStateAwaitingCommand` | `src/ecs/components/FCombatStateAwaitingCommand.h:5` |
| `FCombatStateEnemyThinking` | `src/ecs/components/FCombatStateEnemyThinking.h:5` |
| `FCombatStateResolvingAction` | `src/ecs/components/FCombatStateResolvingAction.h:5` |
| `FCombatStateTurnEndCheck` | `src/ecs/components/FCombatStateTurnEndCheck.h:5` |
| `FCombatStateNextTurn` | `src/ecs/components/FCombatStateNextTurn.h:5` |
| `FCombatStateVictory` | `src/ecs/components/FCombatStateVictory.h:5` |
| `FCombatStateDefeat` | `src/ecs/components/FCombatStateDefeat.h:5` |

Only two are touched by living code today: `FCombatStateSetup` is **set** by `FTacticalCombatStateFactory::createSetupState` (`src/core/factories/FTacticalCombatStateFactory.cpp:11`), and `FCombatStateAwaitingCommand` is **read** by the debug telemetry panel (`src/debug/TacticalCombatTelemetryPanel.cpp:6`).

### Turn-order state

Held on the combat state entity (created by `createSetupState`):

| Component | Fields | Anchor |
| --- | --- | --- |
| `FTacticalTurnOrder` | `units`, `currentIndex=-1`, `round=0` | `src/ecs/components/FTacticalTurnOrder.h:8-13` |
| `FInitiativeRoll` | `naturalRoll`, `initiativeBonus`, `total` | `src/ecs/components/FInitiativeRoll.h:5-10` |
| `FInitiativeBonus` | `+bonus` | `src/ecs/components/FInitiativeBonus.h:5` |
| `FTurnBudget` | `baseMovementFeet`, `remainingMovementFeet` | `src/ecs/components/FTurnBudget.h:5-9` |
| `FTurnResources` | `hasAction`, `hasBonusAction`, `hasReaction` | `src/ecs/components/FTurnResources.h:5-10` |
| `FQueuedTacticalCommand` | `ETacticalCommandAction` + target | `src/ecs/components/FQueuedTacticalCommand.h:18-26` |

`ETacticalCommandAction` (`src/ecs/components/FQueuedTacticalCommand.h:6-13`) = `Unknown, Move, Attack, Dash, Dodge, EndTurn`.

Per-unit data: `FTacticalUnit`, `FGridPosition`, `FHitPoints`, `FArmorClass`, `FSpeed`, `FAbilityScores`, `FTacticalAttack` — stamped by `FTacticalUnitFactory` (see [factories](factories.md)). Markers: `FActiveTacticalUnit`, `FPlayerControlledTacticalUnit`, `FAiControlledTacticalUnit`, `FUnitStateDefeated`.

### Conditions

Applied as additional components on a unit:

| Condition | Data | Anchor |
| --- | --- | --- |
| `FConditionBurning` | `remainingRounds=2` | `src/ecs/components/FConditionBurning.h:5-8` |
| `FConditionDodge` | tag (advantage to defense this turn) | `src/ecs/components/FConditionDodge.h:5` |
| `FConditionPoisoned` | `remainingRounds=-1` (persistent) | `src/ecs/components/FConditionPoisoned.h:5-8` |
| `FConditionStunned` | `remainingTurns=1` | `src/ecs/components/FConditionStunned.h:5-8` |

## Intended / In-progress

> The transition table below is the **first authoritative place** it is written down (R9). It is **reconstructed** — `[UNVERIFIED]` — from the state-tag names, warm `tactical_d20` system names recovered from stale build artifacts, and the D&D-5.5e design. Until the `gameplay/` tree is restored (see [RECOVERY.md](../RECOVERY.md)), no system drives these edges.

**Transition table** ([UNVERIFIED — out/build/macos-clang-debug/CMakeFiles/game01P.dir/src/gameplay/tactical_d20/systems/flow/]):

| From | Event / condition | To | Driving system |
| --- | --- | --- | --- |
| `Setup` | config loaded | `InitiativeRolling` | `TacticalD20SetupSystem` |
| `InitiativeRolling` | all units rolled | `RoundStart` | `TacticalD20InitiativeSystem` |
| `RoundStart` | round begin | `TurnStart` | `TacticalD20CombatLifecycleSystem` |
| `TurnStart` | active unit set | `AwaitingCommand` | `TacticalD20CombatLifecycleSystem` |
| `AwaitingCommand` | valid player command | `ResolvingAction` | `TacticalD20CommandValidationSystem` |
| `AwaitingCommand` | enemy turn begins | `EnemyThinking` | `TacticalD20EnemyAiSystem` |
| `EnemyThinking` | AI picks action | `ResolvingAction` | `TacticalD20EnemyAiSystem` |
| `ResolvingAction` | action resolved | `TurnEndCheck` | one of the `TacticalD20*ActionResolutionSystem`s |
| `TurnEndCheck` | more turns remain | `NextTurn` | `TacticalD20CombatLifecycleSystem` |
| `TurnEndCheck` | no turns remain | `RoundStart` (next round) | `TacticalD20CombatLifecycleSystem` |
| `NextTurn` | next unit | `TurnStart` | `TacticalD20CombatLifecycleSystem` |
| any | one side all defeated | `Victory` / `Defeat` | `TacticalD20CombatLifecycleSystem` |

**Recovered system set** ([UNVERIFIED — out/build/macos-clang-debug/CMakeFiles/game01P.dir/src/gameplay/tactical_d20/systems/]) — the intended combat `gameplay/` systems, named from stale `.o` artifacts:

- flow: `TacticalD20SetupSystem`, `TacticalD20InitiativeSystem`, `TacticalD20CombatLifecycleSystem`, `TacticalD20ConditionSystem`
- ai: `TacticalD20EnemyAiSystem`
- actions: `TacticalD20ActionEconomySystem`, `TacticalD20AttackActionResolutionSystem`, `TacticalD20DashActionResolutionSystem`, `TacticalD20MoveActionResolutionSystem`, `TacticalD20DodgeActionResolutionSystem`, `TacticalD20WaitActionResolutionSystem`
- input: `TacticalD20CommandDragInputSystem`, `TacticalD20CommandValidationSystem`, `TacticalD20MovementPathValidationSystem`
- presentation: `TacticalD20TelemetrySystem`, `TacticalD20UnitLabelSystem`, `TacticalD20VisualFeedbackSystem`, `TacticalD20ValidationChecklistSystem`

Action resolution is a family: one per command action (Attack/Dash/Dodge/Move/Wait), coordinated by `TacticalD20ActionEconomySystem` which spends `FTurnResources` (`hasAction` / movement). The action list matches `ETacticalCommandAction` (`src/ecs/components/FQueuedTacticalCommand.h:6-13`), mapped from the UI in [debug-overlay](debug-overlay.md).

Config/state containers ([UNVERIFIED — out/build/macos-clang-debug/CMakeFiles/game01P.dir/src/gameplay/tactical_d20/config/]) live under `gameplay/tactical_d20/config/` (`FTacticalD20ConfigLoader`, `FTacticalD20ConfigDefaults`, `FTacticalD20ConfigReader`) and were referenced by `BootstrapDemoScene` via `FTacticalCombatConfigLoader` (`src/debug/DemoBootstrap.cpp:5`,`:10`).

**Registration** ([UNVERIFIED — out/build/macos-clang-debug/CMakeFiles/game01P.dir/src/gameplay/SystemRegistration.cpp.o]) was provided by `gameplay/SystemRegistration` calling into a `tactical_d20` registration routine (`TacticalD20SystemRegistration`), wired from `src/main.cpp:15` — see [RECOVERY.md](../RECOVERY.md).

## How to extend

**Drive a transition (the intended pattern):**
1. In the driving system's `update`, read the current state tag(s) with `registry.view<...>()` (e.g. `FCombatStateAwaitingCommand`).
2. Apply the event (consume `frameEvents<T>()` from `FEventBus` for queued events, or react to `publish` for immediate — see [events](events.md)).
3. When the condition matches, `registry.remove<OldState>(e)` / `registry.emplace<NewState>(e)` and update `FTacticalTurnOrder`.
4. Keep the transition table beside the system (R9 / [CONVENTIONS.md](../CONVENTIONS.md) / `src/states/GameStateMachine.h:10-19` doctrine).

**Add a command action:**
1. Add the enum value to `ETacticalCommandAction` (`src/ecs/components/FQueuedTacticalCommand.h:6-13`).
2. Add the matching `TacticalD20<Action>ActionResolutionSystem` and an action-economy branch in `TacticalD20ActionEconomySystem`.
3. Wire a UI button in the telemetry panel (see [debug-overlay](debug-overlay.md)).

## Cross-references

- [factories](factories.md) — combat state + unit creation.
- [ecs](ecs.md) — the component catalog.
- [events](events.md) — event-driven transitions.
- [debug-overlay](debug-overlay.md) — the UI that issues commands.
- [app-flow-states](app-flow-states.md) — why combat is **not** in the app-flow machine.
- [RECOVERY.md](../RECOVERY.md)

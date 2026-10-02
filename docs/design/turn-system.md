# Speed-based rounds and turns

Implements the gameplay policy from issue #2 directly in the existing `game01P` application. This prototype schedules turns only: no combat actions, AP, AI, HP, grid, or victory logic. Development and validation use the same `main.cpp` application path, without a separate turn library or test executable.

## Dependencies and ownership

- `FSpeed` is a nonnegative `int32_t` live stat, independent of movement velocity.
- Valid entities with both `FSpeed` and `FTurnParticipant` participate. There is no party-size limit.
- `TurnController` owns the session/round counters, turn serial, round entries, cursor and `mt19937`. It depends on EnTT and the standard library, not SDL, ImGui or the event bus.
- `FTurnRuntime` in `registry.ctx()` owns the controller, persistent FIFO turn requests and the most recent turn error.
- `FTurnDemo` owns only its demo entity handles, labels, persistent mutation requests and UI settings. `TurnDemoSystem.cpp` is window-free; `TurnDemo.cpp` contains the ImGui renderer.
- Main registers demo processing before turn processing, invokes registration once, and calls `ShutdownTurnDemo` then `ShutdownTurnRuntime` before engine shutdown. Engine services do not know about gameplay.

## Controller API

Include `gameplay/turn/TurnController.h`; all contracts use namespace `game`.

- `start(registry, uint32_t seed) -> expected<void, ETurnError>`: explicitly start at round 1. Successful starts increment the session ID, which survives stop. Failed starts leave state and RNG unchanged.
- `endTurn(registry, FTurnToken expectedTurn) -> expected<void, ETurnError>`: explicitly end the current turn. Tokens compare session ID, round number, turn serial and full versioned entity. Duplicate, wrong-actor and old-session requests cannot end a newer turn.
- `synchronize(registry)`: mark removed pending slots Skipped and end removed active turns before advancing. Call after each participation-removal mutation if removal/re-entry can occur in one update; the controller cannot observe an unreported removal that was already undone.
- `stop()`: idempotently stop the controller and clear its snapshot; it retains the last session ID. `FTurnRuntime::stop()` additionally clears pending requests.
- `snapshot() -> FTurnSnapshot`: read-only copy with Idle/Running, session, round, optional current token and all current-round entries. Completed/Skipped slots remain visible until the next round.
- `takeEvents() -> vector<FTurnEvent>`: drain ordered value notifications; core callers should drain these regularly. No callbacks or reentrant publication occur.
- `SetTurnUnitSpeed(registry, entity, int32_t speed) -> expected<void, ETurnError>`: validate then change only the live stat. Errors preserve the original value.

Errors: `AlreadyRunning`, `NoParticipants`, `InvalidSpeed`, `NotRunning`, `InvalidEntity`, `MissingSpeed`, `StaleTurn`. Ending while Idle returns `NotRunning`; an outdated token while Running returns `StaleTurn`.

## Scheduling policy

Each round collects and validates participants, orders full entity IDs to eliminate registry iteration-order influence, stable-sorts speed descending, then Fisher–Yates shuffles only equal-speed ranges using `uniform_int_distribution`. A one-unit range consumes no random draw. Seed once per successful session; subsequent rounds continue the generator. Identical seed/entity/stat/command histories reproduce results in the same standard-library environment, not necessarily across platforms.

The round snapshot never reorders after live speed edits. Each continuously valid participant gets exactly one turn, including speed 0. Turns remain active without an explicit end request, regardless of time or frame count. The final completed/skipped slot immediately leads to the next round. New and returning participants join the next round; removed slots cannot be restored in the current round. Entity versions prevent a recycled index from inheriting an old slot.

All-participant removal ends the session without looping through empty rounds. Negative live values written around the validated API are checked at start and each round boundary. A bad next-round stat ends the session with an `InvalidSpeed` diagnostic, rather than sorting invalid values or clamping them.

## ECS requests and result events

Include `ecs/systems/TurnSystem.h`. After `onRegister`, enqueue `FStartTurnSession{seed}`, `FEndTurn{token}`, or `FStopTurnSession{}` in `FTurnRuntime::requests`. This deque survives `FEventBus::beginFrame()`, so render-time inputs are processed once during the next update. Stop cancels remaining queued requests; enqueue a fresh start in a later update to restart.

`TurnSystem` synchronizes first, processes requests FIFO, and records results after state changes using typed `queueFrame` events. It never uses frame events to store commands or `publish` to invoke callbacks.

- Start: `FTurnSessionStarted` → `FRoundStarted` → `FTurnStarted`.
- Normal transition: `FTurnEnded(Completed)` → `FTurnStarted`.
- Round boundary: `FTurnEnded` → `FRoundEnded` → `FRoundStarted` → `FTurnStarted`.
- Empty/invalid next roster: replace the final two events with `FTurnSessionEnded(NoParticipants/InvalidSpeed)`.
- Active removal: `FTurnEnded(Removed)` before advancing. Skipped pending slots produce no fake turn events.
- Explicit stop: `FTurnEnded(Stopped)` if active, then `FTurnSessionEnded(Stopped)`. An interrupted round is not reported as completed.

`FRoundStarted` owns the confirmed snapshot (including the first active token); turn events own full tokens. Session/round end events carry IDs and reasons. The event bus maintains per-type buckets; ordered mixed-type history is available from the controller's drained vector, not from a global bus log. The controller does not depend on the bus; the application's `TurnSystem` bridges its results. Runtime records invalid-round diagnostics in `lastError`; rejected requests produce no progression events.

## Turn Demo panel

Open the existing debug overlay's **Turn Demo** panel:

1. **Initialize / reset demo** creates A=30, B=20, C=20, D=10, E=0. It does not start a session.
2. Seed defaults to **42** and is editable. **Start** begins a session; **End current turn** captures its current token; **Stop** cancels the session and queued turn requests.
3. Inspect session, round, current actor, each fixed slot/state, round speed and live speed. Differences say **applies next round**. Next-round tie order is not predicted.
4. Edit live speed, toggle participation, delete owned units, or add a unit with a chosen speed. Negative values report errors without silently clamping.
5. Reset stops the session, cancels both queues, and replaces only demo-owned entities. Unrelated entities and registry services survive.

Rendering only reads scheduler/ECS state and enqueues commands (besides editing UI settings). `TurnDemoSystem` applies ECS commands FIFO and synchronizes after each, so removal followed by re-entry in one update still consumes/skips the old slot. Labels remain safe after entity deletion or Entity Inspector's **Clear All Entities**. Errors remain visible until reset or a later error replaces them.

Suggested manual checks: end several tied rounds, change C to 40 during A's turn and check the next-round-only effect, exclude/rejoin a waiting unit, delete the current unit, add a fast unit, and confirm E=0 still receives a turn. Do not expect every two successive tied rounds to differ.

## Build and run the existing application

TurnController, TurnSystem and the demo are compiled directly into `game01P` by the existing `src` source collection. `main.cpp` registers them alongside the existing ECS systems; the overlay displays the demo in that same application. There are no separate gameplay test targets, shared-library targets or later integration steps.

With the repository's vcpkg environment and native Windows MSVC toolchain:

```text
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --target game01P
.\out\build\windows-msvc-debug\Debug\game01P.exe

cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --target game01P
.\out\build\windows-msvc-release\Release\game01P.exe
```

## Validate in the Turn Demo panel

1. Initialize the five default units, then start with seed 42. Confirm descending speed order, B/C restricted to their tied group, E=0 receiving a turn, and no progression until **End current turn** is pressed.
2. During A's turn, change C's live speed to 40. The current round stays fixed; C moves ahead of A in the next round. Check both speed values and the next-round annotation.
3. Exclude/rejoin a waiting unit, remove the current unit, and add a fast unit. Skipped slots stay skipped, active removal advances safely, and additions join the next round.
4. Try a negative speed and repeated turn-end clicks. Check errors and verify that an old token cannot end a newer turn.
5. Stop, reset and restart. Check new session IDs, owned-only reset, and safe labels after **Clear All Entities**.

Windows CI keeps the Debug/Release matrix and configures/builds the `game01P` target directly in `build/windows`. Artifacts contain `game01P.exe` and its runtime DLLs. CI compilation does not replace interactive verification of these controls in the running application.

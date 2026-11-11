# WP-46: Run completion — theme victory with a choice to return, save-and-leave at camp, place label

Size: L · Player-facing: yes · Certainty: settled · Base commit: the commit that adds this spec · Branch: wp/WP-46

## 1. Goal
A run today has no end the player can reach by winning: after a theme's last boss the game shows the ordinary camp, and after the eighth dungeon it silently starts over. The only ways out are a party wipe or "End expedition", which is written as giving up. This WP gives the intended loop its ending: clearing a theme's final dungeon turns the camp into a victory screen with two choices, **descend deeper** or **return in triumph**; returning ends the run as a success on the run-end screen. It also adds the confirmed "save and leave" at camp, and shows where in the sequence the party is.

- Decisions it rests on (`docs/design/decisions.md`):
  - 런의 끝 (WP-46): after a theme's last boss the player gets a victory screen and chooses to go deeper or to return as a success; the run still does not end by dungeon count (user, 2026-11-11; 확정 22, 23, 111).
  - 전멸 뒤: the checkpoint survives a wipe (user, 2026-11-11).
  - 확정 31: saving and continuing is allowed at the camp.
  - 확정 27, 29: the last dungeon of a theme holds its boss.
- Sources: Slay the Spire continues after each act boss; Darkest Dungeon lets the player end an expedition and go home (`docs/design/research-2026-11-11.md`).
- Terms: a "theme" is a named group of dungeons in `dungeon.json` → `themes[]` (ship: Ember Crypt and Ashen Reach, four dungeons each); the sequence wraps after the last theme and counts a "cycle" (0 on the first pass). "Depth" is the 1-based dungeon number of the run. A camp follows every cleared dungeon and belongs to the dungeon just cleared, whose position is `PositionOf(run.depth - 1)`. A "run intent" is a player decision the run flow applies (`ERunIntent`, `ApplyRunIntent`). The "run-end screen" is the state `ember.runEnd` (`FEmberRunEndState`). "The gate" is `.claude/scripts/wp-gate.mjs`.

## 2. Player experience
- **Decision added**: at the camp after a theme's final dungeon the player chooses.
  - **Descend deeper**: the run goes on into the next theme (or the next cycle), keeping its runes, levels and materials. Favoured when the party is strong and the player wants to push the checkpoint further.
  - **Return in triumph**: the run ends as a success and pays 1 seal, every time. The run-end screen shows what was gained; the checkpoint stays, the run's runes, levels and materials end with it. Favoured when the party is worn down: a wipe later ends the same run without that seal.
  - The camp's services (rest or reinforce, rune offer, loadout) stay usable before the choice.
- **What the player sees before committing** (camp modal):
  - Heading `THEME CLEARED / <THEME NAME>` instead of `DUNGEON CLEARED / CAMP`, and a line `The boss of <theme name> has fallen. Seals: <total>.`
  - Button `Descend deeper: <next place>` and, under it, `Return in triumph (ends the expedition as a success)` with the line `Ends the run: its runes, levels and materials end with it. The checkpoint stays. Seals +1.`
  - At every other camp, under the checkpoint line: `Next: <next place>`.
  - `<place>` is `<theme name> <n>/<dungeons in theme>`, followed by `  |  Cycle <c>` from the second pass on, where `<c>` is the sequence's cycle number as the run-end summary already prints it (1 on the second pass): `Ashen Reach 1/4`, `Ember Crypt 1/4  |  Cycle 1`.
- **Save and leave**: the camp's "End expedition at checkpoint" dialog gains the "Save and leave" button the battle already has. It keeps the save and returns to the title; CONTINUE reopens the camp.
- **In battle**: the status line starts with the current place.
- **Run-end screen after a triumph**: heading `EXPEDITION COMPLETE`, the run's message, and the same ledger as after a defeat with the section title `What ends with the run / what stays`. The two buttons are unchanged ("New expedition from checkpoint", "Return to title").
- **Playtest hypothesis**: expect the player to treat the theme boss as the goal of a session and to weigh going on against banking the win · observe a `run_end` line with `result: "victory"` in the run log, a remark about the choice in the playtest notes, and CONTINUE used after a camp exit · kill-if the player never returns (the reward of returning is nothing) or always returns (going deeper is never worth it), or does not notice that the theme ended.

## 3. Behaviors
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN the run is at the camp after the last dungeon of a theme THEN `IsThemeClearedCamp` SHALL be true; at any other camp and in any other phase it SHALL be false. | `run_flow_tests::ThemeClearedCampDetected` |
| F10 | WHEN a triumph is granted THEN the account SHALL gain exactly `kTriumphSeals` seals each time, and nothing when meta progress is disabled. | `meta_save_tests::TriumphGrantsSealsEveryTime` |
| F2 | WHEN the player returns in triumph at a theme-cleared camp THEN the triumph seals SHALL be granted, the run's phase SHALL be Victory, the save SHALL be deleted, the checkpoint SHALL be unchanged, a `run_end` log line with result `victory` and reason `return` SHALL be written, and the result SHALL show the run-end screen. | `run_flow_tests::ReturnInTriumphEndsRunAsVictory` |
| F3 | WHEN returning in triumph is requested at a camp that is not theme-cleared, outside the camp, or while a rune offer is open THEN it SHALL be refused and the run SHALL be unchanged. | `run_flow_tests::ReturnInTriumphRefusedElsewhere` |
| F4 | WHEN the run is in the Victory phase THEN "retry" SHALL lead to the preparation screen and "leave" to the title, both dropping the run, exactly as after a defeat. | `run_flow_tests::VictoryExitsDropTheRun` |
| F5 | WHEN the player leaves the run at camp with "Save and leave" THEN the save SHALL be kept, the run SHALL be dropped, the result SHALL lead to the title, and continuing SHALL restore the Camp phase. | `run_flow_tests::LeaveRunAtCampKeepsSave` |
| F6 | WHEN a place label is built THEN it SHALL be `<theme name> <n>/<total>` plus `  |  Cycle <c>` when the cycle `c` is 1 or more. | `run_flow_tests::RunPlaceLabelFormat` |
| F7 | WHEN the camp modal host receives ReturnInTriumph or LeaveRun THEN it SHALL apply them; the run-end state SHALL accept retry and leave in the Victory phase. | `run_modal_tests::CampAndRunEndAcceptVictoryIntents` |
| F8 | WHEN the run-end texts are chosen THEN the Victory phase SHALL give `EXPEDITION COMPLETE` and `What ends with the run / what stays`, and Defeat SHALL give today's texts. | `run_debrief_tests::RunEndTextsFollowPhase` |
| F9 | WHEN the camp modal is rendered at a theme-cleared camp THEN it SHALL show the victory heading, the seals line, both buttons and the line stating what returning ends and pays; at another camp it SHALL show `Next: <place>` and no triumph button. | `camp_tests::CampViewShowsThemeVictory` |

## 4. Numbers
| Name | Value | Unit | Where | Knob | Metric it moves |
|---|---|---|---|---|---|
| Triumph seals | 1 | seals per return in triumph | `kTriumphSeals` in `FMetaProgress.cpp`, next to `kClearSeals` (1) and `kBossSeals` (2), which are code constants too | [0, 3] | share of theme-cleared camps where the player returns |

Why 1: the first win over a boss pays 2 once; returning is repeatable, so it pays less per time. It is the only repeatable seal source, which the account needs (16 seals can be earned once, the unlocks cost more); the full seal economy is WP-48. Difficulty per cycle is unchanged. No combat modifier is added.

## 5. File map
Modify:
- `src/gameplay/dungeon/FDungeonRun.h`, `FDungeonRun.cpp` — the Victory phase.
- `src/gameplay/dungeon/FDungeonSave.cpp` — phase id.
- `src/gameplay/dungeon/FRunFlow.h`, `FRunFlow.cpp` — predicate, place label, the new intent, leave at camp.
- `src/gameplay/dungeon/FRunLog.cpp` — result of a triumph.
- `src/gameplay/dungeon/FRunPath.cpp` — treat Victory like Defeat.
- `src/gameplay/meta/FMetaProgress.h`, `FMetaProgress.cpp` — the triumph grant.
- `src/states/FEmberModalHostState.cpp` — intents the camp host accepts.
- `src/states/FEmberRunEndState.h`, `FEmberRunEndState.cpp` — texts by phase.
- `src/presentation/FDungeonDefeatView.cpp` — heading by phase (the legacy view registered for the same modal id).
- `src/presentation/FDungeonCampView.cpp` — victory variant, next-place line, save and leave.
- `src/presentation/FDungeonViews.h` — the comment above `DrawAbandonConfirm` (the camp now offers "Save and leave" too).
- `src/ecs/systems/EmberCombatHudView.cpp` — place in the status line.
- `tests/features/run_flow_tests.cpp`, `run_modal_tests.cpp`, `run_debrief_tests.cpp`, `camp_tests.cpp`, `meta_save_tests.cpp` — §7.

`FDungeonRun.h`: `enum class EDungeonPhase { Battle, Reward, Event, Camp, Defeat, Victory };` Victory is a finished run, like Defeat. Every place that singles out Defeat treats Victory the same way:
- `FDungeonRun.cpp`: the save guard (the `run->phase == EDungeonPhase::Defeat` early return of `SaveDungeonRun`) and the node allocation guard (`CanAllocateDungeonNode`, Battle or Defeat) also apply to Victory.
- `FDungeonSave.cpp`: the phase-name switch returns `"victory"` (a Victory run is never saved; the case keeps the switch complete).
- `FRunPath.cpp`: the early return for a Defeat run also covers Victory.
- `FRunFlow.cpp`: `RunModalIdForPhase(Victory)` is `kRunModalRunEnd`; `PhaseId(Victory)` is `"victory"`; the phase list next to it gains Victory.

`FRunFlow.h` (namespace `game`):
```cpp
enum class ERunIntent {
    ContinueReward, ChooseEventRest, ChooseEventLoot, ChooseEventOption, ContinueEvent, LeaveCamp,
    AbandonRun, RetryAfterDefeat, LeaveRun, TakeFork,
    ReturnInTriumph,   // WP-46: end the run as a success at a theme-cleared camp
};

/// True when a run exists, its phase is Camp, a run sequence exists, and the dungeon just cleared
/// (PositionOf(run.depth - 1)) is the last dungeon of its theme.
[[nodiscard]] bool IsThemeClearedCamp(const entt::registry& registry);
/// "<theme name> <dungeonInTheme + 1>/<dungeons in theme>", plus "  |  Cycle <cycle>" when cycle >= 1.
/// `dungeonIndex` is 0-based over the whole run (depth - 1). "" without a run sequence.
[[nodiscard]] std::string RunPlaceLabel(const entt::registry& registry, int dungeonIndex);
```
`FRunFlow.cpp`:
- `ReturnInTriumph` (file-local, dispatched from `ApplyRunIntent`): no run → `NoRun()`; phase not Camp → `WrongPhase(phase)`; `!IsThemeClearedCamp` → `std::unexpected("The theme is not cleared yet.")`; then the same offer check as `LeaveCamp` (it calls `EnsureRewardsForRun` first and refuses an open offer with `"Resolve the rune offer before leaving camp."`). Then: `(void)GrantTriumphSeals(registry);` `run->phase = EDungeonPhase::Victory; run->message = "The expedition returns in triumph. " + <theme name> + " is cleared.";` `AppendRunEvent(registry, ERunEventKind::RunEnded);` `DeleteDungeonSave(registry);` `return FRunResult{ERunExit::ShowModal, RunModalIdForPhase(EDungeonPhase::Victory)};` The run stays in the registry for the run-end screen, as after a defeat. The checkpoint was already raised at camp entry and is not touched.
- `RetryAfterDefeat`: accepts Defeat and Victory.
- `LeaveRun`: the "finished run" branch (erase, `ToTitle`) covers Defeat and Victory; the "save, erase, `ToTitle`" branch accepts Battle and Camp. Other phases stay `WrongPhase`.
- `FRunLog.cpp`, `HandleRunEnded`: when the run's phase is Victory the line has `"result": "victory"` and `"reason": "return"`; Defeat and abandon are unchanged; the lost-room `room_end` line is still written for Defeat only.

`FMetaProgress.h` / `.cpp`: `inline constexpr`-style constant `kTriumphSeals = 1` beside `kClearSeals` and `kBossSeals`, and
```cpp
/// Grants kTriumphSeals for a run that returned in triumph (repeatable). Returns the amount granted;
/// 0 when meta progress is absent or disabled. Uses GrantSeals(registry, kTriumphSeals, "triumph").
int GrantTriumphSeals(entt::registry& registry);
```
It adds to the seal balance only; the lifetime-earned ledger that seal gates read is not changed.

`FEmberModalHostState.cpp`, `AcceptsIntent`: the camp also accepts `ERunIntent::ReturnInTriumph` and `ERunIntent::LeaveRun`.
`FEmberRunEndState.h` / `.cpp`:
```cpp
/// "EXPEDITION COMPLETE" for Victory, "EXPEDITION LOST" otherwise.
[[nodiscard]] const char* RunEndHeading(game::EDungeonPhase phase);
/// "What ends with the run / what stays" for Victory, "What you lost / what stays" otherwise.
[[nodiscard]] const char* RunEndLossSectionTitle(game::EDungeonPhase phase);
```
Both functions are declared at the scope the header's other helpers use. `Render` uses both (phase of the run in the registry; Defeat's texts when there is no run). The ledger lines under the section ("Runes lost: …") keep their wording in a victory. `Update` is unchanged: it already forwards RetryAfterDefeat and LeaveRun, which the flow now accepts in Victory. `FDungeonDefeatView.cpp` uses `RunEndHeading` for its heading too (it includes `states/FEmberRunEndState.h`, as `FTitleMenuView.cpp` already includes a states header).

`FDungeonCampView.cpp`:
- `DrawAbandonConfirm`: when `viaIntent` is true (the camp), the first two text lines ("The expedition is saved at every room. Leaving mid-battle restarts this room" / "when you continue. Abandoning deletes the save: …") become `"The expedition is saved at this camp; continuing reopens it. Abandoning deletes the save:"` and `"run EXP, nodes, runes and materials are lost."`; the third line is unchanged. When `allowLeave` and `viaIntent` are both true, "Save and leave" posts `PostRunIntent(registry, ERunIntent::LeaveRun)` instead of setting `run->leaveRequested`; the battle's call (`viaIntent` false) keeps today's behaviour.
- `RenderDungeonCampView`: call `DrawAbandonConfirm(registry, true, true)`.
- With `const bool themeCleared = IsThemeClearedCamp(registry);` and `const std::string next = RunPlaceLabel(registry, run.depth);` (the dungeon the descent leads to):
  - heading: `themeCleared ? "THEME CLEARED / " + <upper-case theme name> : "DUNGEON CLEARED / CAMP"`; the theme name comes from the sequence (`FindRunSequence(registry)->themes[PositionOf(run.depth - 1).theme].name`), upper-cased character by character with `std::toupper` on `unsigned char`. Strings built as `std::string` are passed to `ui::HeadingText` / `ui::Button` with `.c_str()`.
  - when `themeCleared`, directly under the run message: `ImGui::TextWrapped("The boss of %s has fallen. Seals: %d.", <theme name>, MetaSeals(registry));`
  - directly under the "Checkpoint: …" line, when not `themeCleared` and `next` is non-empty: `ImGui::TextDisabled("Next: %s", next.c_str());`
  - the descend button's label is `themeCleared ? "Descend deeper: " + next : "Descend to the next dungeon"`;
  - when `themeCleared`, directly under the descend button and inside the same `BeginDisabled(offerOpen)` scope: `if (ui::Button("Return in triumph (ends the expedition as a success)", {-1, 42})) { PostRunIntent(registry, ERunIntent::ReturnInTriumph); ImGui::CloseCurrentPopup(); }` followed by `ImGui::TextDisabled("Ends the run: its runes, levels and materials end with it. The checkpoint stays. Seals +%d.", kTriumphSeals);`
Stale comments in `FRunFlow.h` that count "five" phases are updated. `FDungeonCampView.cpp`, `FDungeonDefeatView.cpp` and `EmberCombatHudView.cpp` include `gameplay/dungeon/FRunFlow.h` and, for the defeat view, `states/FEmberRunEndState.h` where they do not already.
`EmberCombatHudView.cpp`: the `stats` buffer grows to 192 characters and its format becomes `"%s  |  MATERIALS %d  |  CHECKPOINT %d  |  NODES %d"` with `RunPlaceLabel(registry, run.depth - 1)` first; when the label is empty the old format is used.

Do not touch: the rest of `src/gameplay/meta/**` (only the triumph grant is added), `assets/data/**`, `src/gameplay/dungeon/FRunSequence.*` (the `end_on_final_boss` flag stays rejected when true: the ending is the player's choice, not a data flag), `src/states/FRunFlowStateMap.cpp` (a `ShowModal` from the camp already replaces the top modal), `src/dev/**` (the autopilot keeps descending at every camp), `.claude/scripts/**`, `tests/fixtures/**`, `CMakeLists.txt`, `docs/design/**`.

## 6. Tasks
Work in the developer worktree named in the handoff, on branch `wp/WP-46`. `node .claude/scripts/build.mjs --preset windows-msvc-agent` (run from that worktree) configures if needed, builds and runs every ctest entry; it prints only errors and failing tests. A case "passes" when it is absent from that output. `docs/wp/WP-46/progress.md` is created by the implementer; the gate allows it. The implementer never edits `spec.md` or `features.json`. Consecutive tasks may share one build when each has its own commit. The final build runs at the final HEAD so `out/evidence.json` carries that SHA.

1. **T1 Tests first (red)** — add the cases of §7; add `ERunIntent::ReturnInTriumph`, `EDungeonPhase::Victory` and the declarations of §5 with stubs (`IsThemeClearedCamp` returns false, `RunPlaceLabel` returns `""`, `RunEndHeading` and `RunEndLossSectionTitle` return the Defeat texts, `ApplyRunIntent` answers ReturnInTriumph with `std::unexpected("not implemented")`); complete every `switch` over `EDungeonPhase` so the build has no new warning. Done: build.mjs → the build succeeds; the only failing cases are the nine of §7 except `ReturnInTriumphRefusedElsewhere` (its refusals already hold against the stub); no existing case fails. Commit.
2. **T2 Flow** — `FDungeonRun.*`, `FDungeonSave.cpp`, `FRunFlow.*`, `FRunLog.cpp`, `FRunPath.cpp`, `FEmberModalHostState.cpp`. Done: build.mjs → the only failing cases are `RunEndTextsFollowPhase` and `CampViewShowsThemeVictory`.
3. **T3 Screens** — `FEmberRunEndState.*`, `FDungeonDefeatView.cpp`, `FDungeonCampView.cpp`, `EmberCombatHudView.cpp`. Done: build.mjs → 0 failing tests. Stop condition, reported as `BLOCKED:`: a test outside the file map fails (for example a run-log schema test that enumerates `run_end` results); report its name and assertion.
4. **T4 Evidence** — PowerShell, timeout 600000 ms: `node .claude/scripts/baseline.mjs --label wp46-flow --seeds 1-4 --policy "reward=card0,event=first,chest=open,camp=offer+1,rest=60,continue" | Out-File -Encoding utf8 out/baseline-wp46-flow.txt`. Done: exit code 0 and the file is non-empty (the autopilot still descends at every camp, so this only proves the flow did not break); copy the report's battle rows into `docs/wp/WP-46/progress.md`.

## 7. Tests
`tests/features/run_flow_tests.cpp` (`ExitTransitionsDropTheRun` shows how a case checks that the run and the save are gone). Register new cases in the case list of its `main()`.

Reaching a camp in a test. The file's `WalkToCamp` hard-codes the room order of dungeon 0 and cannot walk a theme's last dungeon, and `game::InstallDungeonRunAt(registry, seed, clearedIndex)` clamps the index to the checkpoint. Every new case of this WP (in all four test files) uses this recipe instead, as a file-local helper `CampAfterDungeon(registry, index)`:
1. `registry.ctx().get<game::FDungeonCheckpoint>().cleared = index;` then `game::InstallDungeonRunAt(registry, seed, index)` (any fixed seed).
2. Put the run at the reward of the dungeon's last room, the way `run_modal_tests::RewardToCampReplacesTop` does: `run.phase = game::EDungeonPhase::Reward; run.room = <index of the last room of that dungeon>` (from `FindRunSequence(registry)->DungeonAt(PositionOf(index)).rooms.size() - 1`), and close any open rune offer (`rewards.offer.active = false`).
3. `ApplyRunIntent(registry, game::ERunIntent::ContinueReward)` has a value and the phase is Camp.
`last` below is the dungeon count of theme 0 minus 1.
- **ThemeClearedCampDetected** (F1): `CampAfterDungeon(registry, last)` → `IsThemeClearedCamp` true. `CampAfterDungeon(registry, 0)` → false. Run in Battle phase → false. No run → false.
- **ReturnInTriumphEndsRunAsVictory** (F2): `CampAfterDungeon(registry, last)`; record `checkpoint.cleared` and `MetaSeals(registry)`. `ApplyRunIntent(registry, ERunIntent::ReturnInTriumph)` has a value with `exit == ERunExit::ShowModal` and `modalId == kRunModalRunEnd`; `run.phase == EDungeonPhase::Victory`; the save file no longer exists; `checkpoint.cleared` is unchanged; the last `run_end` event of the `FRunLog` has `result == "victory"` and `reason == "return"`; `run.message` contains the theme's name; when the boot has meta progress enabled, `MetaSeals` rose by `kTriumphSeals` (when it has none, it is unchanged at 0).
- **ReturnInTriumphRefusedElsewhere** (F3): at the camp of index 0 → no value, phase still Camp; in Battle → no value; at the theme-cleared camp (`CHECK(IsThemeClearedCamp(registry))` first) with `registry.ctx().get<game::FRewards>().offer.active = true` → no value with `error() == "Resolve the rune offer before leaving camp."`, phase still Camp, save still exists.
- **VictoryExitsDropTheRun** (F4): two boots brought to Victory as in F2. `ApplyRunIntent(RetryAfterDefeat)` → `exit == ERunExit::ToPrepare`, no `FDungeonRun` in the context. `ApplyRunIntent(LeaveRun)` → `exit == ERunExit::ToTitle`, no run in the context.
- **LeaveRunAtCampKeepsSave** (F5): `CampAfterDungeon(registry, 0)`; `ApplyRunIntent(LeaveRun)` → `exit == ERunExit::ToTitle`, no run in the context, the save file exists; `game::ContinueDungeonRun(registry)` succeeds and the restored run's phase is Camp. In Reward phase `LeaveRun` still has no value.
- **RunPlaceLabelFormat** (F6): with `seq = FindRunSequence(registry)`, `n0 = seq->themes[0].dungeons.size()`, `total = seq->TotalDungeons()`: `RunPlaceLabel(registry, 0) == seq->themes[0].name + " 1/" + std::to_string(n0)`; `RunPlaceLabel(registry, total - 1)` ends with `"/" + <dungeon count of the last theme>` and contains no `"Cycle"`; `RunPlaceLabel(registry, total) == seq->themes[0].name + " 1/" + std::to_string(n0) + "  |  Cycle 1"`.

`tests/features/run_modal_tests.cpp` (follow `RewardToCampReplacesTop` and `DefeatPushesRunEndOnce` for driving a modal host and the run-end state):
- **CampAndRunEndAcceptVictoryIntents** (F7): a camp host at a theme-cleared camp (`CampAfterDungeon(registry, last)`, then the camp host pushed as `RewardToCampReplacesTop` ends up) receives a posted `ReturnInTriumph` → its transition replaces the top with `ember.runEnd` and the phase is Victory. The run-end state then receives `RetryAfterDefeat` → transition to the preparation state, `FRunStartRequest{false}` in the context. A second scenario: a camp host receives `LeaveRun` → the transition is `{EStateChangeKind::Root, kEmberTitleId}`, no run in the context, and the save file still exists (checked as `ContinueLoadsThenRootsBattle` checks it).

`tests/features/run_debrief_tests.cpp`:
- **RunEndTextsFollowPhase** (F8): `RunEndHeading(Victory) == "EXPEDITION COMPLETE"`, `RunEndHeading(Defeat) == "EXPEDITION LOST"`, `RunEndLossSectionTitle(Victory) == "What ends with the run / what stays"`, `RunEndLossSectionTitle(Defeat) == "What you lost / what stays"`; every other phase gives the Defeat texts.

`tests/features/camp_tests.cpp` (helpers `FBoot`, `StartFresh`, `WalkToCamp(boot)`, `RenderModalLogs`; `CampViewShowsChoice` shows how a case captures the camp text):
- **CampViewShowsThemeVictory** (F9): boot A: `CampAfterDungeon(boot.registry, last)` → the captured text contains `"THEME CLEARED / "`, `"has fallen. Seals: "`, `"Descend deeper: "`, `"Return in triumph"` and `"The checkpoint stays. Seals +"`, and does not contain `"DUNGEON CLEARED / CAMP"`. Boot B: `CampAfterDungeon(boot.registry, 0)` → contains `"DUNGEON CLEARED / CAMP"`, `"Next: "`, `"Descend to the next dungeon"`, and does not contain `"Return in triumph"`.

`tests/features/meta_save_tests.cpp` (its meta-enabled fixture and `MetaSeals`):
- **TriumphGrantsSealsEveryTime** (F10): with meta enabled, `GrantTriumphSeals(registry)` returns `kTriumphSeals` and `MetaSeals` rises by it; a second call rises by it again; `LifetimeSealsEarned` is unchanged by both. On a registry without meta progress (or with it disabled, as the file's disabled-meta cases set it up) it returns 0 and changes nothing.

The save-and-leave button, the HUD status line and the legacy defeat view have no test of their own (F5 covers the intent behind the button, F6 the label); the diff review checks the wiring. Timeouts are unchanged. `features.json` exempts no file from the gate's CHECK-count rule.

## 8. Decisions
- Resolved: the victory screen is the camp modal in a victory variant, not a new modal — the camp is already where the player decides between going on and stopping, its services must stay available before descending, and a new modal would need a new phase in the save, a state-map entry and an autopilot branch for no extra information.
- Resolved: returning ends the run through a new Victory phase and the existing run-end screen — the screen already shows the ledger and offers "new expedition" and "title"; only its words assumed a defeat.
- Resolved: returning pays 1 seal, every time — ruling 111 names "run success" as a seal source of its own and the ruling of 2026-11-11 defines it as returning after a theme boss; without it returning would never be better than descending (a later wipe keeps the checkpoint too). The amount is a code constant like the other two seal amounts.
- Accepted: a failed save on the camp's "Save and leave" is only logged by the modal host (the battle path shows the reason); the camp is already saved on entry, so the player loses nothing.
- Accepted: the HUD status line grows by the place label without a width check; its layout is checked in the playtest.
- Resolved: "save and leave" reuses `LeaveRun` — it already saves, drops the run and returns to the title for battles; the camp is saved on entry and after every purchase, so the same intent is safe there.
- Resolved: `end_on_final_boss` stays a rejected flag — the ruling makes the ending a choice, so a data switch that forces the end has no consumer.
- Resolved: the autopilot is not changed — it descends at every camp, which keeps existing baselines comparable; a policy token for returning can be added when a simulation needs it.
- Alternatives considered: ending the run automatically at the last theme's boss — rejected by the user (확정 22 stays). A separate "Return" button at every camp — rejected: outside a theme end, stopping is "End expedition", which already exists.
- Defaults for anything unspecified: Victory behaves like Defeat wherever the code asks "is this run over".
- Out of scope: the rest of the seal economy, roster and unlock changes (WP-48), new bosses (WP-47), per-cycle difficulty and content, a success summary different from the defeat ledger, remembering triumphs in the records tab.

## 9. Pitfalls
- `ERunIntent` and `EDungeonPhase` are switched over in several files; add the new enumerators at the end and complete every switch (the build must stay warning-clean for those switches).
- The run-end state is entered while the run is still in the registry; `ReturnInTriumph` must not erase it (only Retry and Leave do).
- `LeaveCamp` refuses while a rune offer is open; `ReturnInTriumph` must refuse the same way, or the offer is lost.
- The camp belongs to the dungeon just cleared: its position is `PositionOf(run.depth - 1)`, and the next dungeon's is `PositionOf(run.depth)`.
- The tests run on the frozen fixture's `dungeon.json`; read theme names and dungeon counts from the loaded sequence instead of hard-coding the ship values.
- Player-facing strings are English only. ImGui math operators are disabled: build `ImVec2` component-wise.
- One build at a time per worktree; an MSB3073/MSB3075 DLL-copy failure is a transient lock: rerun once. A linker error about a corrupt `game01P_runtime.lib` after an interrupted build: delete that `.lib` under `out/build/windows-msvc-agent/Debug/` and rebuild.

## 10. Open questions
None.

# Verdict — gameplay-run (slice 3, dungeon run) (2026-09-17)

Range: `37daea8..432aeca` on `feature/gameplay-dungeon-run`.
Artifacts: `contract.md` (Pass 0, committed before implementing), `pass-1.md` (self), `pass-2.md` (structure,
independent), `pass-3-correctness.md` (correctness, independent), this file.

## Status

**GROWING** — the run cycle, the data-driven behaviour tree and the movement unification are in place, and every
finding from the three passes is either fixed or recorded as accepted with its rationale. Slice 4 (파밍 /
장비·룬) builds on a run state that already owns dungeon progression, so nothing here has to be re-cut.

## Summary of the review cycle

| Pass | Owner | Outcome |
| --- | --- | --- |
| Pass 0 | oracle (coordinator) | `contract.md` — scope, anti-goals, AC1~AC5, Implementation choices (던전 순환, 화면 구성, 클리어 수 유지). |
| Pass 1 | coordinator (self) | 1 Medium fixed during implementation (hub screen dropped the transition narration — `FBattleTextView.cpp:273`). |
| Pass 2 | `reviewer` `del_mu4b4alg_hnz8` | **REWORK** — 1 Blocker (build-breaking `distance` scope error the harness cannot see), 1 High (R9 deletion-test surface), 3 Medium (dead content, missing out-of-range narration, stale comment), 3 Low. All addressed. |
| Pass 3 | `reviewer` `del_mu4b5w1z_ku1d` | 1 High + 3 Medium + 2 Low, all fixed (`pass-3-correctness.md`). The run was cut off by the watchdog before its closing summary; each item was verified by the coordinator. |
| Oracle | `oracle` `del_mu4b4ex5_l0co` | All 8 questions **PASS**; final line **READY TO MERGE** at `79800c6` (pre-fix). Its Q5 gap list (threshold domain, spawn-cell bounds/overlap) is closed by `432aeca`. |

Two delegates died with an empty result on this range (`del_mu4b467u_6o09`, and an earlier oracle attempt): the
harness has no build step, so the only way to catch a compile error is a human-run build — which is why the
Blocker above is recorded as evidence that a green `verify-changeability.sh` is **not** compile evidence.

## Accepted / deferred (recorded so the next slice does not re-litigate them)

- **R9 deletion surface (High, structure pass):** removing the behaviour tree touches ~7 files beyond the added
  ones (`FBehaviorContent.*`, `FContentRegistry.*`, the `behavior` field in `FUnitContent.*`, `FBehaviorDecidedEvent`,
  the narrator switch, `units.json`). Accepted because design §2 itself fixes the `units.json behavior: [] →
  behaviors.json` mapping, the pure-rule/executor split and the "decided → did" narration. The revert checklist is
  the oracle's Q6 list in `pass-3-correctness.md`'s sibling report (recorded here): delete `behaviors.json` +
  `FBehaviorContent.*` + `rules/FBehaviorTree.h`; drop `kBehaviorSchema`, the behaviour load loop, its threshold and
  cross-reference checks; drop the `behavior` field and `kUnitFieldBehavior` (appended last, so indices stay
  stable); drop the three `behavior` arrays in `units.json`; cut the decision half of `FEnemyTurnSystem.h`
  (keep `useFirstSkill` / `moveRelativeToNearest` / `applyStep` usage) and the `FBehaviorDecidedEvent` render loop.
- **Unreferenced behaviour rows** (`proto_retreat`, `proto_wait_outnumbered`): kept as vocabulary-coverage sample
  data and now labelled as such in `assets/data/behaviors.json`'s `_comment`. Wiring a count-based rule into the
  two existing units was rejected on balance grounds (party = 2, so `opponent_count_at_or_above: 3` would make a
  monster a permanent bystander).
- **No seam for carried-over party state (oracle Q3):** 룸 시작 시 파티 온전 holds as a *consequence* of a fresh
  battle spawn, with no reset routine. If a later slice carries HP/resources across rooms, it needs a run-scoped
  party state plus a `spawnUnit`/`FRunFactory` change — a deliberate future cut, and design §1 defers it
  ("미결: 정비(캠프)·자원 유지 여부는 그 슬라이스에서 재검토").
- **Remaining AC5 gaps not closed** (recorded, not silent): empty-string ids/`display_name` (key presence is
  required, emptiness is not), and an unknown `sprite` id (the field is documented as unresolved — nothing reads
  it yet, so it cannot degrade a run).

## Owner-run residue (cannot be verified without a build)

Per `DEV_WORKFLOW.md` §"Builds Are Human-Run": AC1~AC4 (real hub → 3 rooms → clear → hub walk-through, defeat →
RunOver → hub, second-run dungeon cycling), the AC5 boot test (break an asset, confirm the boot fails with the
logged reason), and AC7 (the build itself). Static traces and harness evidence are in the pass documents.

## Teach-back (what the implementer can explain from memory)

- Why a room spawn is the party reset, and why `FRunFactory` (not the systems) owns destroy-then-spawn.
- Why `FRunProgressionSystem` is registered last: it is the only consumer of the battle's terminal *state*, and it
  spawns/destroys battles, so the other systems must never observe a half-built room.
- Why the movement refactor is a correctness fix, not a cleanup: one `applyStep` means the player and a monster
  answer AP/bounds/occupancy identically, and the AI's range fact and the resolver's range check share
  `withinSkillRange`.
- Why the behaviour tree is a pure function over a gathered `FBehaviorContext`, and therefore deterministic without
  consuming the run seed.
- Why `threshold` is a required schema field even for conditions that ignore it (projection defaults make
  "absent" indistinguishable from "authored 0").

## Ready to squash-merge to `main`

**Yes — after the owner-run build** (and the AC walk-through above). Nothing in the reviewed range is left with an
open Blocker/High finding; the only unresolved items are the accepted/deferred list and the owner-run residues.

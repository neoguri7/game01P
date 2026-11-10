# Follow-ups (logged by reviews, not yet scheduled)

Each line: source review · item · why it was deferred. A new WP picks items from here; remove a line when its WP merges.

## From the WP-35 diff review (2026-11-10)
- `wp-gate.mjs`: a gate rule `ctest-not-run`, plus the `-R` filter recorded in the evidence. Today a WP proven only by `node:` tests can pass the gate even though the C++ suite never ran at its HEAD. Deferred because it needs a spec change: the rule has to be added to the §3c rule list.
- `wp-gate.mjs`: a rename out of `tests/` (status R, old path under `tests/`, new path outside it) should count as `test-deleted`. Also tighten `gate.test.mjs` so its rename case actually exercises the old-path half. Deferred: needs a spec change.
- `wp-gate.mjs`: node proofs are currently matched by test name across all of a WP's node files. Key the match by file as well (JUnit `file`/classname), so a test in file B can't prove a feature declared for file A. Deferred: needs a spec change.
- `locks.mjs`: when Windows recycles a PID, a stale lock still looks alive until the 30-minute timeout. Low priority: it only costs time.
- `locks.mjs`: on win32, an EPERM/EACCES caused by a real permission problem (an unwritable lock dir) now waits out the full 30-minute timeout instead of failing fast. Bound the retries, or retry only while the lock file exists.
- Tests missing for two paths:
  - takeover leaving a fresh lock record alone
  - an untracked `tests/features/*.cpp` judged unproven by the gate

## Workflow (design session)
- Spec cost: rewriting the WP-36 spec, with exact replacement text for 21 comment lines, cost about 210k Opus tokens. Proposal: for deletion and cleanup WPs, let comments follow a rule ("delete sentences that name a deleted symbol") and require exact text only for code and tests.
- Review sizing: an L tooling or deletion WP ran 6 Opus gate reviews in round 1. Proposal: size the gate by risk as well as by file count.

## From the WP-36 diff review (2026-11-10)
- `out/build/windows-msvc-agent/Debug/assets/art/forest_{ground,oak,waystone}.png` stay staged because staging copies and never deletes. Clutter only: nothing loads them.
- Spec process: a word-check filter that drops every line containing a removed switch hid a stale README line. Filter on the exact new lines instead; exclude prescribed "formerly …" comments from symbol checks.
- `src/states/FBaseState.h:25` still says "(tests, prototype checks)"; PrototypeChecks no longer exists. Not in the WP-36 spec.

## From the WP-39 diff review (2026-11-11)
- `src/gameplay/dungeon/FBattleObjective.cpp` loader: `get<int>()` narrows int64 silently (`reward_materials: 4294967297` reads as 1). Read as `std::int64_t`, range-check, then narrow.
- `ApplyBattleStakes`: a bad tuning logs `LOG_ERROR` every battle, and both JSON tables are re-read at every battle start. Cosmetic.
- Simulation and optimizer runs that launch `game01P` without `GAME01P_STAKES=0` now include crits and objective materials; baselines from before WP-39 are not comparable. Note it in the sim config when the sim is next touched.
- `ApplyBattleStakes` lives in `FBattleObjective.cpp`; `gameplay/battle/FBattleSetup` would be a cleaner home (optional).
- Boss rooms pick from the same two objectives; `Swift` (win by round 5) may be unreachable there. Decide after the playtest log shows the boss-room met rate.
- Spec process: `features.json` `allowCheckDrop` must be a list (`[]`), not `false`; `wp-gate.mjs` crashes on a boolean. The template line in `.claude/skills/wp-spec/template.md` shows the key without its type.

## From the WP-40 diff review (2026-11-11)
- `reward_equip_tests::OfferModalDoesNotScroll` erases the growth panel before measuring; assert instead that Skip and Continue end inside the visible modal with the growth panel kept in.
- `FDungeonRewardView.cpp` `DrawRuneOfferCard`: Pick is drawn before the duplicate-refund line, so "Already owned" can flash for one frame after a pick. Read `IsRuneOwned` before drawing Pick.
- No test clicks a card shown in sorted order and checks the stored index that resolves (correct by inspection).
- `LoadRewards` rejects rarity weights above 1,000,000 (an overflow guard the spec did not ask for).
- Simulation baselines from before WP-40 are not comparable (offers are tiered now).
- Spec process: a fix that adds a feature must be added to `features.json` on main by the design session; an implementer edit trips the gate rule `spec-edit`.

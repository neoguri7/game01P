# game01P — agent instructions

Ember Dungeon: C++23 / SDL3 / ImGui / EnTT, 4-unit grid-tactics dungeon roguelike, data-driven JSON content in `assets/data/`.

## Design sources (bind every WP)
- `docs/design/decisions.md` — the user's rulings (확정/가정/미결). A 확정 ruling changes only with the user.
- `docs/design/combat-math.md` — modifier order and owners; new modifiers slot into it.
- Reference games: dungeon/expedition → Darkest Dungeon; builds → Path of Exile; UI/information → Pillars of Eternity.
- Fact questions are settled by research (`browse`); taste questions without a ruling go to the user as one batch of one-liners, each with a recommendation.

## Playtest loop
The user playtests personally. Notes go to `docs/playtest/YYYY-MM-DD.md` as raw lines in the user's words; agents triage them into WP hypotheses or `decisions.md` entries, never rewrite them. At most 3 player-facing WPs ship between two playtests. Numeric balance is tuned by simulation + optimizer (GitHub Actions), never by hand-editing in an LLM loop.

## Token rule (first criterion for every choice)
Spend tokens where they buy quality. Opus designs and reviews; Sonnet 5.5 implements; Haiku 5.5 reads.
- Every heavy read — searches, fetched pages, papers, long docs — goes through the `browse` agent (Haiku) first, in design and after implementation alike. You read its compressed findings and re-open only the cited spans a decision rests on.
- Build and test output is filtered by script (`.claude/scripts/build.mjs` prints only errors and failing tests); compiler errors are never summarized by an LLM.
- Keep your own context small: targeted reads, summaries back from subagents (≤1,500 tokens), a fresh session per work package.

## Work package (WP) lifecycle
1. **Design** (Opus, main session): `/wp-spec <WP-id> <goal>` → `docs/wp/<id>/spec.md` + `features.json`, gated by `spec-critic` plus EGM goldfish reviewers sized to the WP; the spec is committed to main before implementation.
2. **Implement** (Sonnet 5.5): the `implementer` agent, launched in the background, working in the main checkout on branch `wp/<id>` with the one existing build directory. WPs run one at a time; while an implementer runs, the design session reads only (no branch switches, no edits to tracked files). `BLOCKED:` questions come back to the design session.
3. **Gate + review**: `.claude/scripts/wp-gate.mjs` checks scope, test integrity and evidence deterministically; then the `diff-reviewer` agent (Opus) reviews the diff against the spec. Must-fix items go back to the same implementer (SendMessage).
4. **Merge**: `node .claude/scripts/wp-merge.mjs <id> --reviewed <sha>` — squash-merges `wp/<id>` into main with the spec H1 as the commit message (e.g. `WP-35: Agent build tooling …`), re-runs build + ctest on main, deletes the branch. Push only when the user asks. (Until WP-38 lands, `wp-merge.mjs` still expects a separate worktree; merge single-checkout WPs by hand with the same steps.)
Delegated work runs as local background agents launched from the main session. One checkout, one build directory: isolation comes from git branches (mistakes are reverted with git), never from extra worktrees, clones or fresh build folders. A new executable target is added to CMake only when a WP truly needs one. No Claude cloud sessions and no remote agent isolation unless the user asks; GitHub Actions is used only for long simulations.

## Build and test (Windows, PowerShell 7)
`node .claude/scripts/build.mjs [--preset windows-msvc-agent]` — configure if needed, build, ctest; one at a time; writes `out/evidence.json`. Raw commands for reference: `cmake --preset <p>` · `cmake --build --preset <p> --parallel` · `ctest --preset <p> --output-on-failure`. Long commands need an explicit timeout (≥600000 ms).
Gotchas the config won't tell you:
- One build/ctest at a time per worktree (shared PDBs and asset staging collide). An MSB3073/MSB3075 DLL-copy failure is a transient lock: rerun once.
- Deleting a `tests/features/*_tests.cpp` file needs a reconfigure, or the build fails with C1083.
- Game runs from agents stay windowless (SDL dummy video driver) with `GAME01P_SAVE_DIR` under `out/`; the user's real save in `%APPDATA%\Game01P` stays untouched. Reproducible runs: `GAME01P_SEED`, `GAME01P_AUTOPLAY=1`, `GAME01P_AUTOPILOT`.
- Player-facing strings are English: the HUD font has no Hangul.
- `python3` is a Windows Store stub: use `py` or `node`.
- ImGui math operators are disabled (C2676): build `ImVec2` component-wise.

# game01P — agent instructions

Ember Dungeon: C++23 / SDL3 / ImGui / EnTT, 4-unit grid-tactics dungeon roguelike, data-driven JSON content in `assets/data/`.

## Token rule (first criterion for every choice)
Spend tokens where they buy quality. Opus designs and reviews; Sonnet 5.5 implements; Haiku 5.5 reads.
- Every heavy read — searches, fetched pages, papers, long docs, big logs, test output — goes through the `browse` agent (Haiku) first, in design and after implementation alike. You read its compressed findings and re-open only the cited spans a decision rests on. Structured output (build logs, ctest) is grepped by script before anything reads it.
- Keep your own context small: targeted reads, summaries back from subagents (≤1,500 tokens), a fresh session per work package.
- Deterministic work (build, test, search, metrics, balance tuning) goes to scripts, never to an LLM loop.

## Work package (WP) lifecycle
1. **Design** (Opus, main session): `/wp-spec <WP-id> <goal>` → `docs/wp/<id>/spec.md` + `features.json`, gated by `spec-critic` plus EGM goldfish reviewers sized to the WP.
2. **Implement** (Sonnet 5.5): the `implementer` agent, launched in the background, in its own worktree. Several WPs with disjoint file maps run in parallel. `BLOCKED:` questions come back to the design session.
3. **Review** (Opus): `diff-reviewer` agent on the WP branch, diff + spec only. Must-fix items go back to the same implementer (SendMessage).
Delegated work runs as background agents launched from the main session; terminal-launched sessions are not part of the flow.
4. **Merge**: one commit per WP (`WP-<id>: <imperative>`), branch `wp/<id>`; merge only on reviewer PASS. Push only when the user asks.
The user playtests personally; numeric balance is tuned by simulation + optimizer, not by hand-editing in an LLM loop.

## Build and test (Windows, PowerShell 7)
`cmake --preset windows-msvc-debug` · `cmake --build --preset windows-msvc-debug --parallel` · `ctest --preset windows-msvc-debug --output-on-failure`
Gotchas the config won't tell you:
- One build/ctest at a time per worktree (shared PDBs and asset staging collide). An MSB3073/MSB3075 DLL-copy failure is a transient lock: rerun once.
- Deleting a `tests/features/*_tests.cpp` file needs a reconfigure, or the build fails with C1083.
- Game runs from agents stay windowless (`SDL_VIDEO_DRIVER=dummy`) with `GAME01P_SAVE_DIR` under `out/`; the user's real save in `%APPDATA%\Game01P` stays untouched. Reproducible runs: `GAME01P_SEED`, `GAME01P_AUTOPLAY=1`, `GAME01P_AUTOPILOT`.
- Player-facing strings are English: the HUD font has no Hangul.
- `python3` is a Windows Store stub: use `py` or `node`.
- ImGui math operators are disabled (C2676): build `ImVec2` component-wise.

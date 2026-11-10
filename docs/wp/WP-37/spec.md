# WP-37: Background-friendly builds — low priority, capped parallelism

Size: S · Player-facing: no · Certainty: settled · Base commit: (set at handoff) · Branch: wp/WP-37
`<id>` = `WP-37`. §2 (player experience) and §4 (numbers) do not apply.

## 1. Goal [S]
Agent builds saturate all 16 cores at normal priority, which makes the user's other apps (games, calls) stutter and their ping spike. `build.mjs` builds now run below normal priority with two cores left free, and without reusing normal-priority MSBuild nodes.
- Decisions it rests on: user request 2026-11-10 ("작업할 때 네트워크/우선순위에서 뒤로").

## 3. Behaviors [S]
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN `runBuild` starts with `priority` = `'below-normal'` (the default) THEN it SHALL call `setPriority(0, os.constants.priority.PRIORITY_BELOW_NORMAL)` once before running any command; WHEN `priority` = `'normal'` THEN it SHALL not call `setPriority`; a throwing `setPriority` SHALL be ignored | `node:.claude/scripts/test/build.test.mjs::LowPriorityAndCappedParallelism` |
| F2 | WHEN `runBuild` builds with `cpuCount` = N THEN every `--build` call (including the MSB3073/3075 retry) SHALL use args `['--build', '--preset', <preset>, '--parallel', String(Math.max(1, N - 2))]` | same test |
| F3 | WHEN `runBuild` calls `run` for `cmake` or `ctest` THEN it SHALL pass a third argument `{ env: { MSBUILDDISABLENODEREUSE: '1', VCPKG_MAX_CONCURRENCY: String(Math.max(1, Math.floor(N / 4))) } }`; `git` calls get no third argument | same test |
| F4 | WHEN `parseArgs(['--normal-priority'])` runs THEN it SHALL return `priority: 'normal'`; WHEN a `run` made by `makeRun(cwd)` is called with a third argument `{env}` THEN the child SHALL see `process.env` merged with `env` | `node:.claude/scripts/test/build.test.mjs::CliPriorityFlagAndEnv` |

## 5. File map [S]
- Modify `.claude/scripts/lib/runner.mjs` — `runBuild` gains parameters `priority = 'below-normal'`, `setPriority = os.setPriority`, `cpuCount = os.availableParallelism()`; F1 at the top of the function body (before taking `build.lock`), wrapped in try/catch; F2 in `buildArgs` (shared by the build call and its retry); F3 on the three `cmake` calls (configure, build, retry) and the `ctest` call. Import `os` from `node:os`.
- Create `.claude/scripts/lib/cli.mjs` — `export function parseArgs(argv) → {noTest, preset?, tests?, priority?}` (no `cwd`; flags as today plus `--normal-priority` → `priority: 'normal'`; an unknown argument throws `Error('unknown argument: <arg>')`), and `export function makeRun(cwd) → (cmd, args, opts = {}) => Promise<{code, output}>` spawning with `{ cwd, windowsHide: true, env: opts.env ? { ...process.env, ...opts.env } : process.env }` (same stdout/stderr/error/close handling as today's `run`).
- Modify `.claude/scripts/build.mjs` — import `parseArgs`/`makeRun` from `./lib/cli.mjs`; catch the parse error, print it and exit 64; call `runBuild({ ...opts, cwd, run: makeRun(cwd) })`; output and exit codes unchanged.
- Modify `.claude/scripts/test/build.test.mjs` — add the two tests.
- Do not touch: everything else (including `wp-gate.mjs`, `wp-merge.mjs`, CMake files, `.claude/` docs).

## 6. Tasks [S]
0. **T0 Red** — add both tests; `CliPriorityFlagAndEnv` loads the module with `await import('../lib/cli.mjs')` inside the test body. Done: `node --test ".claude/scripts/test/build.test.mjs"` fails only in the two new tests; commit `WP-37: add failing tests`.
1. **T1 runner** — F1–F3. Done: `node --test --test-name-pattern=LowPriorityAndCappedParallelism ".claude/scripts/test/build.test.mjs"` exits 0.
2. **T2 CLI** — `cli.mjs`, `build.mjs`. Done: `node --test ".claude/scripts/test/*.test.mjs"` all green; `node .claude/scripts/build.mjs` exits 0 (tool timeout ≥600000 ms) and `out/evidence.json` is fresh at HEAD.

## 7. Tests
- `LowPriorityAndCappedParallelism`: temp `cwd` with `CMakeCache.txt` and a matching `feature-set.json` already present (no configure), temp `lockRoot`, fake `run` recording `(cmd, args, opts)` and returning `{code: 0, output: ''}` (writing a ctest JUnit file to the evidence path as the existing tests do), fake `setPriority` recording calls, `cpuCount: 16`. Assert:
  - `setPriority` called once with `(0, os.constants.priority.PRIORITY_BELOW_NORMAL)` before the first `run` call;
  - the build args end with `'--parallel', '14'`; every cmake/ctest call has `opts.env` deep-equal to `{MSBUILDDISABLENODEREUSE: '1', VCPKG_MAX_CONCURRENCY: '4'}`; git calls have `opts` undefined;
  - a run whose first `--build` returns `{code: 1, output: 'error MSB3073: x'}`: both `--build` calls end with `'--parallel', '14'` and carry the env opts;
  - `priority: 'normal'` → `setPriority` not called; a throwing `setPriority` → `exitCode` 0;
  - `cpuCount: 2` → `'--parallel', '1'` and `VCPKG_MAX_CONCURRENCY: '1'`.
- `CliPriorityFlagAndEnv`: `parseArgs(['--normal-priority']).priority === 'normal'`; `parseArgs([])` has no `priority` key; `parseArgs(['--bogus'])` throws; set `process.env.WP37_PARENT = 'p'`, then `makeRun(tmp)(process.execPath, ['-e', 'process.stdout.write(process.env.WP37_PROBE + process.env.WP37_PARENT)'], {env: {WP37_PROBE: 'x'}})` resolves with output `xp`.
- Existing tests keep passing unchanged.

## 8. Decisions
- Below-normal is inherited: on Windows a child of a BELOW_NORMAL process inherits that class, so cmake → MSBuild → cl.exe all run below normal without touching each process.
- `MSBUILDDISABLENODEREUSE=1`: reused MSBuild nodes from earlier normal-priority builds would otherwise keep normal priority.
- Two cores stay free (N − 2); vcpkg gets N/4 jobs because its downloads and builds are the bursty part.
- Existing tests that call `runBuild` without `setPriority` lower the `node --test` process itself to below normal; accepted (harmless).
- Network: Windows QoS can only shape outbound traffic, and our downloads are one-time since WP-35's shared vcpkg dir, so no network throttling here (a router QoS/SQM setting is the user's option).
- Out of scope: wp-merge's build command (it calls build.mjs and inherits this behaviour).

## 9. Pitfalls
- Pass ≥600000 ms for the real build; it may still be moved to the background after ~180 s — wait for it, never start a second build.
- `os.availableParallelism()` exists on Node ≥18.14; the installed Node is newer.

## 10. Open questions
None.

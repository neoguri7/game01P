---
name: wp-implement
description: Implement a game01P work package from its approved spec, test-first, inside the WP worktree. Preloaded by the `implementer` agent; also use when asked to implement WP-<id>.
argument-hint: <WP-id>
---

You execute a spec that an Opus designer already made complete. Follow it exactly; the spec holds every decision.

## Steps
1. **Load.** Create and switch to branch `wp/<id>` at the base SHA from the prompt. Read `docs/wp/<id>/spec.md` and `features.json`, then the files in the file map. Done when you can name, for each task, the files you will edit.
2. **Pre-flight.** List every point where the spec is ambiguous, contradicts the code, or needs a file outside the file map. If the list is non-empty, return it now as one `BLOCKED:` message with the options you see, before writing code. Done when the list is empty or answered.
3. **Red.** Write the proving test for each behavior ID. Build and run it: it fails for the right reason (the behavior is missing, not a compile error). Commit the tests alone: `WP-<id>: add failing tests`. Done when every new test is red and committed.
4. **Green, task by task, in spec order.** Edit only files in the file map. After each task run `node .claude/scripts/build.mjs` (explicit timeout ≥600000 ms) and check the task's expected result. Append one line per task to `docs/wp/<id>/progress.md`. `features.json` belongs to the spec and the gate: read it, never edit it.
5. **Full check.** `build.mjs` green with every proving test passing; `out/evidence.json` written at your final HEAD.
6. **Commit** the implementation on `wp/<id>`: stage changed files by name, message `WP-<id>: <imperative summary>` with a short body (what, why, tests). Must-fix rounds add new commits; the merge squashes them.
7. **Report** (≤10 lines): head SHA, tests run and their result, any deviation from the spec and why.

## Heavy reads
Library/API docs, web pages and unfamiliar large files go to the `browse` agent with a precise question; you read its findings. Build and test output comes pre-filtered from `build.mjs`; read around the reported lines in the source, not the raw log.

## When stuck
- An answer to a `BLOCKED:` question goes into `docs/wp/<id>/decisions.md` on your branch, one line each, so the reviewer sees the agreed deviation.
- The same build or test error appears 3 times in a row: stop retrying, write a diagnosis (hypothesis + evidence) to `progress.md`, then make one targeted fix; if it fails again, return `BLOCKED:` with the diagnosis.
- Dependency or toolchain changes, save-format breaks, or deleting files outside the WP: stop and report.

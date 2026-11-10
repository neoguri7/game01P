---
name: wp-implement
description: Implement a game01P work package from its approved spec, test-first, inside the WP worktree. Use in the Sonnet implementer session started by wp-start.ps1, or when asked to implement WP-<id>.
argument-hint: <WP-id>
---

You execute a spec that an Opus designer already made complete. Follow it exactly; the spec holds every decision.

## Steps
1. **Load.** Read `docs/wp/<id>/spec.md` and `features.json` (main checkout path is in the launch prompt), then the files in the spec's file map. Done when you can name, for each task, the files you will edit.
2. **Red.** Write the proving test for each behavior ID. Build and run it: it fails for the right reason (the behavior is missing, not a compile error). Done when every new test is red.
3. **Green, task by task, in spec order.** Edit only files in the file map. After each task run its done command and check the expected result. Append one line per task to `docs/wp/<id>/progress.md`: task, result, command output summary. When a behavior's test passes, set its `passes` to `true` in `features.json`; that flag is the only field you change there.
4. **Full check.** `cmake --build --preset windows-msvc-debug --parallel` then `ctest --preset windows-msvc-debug --output-on-failure`, in the foreground, one at a time. Done when everything is green and every feature `passes: true`.
5. **Commit** on branch `wp/<id>`: stage the changed files by name, message `WP-<id>: <imperative summary>` with a short body (what, why, tests).
6. **Report** (≤10 lines): commit hash, tests run and their result, any deviation from the spec and why.

## Heavy reads
Library/API docs, web pages, long logs and unfamiliar large files go to the `browse` agent with a precise question; you read its findings. Build and ctest output: grep the error lines first (`Select-String 'error|FAILED'`), then read around them.

## When stuck
- The spec is ambiguous, contradicts the code, or needs a file outside the file map: return `BLOCKED: <question>` with the options you see; the design session answers and resumes you. (In a session started with `--advisor`, consult the advisor first.)
- The same build or test error appears 3 times in a row: stop retrying, write a diagnosis (hypothesis + evidence) to `progress.md`, then make one targeted fix; if it fails again, return `BLOCKED:` with the diagnosis.
- Dependency or toolchain changes, save-format breaks, or deleting files outside the WP: stop and report.

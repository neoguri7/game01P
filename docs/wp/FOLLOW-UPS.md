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

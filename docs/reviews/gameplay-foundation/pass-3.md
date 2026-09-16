# Pillar 3 — Optimization (slice 1: content data layer)

**N/A — no optimization claim, no profile.** This slice adds a startup-time
loader; it is not a measured hot path (`R13` N-A, debt registered in `pass-2.md`).

## Evidence

| What | How measured | Before | After |
|------|--------------|--------|-------|
| — | no profile/microbench run | — | — |

`UNVERIFIED` items: none claimed, so nothing is asserted.

## When this becomes mandatory

The first time slice 1's data is consumed per-frame in a measured hot path, run
one of the referenced profiles (Tracy / `ZoneScopedN` capture / microbench) and
fill the table above. Until then, `pass-3.md` stays N/A and any perf claim without
a number here is not accepted as evidence.

Non-blocking design notes already taken to avoid future debt:

- the loader parses each table once at startup and hands out
  `std::shared_ptr<const TContent>` immutably (`R15`), so per-frame consumers
  cannot pay a re-parse cost by accident;
- lookups are `unordered_map` keyed by row id (`R16`, read-only in the frame
  loop), not linear scans over the row vector.

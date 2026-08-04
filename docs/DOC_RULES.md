<!-- doc-verify subsystem=rules commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
# Documentation Rules (DOC_RULES)

This file is the **contract** that every other documentation file in this repository obeys. It is the single source of truth for how docs are written, anchored, kept fresh, and enforced. Before writing or editing any `docs/` file, read this file.

Rules use RFC 2119 keywords (MUST / MUST NOT / SHOULD / MAY).

---

## R1 — One source of truth per subsystem

- Each `src/` subsystem has **exactly one primary doc** under `docs/subsystems/`.
- Cross-referencing is done with relative Markdown links, **never** by copying a fact into a second doc.
- If a fact belongs to subsystem A but is needed by doc B, B links to A's doc.

## R2 — As-built vs Intended split

Every subsystem doc MUST contain both sections:

- `## As-built` — only what exists and compiles in the **current working tree**, with every claim `file:line`-anchored.
- `## Intended / In-progress` — recovered or planned architecture. Every claim here MUST be prefixed `[UNVERIFIED]` and MUST cite its recovery source (`git HEAD:<path>` or `out/build/<preset>/...`).

The two are NEVER blended. A claim is either as-built (verified in the tree) or intended (`[UNVERIFIED]`).

## R3 — Verified anchors

- Every factual claim about code in an As-built section carries an inline anchor `path:line` or `path:line-line`, relative to the repo root (e.g. `src/core/Engine.cpp:113-129`).
- A claim with no anchor and no `[UNVERIFIED]` tag is a rule violation.
- The enforcement script (`scripts/check-docs.sh`) resolves every on-disk anchor and fails on an anchor whose path does not exist or whose line is beyond the file's length.

## R4 — Freshness header

- **Line 1** of every `docs/` file is an HTML comment verify block, verbatim:

```
<!-- doc-verify subsystem=<id> commit=<40-char-sha> date=YYYY-MM-DD -->
```

- `<sha>` is the tree commit the doc was last verified against.
- A doc is **stale** if `git merge-base --is-ancestor <sha> HEAD` fails (round-trip), **OR** if HEAD is more than **20 commits** ahead of `<sha>`.
- Update the header (to the new HEAD sha and today's date) in the same change that updates the doc's content.

## R5 — No fabrication

- If a fact cannot be grounded in the current tree or in git history, mark it `[UNVERIFIED]` or omit it.
- NEVER invent a signature, name, path, or behavior.

## R6 — Mirror src structure

- `docs/subsystems/<name>.md` maps 1:1 to a `src/` subsystem boundary.
- No single doc spans two subsystems.

## R7 — English only

- All new docs are written in English.
- The existing (partly Korean) `README.md` is left as-is except for a single added pointer line to `docs/`.

## R8 — Executable onboarding

- Every shell command in `docs/ONBOARDING.md` and `docs/BUILD_AND_TOOLING.md` MUST be runnable verbatim.
- Because the build is currently broken, `ONBOARDING.md` states this up front and points to `docs/RECOVERY.md` before the first `cmake --build`.

## R9 — Decision-complete per doc

Each subsystem doc answers, for its subsystem:

1. What it is.
2. Where it lives (anchors).
3. How it is wired into the engine.
4. What the public API surface is.
5. How a new contributor extends it (the one system / event / component / factory to add).

A reader needs no other doc to start coding in that subsystem.

## R10 — Drift contract

- Editing code under a `src/` subsystem requires updating that subsystem's doc — including its `doc-verify` commit — in the same change.
- Enforced by the R3/R4 checks run in CI.

---

## Doc template

Every `docs/subsystems/*.md` and top-level `docs/*.md` MUST follow this skeleton:

```
<!-- doc-verify subsystem=<id> commit=<sha> date=YYYY-MM-DD -->
# <Subsystem Name>

> One-sentence purpose.

## As-built
- <fact> — `src/...:line`
- ...

## Intended / In-progress
- [UNVERIFIED — git HEAD:<path>] <fact>

## Public API surface
| Symbol | Signature | Anchor |

## How to extend
1. <concrete step>

## Cross-references
- [Related doc](<relative path>)
```

### Example verify header + anchor

```
<!-- doc-verify subsystem=core-engine commit=b048ebd7fc8c56f474111ec70b2261ccc2222a79 date=2026-08-03 -->
```

The entry system struct is `SSpriteRender` (`src/ecs/systems/SpriteRenderSystem.h:18`) and the main loop runs in `Engine::run()` (`src/core/Engine.cpp:113-129`).

---

## Using these rules

- **Writers (human or AI agent):** write every doc against this template, anchor every As-built claim, tag every intended claim `[UNVERIFIED]`, and set the verify header.
- **Reviewers:** check `scripts/check-docs.sh` is green and spot-check R2 (no untagged intended claim) and R5 (no invented facts).
- **CI:** `.github/workflows/docs-check.yml` enforces R3/R4 automatically on every push and pull request.

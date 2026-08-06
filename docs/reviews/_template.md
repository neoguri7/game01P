# Review Record Template

Copy this layout into `docs/reviews/<slug>/` when starting a slice. Commit
`contract.md` before implementing (Pass 0), then add per-pass findings and the
final verdict. See `DEV_WORKFLOW.md` → Review-Cycle Branch Flow.

## Layout

```text
docs/reviews/<slug>/
├── contract.md   # Pass 0 oracle — commit first
├── pass-1.md     # Pillar 1: Correctness & Exceptions findings
├── pass-2.md     # Pillar 2: Structure findings + architecture table
├── pass-3.md     # Pillar 3: Optimization findings (or N/A)
└── verdict.md    # final GROWING/REWORK/RESET + teach-back result
```

## contract.md

```markdown
# Contract — <slug>

## What changes
- <interface/behavior changed>

## What must NOT change (oracle)
- existing public behavior
- dependency direction `core/ <- ecs/ <- gameplay/`
- no gameplay in `core/`; `Engine`-prefixed GAS vocabulary only

## Non-goals (out of scope)
- <...>
```

## pass-1.md

```markdown
# Pillar 1 — Correctness & Exceptions (<date>)

- [Severity] `file:line` — what, why, guided question
- ...
```

## pass-2.md

```markdown
# Pillar 2 — Structure (<date>)

- [Severity] `file:line` — what, why, guided question
- Architecture audit table (statuses + `file:line`)
```

## pass-3.md

```markdown
# Pillar 3 — Optimization (<date>)

- [Severity] `file:line` — or `N/A` (no measured hot path)
```

## verdict.md

```markdown
# Verdict — <slug> (<date>)

- Status: GROWING / REWORK / RESET
- Teach-back: <what the implementer could and could not explain from memory>
- Ready to squash-merge to main: yes/no
```
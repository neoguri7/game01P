# game01P Harness (pi-coding-agent)

A minimal Plan → Code → Verify harness for `pi-coding-agent`, designed
for solo C++ game development with the "minimal yet strong" philosophy.

## What's in here

```
.pi/
├── APPEND_SYSTEM.md                    # Process constitution (appended to pi's default system prompt)
├── prompts/
│   ├── plan-kick.md                    # /plan-kick <task>  — start a new task
│   ├── plan.md                         # /plan              — re-enter Plan phase
│   ├── verify.md                       # /verify            — run Verify phase
│   └── rescope.md                      # /rescope           — split an oversized checklist item
└── extensions/
    ├── observation-masking.ts          # Trims old tool outputs to cut trajectory cost (~50%)
    └── phase-guard.ts                  # Enforces phase-based tool restrictions via PLAN.md

CODEMAP.md                              # Project structure doc (fill in once, read-on-bootstrap)
PLAN.md                                 # Current task + checklist + phase (hot-updated each turn)
```

## Install

1. Drop this whole directory into your `game01P/` project root so you end up with
   `game01P/.pi/`, `game01P/CODEMAP.md`, `game01P/PLAN.md`.
2. Fill in `CODEMAP.md` with the real directory tree, build commands, and main symbols.
   This file is read once per task on bootstrap — invest in it.
3. Leave `PLAN.md` as-is for now. The first task will fill it in.
4. Run `pi` from the project root. You should see a footer `phase: plan` and
   the agent starts by reading AGENTS.md → CODEMAP.md → PLAN.md.

## Usage pattern

```
# Start a new task
/plan-kick "Implement MoveComponent and the movement system"

# Agent writes PLAN.md, then stops. Review the checklist.
# If an item is too big:
/rescope

# Let the agent proceed with Code phase (it flips phase automatically
# via PLAN.md frontmatter; phase-guard then re-scopes tools at turn_end):
continue

# When it announces "Done: <item>", run:
/verify

# On failure, /plan automatically re-enters planning from the Last Verify state:
/plan
```

## Manual phase override

If the model forgets to flip the frontmatter (it happens):
```
/phase plan
/phase code
/phase verify
```
This updates PLAN.md's frontmatter and resets `setActiveTools` immediately.

## Tuning observation-masking

Edit `.pi/extensions/observation-masking.ts`:
- `KEEP_RECENT_RESULTS` (default 4): how many recent tool outputs stay in full
- `MAX_OLDER_RESULT_BYTES` (default 500): byte threshold above which older
  outputs get trimmed

Raise these if you hit cases where the model loses important context.
Lower them in very long sessions.

## Caveats (be aware)

1. **Message-shape assumption.** `observation-masking.ts` assumes `toolResult`
   messages have `{ role: "toolResult", toolName, content }`. If pi's internal
   message shape differs from this and masking doesn't fire, inspect
   `event.messages[i]` in the `context` event handler and adjust the field
   names. Logging a single message is a one-line addition.

2. **Prompt template variable syntax.** `plan-kick.md` uses `{{task}}` per the
   pi docs. If the expansion produces the literal `{{task}}` in your prompt,
   check `pi-mono/packages/coding-agent/docs/prompt-templates.md` for the
   correct placeholder name and swap it in.

3. **`setActiveTools` scope.** `phase-guard.ts` restricts *built-in* tools
   (`read`, `edit`, `write`, `bash`, `grep`, `find`, `ls`). If you install
   extension tools later that should also be phase-restricted, add them to
   `PHASE_TOOLS` in that file.

4. **APPEND_SYSTEM.md size.** Currently ~560 tokens. Below the 3,000-token
   reasoning-degradation threshold (Levy et al., 2024), but above the
   150–300-word sweet spot. If you see reasoning get sloppy, trim the less
   critical invariants first.

## What's NOT included (for a later round)

- **`compaction-policy.ts`** — custom session compaction that always preserves
  PLAN.md + CODEMAP.md state. Useful only once sessions regularly hit the
  context limit. pi's default compaction + the `PLAN.md` SSOT principle
  already covers most cases.
- **`structural-review` skill** — deeper structural metrics (coupling,
  cohesion, grep-based memory layout checks) to replace the inline Verify
  step 4 when you want something more rigorous than three yes/no lines.
- **Tool-response concise mode** — built-in `read` already truncates to 50KB.
  A further "concise mode" wrapper would cut tool-response tokens by another
  ~1/3 per Anthropic's tool guidance, but it's a bigger refactor.

Ping me (동우's pair) when the session length or the audit depth starts to
hurt, and we'll add them.

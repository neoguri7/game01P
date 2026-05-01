---
description: Kick off a new task in Plan phase
argument-hint: <task description>
---
Task: {{task}}

Enter Plan phase.

1. Read CODEMAP.md to orient. Do NOT read project files broadly.
2. If you need to locate a symbol or behavior, use grep/find first. Full file reads only as a last resort.
3. Produce a minimal ordered checklist and write it to PLAN.md. Each checklist item must:
   - Touch ≤ 1 file OR ≤ 1 test target
   - Be independently verifiable (build + test passes on its own)
   - State the expected verification in one line
4. Update `## Current Task` in PLAN.md to one sentence describing this task.
5. Keep `phase: plan` in frontmatter until a user runs `/code` or you explicitly enter Code phase.
6. Stop after writing PLAN.md. Do not proceed to Code this turn.

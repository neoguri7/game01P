---
description: Re-enter the Plan phase
---
Enter Plan phase.

1. Read PLAN.md. Check `## Last Verify` for any failures.
2. If the last verify failed:
   - Identify which step (Build/Typecheck/Tests/Structural) failed.
   - Insert corrective checklist items at the top of `## Checklist` to fix it.
   - Move failed items back to unchecked state if needed.
3. Pull any `## Discovered` items into `## Checklist` if they are now relevant; otherwise leave them.
4. Confirm the top unchecked item is still ≤ 1 file / ≤ 1 test. If not, split it (see /rescope).
5. Flip `phase:` frontmatter to `plan`. Stop. Wait for the user before entering Code.

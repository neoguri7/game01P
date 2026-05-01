---
description: Split the current plan item into smaller subitems
---
Look at PLAN.md's `## Checklist`. Pick the current top unchecked item.

Split it into up to 3 smaller, independently-verifiable subitems.

Each subitem MUST:
- Touch ≤ 1 file OR ≤ 1 test target
- Be individually buildable and testable
- State its verification in one line

Replace the original item in `## Checklist` with the subitems. Keep the rest of the file untouched. Do not change `phase:`.

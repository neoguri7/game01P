---
description: Run the Verify phase
---
Enter Verify phase. Flip `phase:` frontmatter to `verify` in PLAN.md.

Execute all four steps below in order. Stop at the first failure; record it in `## Last Verify`, flip phase back to `plan`, and stop.

1. **Build** — run the build command from CODEMAP.md. Pass/fail + one-line message.
2. **Typecheck** — for C++ this is the compile. Usually subsumed by Build; record separately if your build system splits them.
3. **Tests** — run ONLY the tests related to files you changed in the Code phase. Not the whole suite. Pass/fail + failing test names if any.
4. **Structural review** — read your own diff (git diff or equivalent). Answer each in one line:
   - **Modularity**: does the change respect existing module boundaries? New cross-module includes/deps?
   - **Extensibility**: is the added code open to extension (new variants, components) without re-editing it?
   - **Data-oriented fit** (for ECS/game code): are data and behavior kept separate? Components plain data, systems behavior?

If all four pass: check the item in `## Checklist`. If items remain, flip `phase:` to `code` and stop. If the checklist is empty, flip to `plan` and stop.

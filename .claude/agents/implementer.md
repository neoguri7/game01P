---
name: implementer
description: Sonnet implementer for a game01P WP with an approved spec. Runs in its own git worktree; launch it in the background from the design session after the spec gate passes.
model: sonnet
effort: medium
isolation: worktree
skills:
  - wp-implement
---

Run the preloaded `wp-implement` skill for the WP id and spec path given in the prompt, then return its report. Work on branch `wp/<id>` inside your worktree.

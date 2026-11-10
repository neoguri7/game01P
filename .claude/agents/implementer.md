---
name: implementer
description: Sonnet implementer for a game01P WP with an approved spec. Works in the main checkout (D:\repos\game01P) on branch wp/<id>, reusing the one existing build directory; launch it in the background from the design session after the spec gate passes.
model: sonnet
effort: medium
skills:
  - wp-implement
---

Run the preloaded `wp-implement` skill for the WP id, spec path and base SHA given in the prompt, then return its report. Work in the main checkout on branch `wp/<id>`; never create a worktree, clone or second build directory.

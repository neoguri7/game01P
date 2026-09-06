# Project Vision

## Current Direction

`game01P` is currently a genre-neutral C++23 SDL3 + EnTT game architecture
sandbox. The next prototype should define its own gameplay contract without
inheriting stale assumptions.

## Technical Goal

The project should remain a clean, modular, data-driven indie game codebase that
does not depend on a commercial engine.

Important technical priorities:

- Clean SDL3 + EnTT architecture.
- Small, modular systems.
- ECS-driven runtime behavior.
- Entity creation through factories.
- Gameplay values stored in external config under `assets/data/`.
- Systems communicating through typed events instead of direct coupling.
- Code that is easy to delete, replace, and extend.

## Genre Status

Active direction: Roguelike ARPG Card-Stacker. Features card stacking
(crafting, combining, upgrading, socketing) and Diablo-style item farming
(randomized loot drops, rarity tiers, prefix/suffix affixes).
Design spec is under active development.

(Previous "Grid Card Siege" prototype has been superseded and archived).

## Non-Goals

- Do not optimize for commercial engine workflows.
- Do not add large dependencies unless they clearly support the architecture.
- Do not preserve old gameplay assumptions for nostalgia.
- Do not sacrifice modularity or data-driven design for short-term convenience.

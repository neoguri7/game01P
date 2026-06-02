# Project Vision

## Game Summary

`game01P` is a C++ roguelike dungeon RPG built around D&D 5.5e-inspired
rules. The game is turn-based, system-heavy, and focused on dungeon survival,
party growth, tactical decision-making, and repeatable boss attempts.

## Final Goal

The final project should become a clean, modular, data-driven dungeon RPG
framework and playable roguelike experience. Code quality, maintainability, and
engine ownership matter more than reaching commercial production polish.

The project should avoid dependence on a commercial engine. Its long-term value
is in building a clear C++ SDL3 + EnTT game architecture that can support
turn-based RPG systems, extensible content, and future experimentation.

## Genre And References

- Turn-based roguelike dungeon RPG.
- Inspired by D&D rules-based games, including `Pillars of Eternity`,
  `Baldur's Gate`, and other tactical party RPGs.
- The atmosphere should lean toward a Diablo-like dungeon mood: dark,
  dangerous, loot-driven, and hostile.

## Player Experience

The player should feel like they are preparing, surviving, adapting, and
iterating through dangerous dungeon runs. Each run should create pressure
through limited resources, tactical encounters, item decisions, character
builds, and boss readiness.

The game should reward planning and system understanding more than reflexes.

## Core Loop

1. Enter a dungeon containing general monsters and a few middle bosses.
2. Farm items, resources, experience, and character specialization options.
3. Return to the main room for 정비 시간: repair, preparation, item upgrades,
   skill upgrades, and build decisions.
4. Attempt a divided boss dungeon or boss route.
5. Fail, succeed, improve the build, and repeat the loop.

## Main Systems

- Turn-based dungeon exploration.
- D&D 5.5e-inspired combat rules.
- Monster encounters and middle boss encounters.
- Divided boss dungeons or separated boss routes.
- Item farming, equipment upgrades, and skill upgrades.
- Character specialization and build growth.
- Main room preparation phase, including 정비 시간.
- Data-driven rules, stats, encounters, items, and tuning values.

## Art, Camera, And Style

- 2D top-down camera.
- Pre-rendered art direction.
- Dungeon-heavy Diablo-like atmosphere.
- Tactical readability should take priority over visual spectacle.

## Technical Direction

The project exists to build clean and good C++ game code without relying on a
commercial engine. The `game-principles` rules are core project goals, not just
implementation preferences.

Important technical priorities:

- Clean SDL3 + EnTT architecture.
- Small, modular systems.
- ECS-driven runtime behavior.
- Entity creation through factories.
- Gameplay values stored in external config under `assets/data/`.
- Systems communicating through events instead of direct coupling.
- Code that is easy to delete, replace, and extend.

Game completeness and production polish are secondary to architectural quality.

## Non-Goals

- Do not optimize for commercial engine workflows.
- Do not add large dependencies unless they clearly support the architecture.
- Do not sacrifice modularity or data-driven design for short-term gameplay
  convenience.
- Do not prioritize visual polish over tactical clarity and maintainable code.

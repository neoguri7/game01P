# Tactical D20 Module Map

This module is organized by extension point:

- `actions/`: shared helpers for resolving tactical actions.
- `config/`: config loading defaults and parsed config readers.
- `events/`: tactical event payloads published through `FEventBus`, split by
  mechanism (`Flow`, `Command`, `Action`, `Damage`, `Condition`). Use
  `FTacticalD20Events.h` only for cross-cutting consumers that need all events.
- `logging/`: tactical log appenders and command drag log formatting.
- `rules/`: reusable d20 rules, random roll policy, and math helpers.
- `state/`: registry context state used by tactical systems.
- `systems/actions/`: action economy and command resolution systems.
- `systems/ai/`: tactical AI systems.
- `systems/flow/`: setup, initiative, conditions, and combat lifecycle systems.
- `systems/input/`: command drag/drop and validation systems.
- `systems/presentation/`: labels, telemetry, checklist, and feedback systems.
- `ui/`: tactical UI hit-test helpers used by gameplay systems.

Public data/context headers used outside the module stay at this root.
System registration for tactical systems is centralized in
`TacticalD20SystemRegistration.*`; add new tactical systems there instead of
expanding the global gameplay registration file.

Use `core/events/FEventPublishing.h` for domain-neutral publish/queue helpers.
Keep tactical event payloads and log formatting inside this module.

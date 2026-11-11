T1: schema, stubs, tests committed red (only the 9 expected cases fail; RunewordEffectSchema passes).
T2: effects, guard, resolver call, listener registration, retired filter. T3: info texts and three views.
T4: ship data (engine_ignition, runes, unlocks) and hub_shop count 9 -> 8.
T4 BLOCKED: effect_hooks_tests::HealDeadTargetIsNoOp (line 587, outside the file map) expects FireHooks(OnKill) == 1; the spec-mandated RunewordOnKill listener counts as fired, giving 2. All other ctest entries pass. Awaiting a ruling.
T4 unblocked: effect_hooks_tests HealDeadTargetIsNoOp count 1 -> 2 (spec amended, base 85c6378).
T5 (baseline wp45-runes, seeds 1-12, exit 0, out/baseline-wp45-runes.txt):
BATTLES: all count 108, defeats 0, wins 108, meanRounds 6.11; elite 19 (meanRounds 6.21); normal 89 (meanRounds 6.09).
RUNES: offers 121; picks by rarity: common 51/141, uncommon 60/166, rare 10/56.
Per-rune (offered, picked, rate): area 29,5,0.17; cold 17,12,0.71; combust 6,1,0.17; concentrated 24,5,0.21; duration 13,11,0.85; ember 13,12,0.92; fire 16,12,0.75; frostbite 17,11,0.65; frostpath 10,1,0.10; guard 4,1,0.25; impact 12,1,0.08; leech 40,9,0.23; lightning 21,12,0.57; multishot 9,0,0; pierce 9,3,0.33; precision 16,3,0.19; range 43,6,0.14; shock 11,7,0.64; swift 47,6,0.13; zone 6,3,0.50.
rune.power, rune.inferno, rune.glacial: not offered (0 occurrences).

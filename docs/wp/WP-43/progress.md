T1 done: tests + stubs committed (03aaf39); 10 new cases red, ServicesStayOpenAfterChoice green as expected.
T2 done: CampChoiceThisCamp/SetCampChoice + save keys (built together with T3).
T3: rest replaces paid heal in code, view, fixture, existing tests.
T4: autopilot rest=<P> token + camp branch. T5: ship data values (built together).
T5 done: build.mjs BUILD OK (all ctest entries pass), no stop condition hit (dungeon_sequence_tests never reached Defeat).
T6 evidence (baseline.mjs wp43-rest, seeds 1-12, policy rest=60, exit 0):
- CAMPS: visits 36, meanHpFraction 0.79, meanHpFractionBeforeHeal 0.79, meanMaterials 14.75, meanStaySeconds 0.01
- spend/rest count 2 total 0; spend/rune_offer count 24 total 72; spend/upgrade.attack count 22 total 32
- BATTLES all: count 108, wins 108, defeats 0 (normal 108/108, elite 0); meanRounds 6.07

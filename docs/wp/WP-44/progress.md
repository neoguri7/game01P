T1 done: loader, ship elite block, FEliteRules state install, sheet stage, fork text, strict offer; tests added. Red: SpellFed, Escalating, Thorned, Unyielding, HudLine, FeedLines (EliteBattleInstallsState already passes: state install is in T1).
T2/T3 done: rules, call sites, texts, HUD line; build.mjs BUILD OK (all ctest).
T4 evidence (seeds 1-12, policy fork=variant), out/baseline-wp44-elite.txt:
  elite: count 19, defeats 0, meanFirstDamageRound 1.68, meanRounds 6.32, meanSimSeconds 36.00, meanWallSeconds 2.57, wins 19
FORKS
  elite:affix.escalating: count 7, meanHpFraction 0.91, variantRate 1
  elite:affix.spell_fed: count 5, meanHpFraction 0.77, variantRate 1
  elite:affix.thorned: count 3, meanHpFraction 0.78, variantRate 1
  elite:affix.unyielding: count 4, meanHpFraction 0.86, variantRate 1

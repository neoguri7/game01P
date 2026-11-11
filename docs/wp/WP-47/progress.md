T1 red: four new cases fail, committed. T2 data: units/bosses/ai/battle_anim, boss_goals_tests green. FBattle gained copyFixture param.
T3 BLOCKED: build.mjs red, settings_tests::AnimSpeedScalesPresentationOnly line 705 InitializeBattleAnim false. Hypothesis: test boots fixture units (TestAssetRoots) + shipped battle_anim.json; ParseBattleAnim rejects new unit rows ('enemy.cinder_warden' is not a unit). Outside file map.
T3 resolved (coordinator ruling c): battle_anim.json reverted to 4ba20c8; settings_tests + boss_goals_tests green.
T4 baseline wp47-boss seeds 1-4 (exit 0 each): 4 runs, 4 finalBosses, results defeat 4. Battles all: count 96, defeats 4, wins 92, meanRounds 9.05 (normal 96, elite 0).
boss_defeat in run logs (each 4x): boss.stone_guardian, boss.cinder_warden, boss.chain_warden. boss.ash_hierophant never defeated (4 defeats, balance observation).
T3 BLOCKED (2): meta_save_tests::UnlockCatalogValid line 746 - unlocks.json skill unlocks skill.hook and skill.shackle sit at index 0/1 of enemy.chain_warden's skills; test requires every unit authoring an unlockable skill to place it at index >= 4. Outside file map.

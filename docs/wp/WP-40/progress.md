# WP-40 progress

- Red: tests (content_a, reward_offer, run_log, camp, reward_equip rewrite) committed first (0725b03); they fail to compile until the API exists.
- T1 rune rarity content: ERuneRarity, ParseRuneRarity/RuneRarityName, kRuneFields rarity (19 fields), boot gate in FContentRegistry, runes.json rarities 8/8/8.
- T2 odds tables: FRarityWeights, rarityTables, FindRarityTable, LoadRewards validation, rewards.json tables.
- T3 tiered draw: CandidatesFor tier roll (kTierSalt) + fallback order, table through Roll/Peek, offer.rarityTable (room, boss extra, camp), save section `rarity_table`.
- T4 fit preview: FRuneFitPreview, RuneFitPreviews.
- T5 offer screen: RuneOfferDisplayOrder, RuneOfferOddsLine, RuneFitPreviewLine, card group + rarity border.
- T6 run log: offer record carries table, rarities, picked_rarity.
- Offer card layout (reason: reward modal height at 1280x800): one title row (Pick, name, rarity), capped inline `Fits:` (first 3 skills, then `+N more (hover)`) with the full before/after lines in a hover tooltip. `reward_equip_tests::OfferModalDoesNotScroll` guards it; MeasureFitsWrap is at 1280x800.
- Loader ceiling: each rarity weight must be 0..1,000,000.

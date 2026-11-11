#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/combat/FSkillResolver.h"

namespace game {

struct FAssetManager;

/// WP-40: one named rarity odds table (relative integer weights, not percentages).
struct FRarityWeights {
    std::string name;
    int common{0};
    int uncommon{0};
    int rare{0};
};

/// WP-17: the reward-offer knobs as authored in `assets/data/rewards.json`. Only the offer economy lives
/// here — materials/EXP payouts stay in `data/dungeon.json balance` (one canonical source per number).
struct FRewardTables {
    int count{3};                            // cards per offer (an upper bound: the legal pool can be smaller)
    double chance{0.7};                      // per-monster-room chance, 0..1
    int pityThreshold{2};                    // dry-streak length that forces the next monster-room offer; 0 = off
    int minLegalCandidates{2};               // fewer legal candidates than this: no offer at all (0 reads as 1)
    int skipRefundMaterials{1};              // materials paid when the player skips
    int duplicateRefundMaterials{1};         // materials paid for an already-owned pick (refund policy)
    std::string duplicatePolicy{"refund"};   // "refund" | "allow"
    std::string bossOffer{"guaranteed"};     // "guaranteed" | "extra" | "none"
    std::vector<FRarityWeights> rarityTables; // WP-40: empty = tiering off (every card draws from the whole pool)
};

inline constexpr std::string_view kRarityTableNormal = "normal";
inline constexpr std::string_view kRarityTableBoosted = "boosted";
inline constexpr std::string_view kRarityTableBoss = "boss";

inline constexpr std::string_view kRewardDuplicatePolicyRefund = "refund";
inline constexpr std::string_view kRewardDuplicatePolicyAllow = "allow";

inline constexpr std::string_view kRewardBossOfferGuaranteed = "guaranteed";
inline constexpr std::string_view kRewardBossOfferExtra = "extra";
inline constexpr std::string_view kRewardBossOfferNone = "none";

struct FRuneOffer {
    bool active{false};
    std::vector<std::string> candidates;
    std::string rarityTable;  // WP-40: the table name this offer was rolled with; empty when tiering is off
};

/// The reward module's state: the loaded tables plus the run-scoped dry streak, the deterministic draw
/// cursor and the open offer. It lives in the registry context and is saved through the `offers` section.
struct FRewards {
    FRewardTables tables;
    std::uint32_t runSeed{0};
    int pity{0};
    std::uint32_t nextDrawIndex{0};
    FRuneOffer offer;
    bool bossExtraPending{false};  // boss_offer="extra": a second offer follows the first decision
};

/// Boot load (fail-fast): `data/rewards` through the asset boundary + FJsonLoader. A missing file or a
/// validation failure returns false and the boot stops — an offer economy with guessed numbers is worse
/// than no boot. Re-initialization replaces the module state (it never keeps a stale copy).
[[nodiscard]] bool LoadRewards(entt::registry& registry, const FAssetManager& assets);

/// Registers the `offers` save section (B4). Call once, under the [WP-17] anchor in the composition root.
void RegisterRewardSection(entt::registry& registry);

/// Clears the run-scoped reward state unconditionally (pity, draw cursor, open offer, boss extra flag) and
/// tags it with the installed run's seed. A new run with the same seed must also start clean.
void ResetRewardsForRun(entt::registry& registry);

/// Resets the module state when the installed run's seed is not the one it was tracking. A load installs
/// the run after unpacking the section, so this comparison happens lazily on the next reward query.
void EnsureRewardsForRun(entt::registry& registry);

/// Room clear: decides (and opens) the offer only. It never pays materials or EXP and never changes the
/// phase. `bossRoom` uses `boss_offer`; a monster room uses chance/pity.
/// An elite monster room skips the chance roll and opens a strict "boss"-table offer: uncommon or better (WP-44).
void RollRoomOffer(entt::registry& registry, bool bossRoom, bool elite = false);

/// Rolls candidates from `rarityTable` (RollRuneCandidates) and opens the offer: active, candidates,
/// rarityTable = FindRarityTable name or empty, pity = 0, bossExtraPending = false. False when an offer is
/// already open (nothing consumed) or fewer than max(1, min_legal_candidates) candidates came up (the draw
/// index is consumed, nothing else changes). `strictTiers` (WP-44): no candidate of a rarity whose table weight is 0.
bool OpenRuneOffer(entt::registry& registry, std::string_view rarityTable, bool strictTiers = false);

/// Resolves the open offer: `pickedIndex < 0` skips (materials refund), otherwise the candidate is granted
/// (or refunded when already owned and the policy says so). It never changes the phase. False = nothing was
/// open or the index is out of range (no state change).
[[nodiscard]] bool ResolveRuneOffer(entt::registry& registry, int pickedIndex);

/// WP-40: pure lookup, never logs: the named table, else the table named `normal`, else nullptr (tiering off).
[[nodiscard]] const FRarityWeights* FindRarityTable(const FRewardTables& tables, std::string_view name);

/// The public draw API (WP-10 camp purchases reuse it): weighted, legal-only candidates. Consumes one draw
/// index, so a caller gets the next deterministic draw of this (run, depth, room). Each card first rolls a
/// rarity from `rarityTable` (WP-40), then draws a legal rune of that rarity.
[[nodiscard]] std::vector<std::string> RollRuneCandidates(
    entt::registry& registry, std::string_view rarityTable = kRarityTableNormal, bool strictTiers = false);

/// The distinct skills of the run party (made public for the elite texts, WP-44).
[[nodiscard]] std::vector<const gameplay::FSkillContent*> PartySkillRows(entt::registry& registry);

/// The same draw without consuming an index (previews/tests).
[[nodiscard]] std::vector<std::string> PeekRuneCandidates(
    entt::registry& registry, std::string_view rarityTable = kRarityTableNormal);

struct RuneOfferForecast {
    bool guaranteed{false};
    int pity{0};
    double chance{0.0};
};

/// The one-line forecast the reward/camp screens show: whether the next monster room is forced by pity and
/// what the plain chance is.
[[nodiscard]] RuneOfferForecast RuneOfferForecastOf(entt::registry& registry);

/// The party skills this rune could be fitted to (base skill only: camp can swap runes, so already-fitted
/// runes and free sockets are ignored). Empty = the rune is not a legal candidate for this party.
[[nodiscard]] std::vector<std::string> RuneFittingSkillNames(entt::registry& registry, std::string_view runeId);

/// WP-40: one fitting party skill with its numbers before and after the rune.
struct FRuneFitPreview {
    std::string skillId;
    std::string skillName;      // display name, id when the name is empty
    FResolvedSkill before;
    FResolvedSkill after;
    bool alreadyFitted{false};  // the slot already holds this rune: before and after are the same resolve
    bool socketsFull{false};    // the slot's fitted runes fill every socket
};

/// One entry per party skill id the rune fits, in PartySkills order (ids are unique there; two skills that
/// share a display name give two entries - intended).
[[nodiscard]] std::vector<FRuneFitPreview> RuneFitPreviews(entt::registry& registry, std::string_view runeId);

/// True when the rune is already owned: in the collected inventory or fitted to a party skill.
[[nodiscard]] bool IsRuneOwned(const entt::registry& registry, std::string_view runeId);

} // namespace game

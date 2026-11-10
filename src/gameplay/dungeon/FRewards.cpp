#include "gameplay/dungeon/FRewards.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/JsonLoader.h"
#include "core/Logger.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/meta/FMetaProgress.h"

namespace game {
namespace {

using gameplay::ERuneRarity;
using gameplay::FContentRegistry;
using gameplay::FRuneContent;
using gameplay::FSkillContent;

// The chance roll and the candidate draw are separate streams of the same (seed, depth, room, drawIndex)
// grid: distinct salts keep a chance roll from aliasing a draw, and neither touches the combat RNG.
constexpr std::uint32_t kChanceSalt = 0x51ed270bu;
constexpr std::uint32_t kDrawSalt = 0x2545f491u;
// WP-40: the per-card rarity roll has its own stream so the pick stream (kDrawSalt) is unchanged.
constexpr std::uint32_t kTierSalt = 0x6b43a9b5u;

constexpr int kMinOfferCount = 1;

/// A deterministic value mix (not a PRNG service): the reward draw must be reproducible from the save alone.
[[nodiscard]] std::uint32_t MixBits(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

[[nodiscard]] std::uint32_t Combine(std::uint32_t seed, std::uint32_t value) {
    return MixBits(seed ^ (value + 0x9e3779b9u + (seed << 6) + (seed >> 2)));
}

[[nodiscard]] std::uint32_t StreamSeed(
    std::uint32_t runSeed, int depth, int room, std::uint32_t drawIndex, std::uint32_t salt) {
    std::uint32_t value = MixBits(runSeed);
    value = Combine(value, static_cast<std::uint32_t>(depth));
    value = Combine(value, static_cast<std::uint32_t>(room));
    value = Combine(value, drawIndex);
    return Combine(value, salt);
}

/// A small xorshift32: enough for a weighted pick, and the same bytes on every platform.
struct FRewardRng {
    std::uint32_t state;

    explicit FRewardRng(std::uint32_t seed) : state(seed == 0 ? 0x9e3779b9u : seed) {}

    std::uint32_t Next() {
        std::uint32_t value = state;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        state = value;
        return value;
    }

    [[nodiscard]] double Unit() {
        constexpr double kScale = 4294967296.0;  // 2^32: Unit() is always < 1
        return static_cast<double>(Next()) / kScale;
    }
};

[[nodiscard]] int RuneWeight(const FRuneContent& rune) {
    return std::max(1, rune.dropWeight);
}

/// Weighted pick with replacement-free bookkeeping: a rune's drop_weight is its share of the remaining pool.
[[nodiscard]] std::size_t PickWeighted(FRewardRng& rng, const std::vector<const FRuneContent*>& pool) {
    int total = 0;
    for (const auto* rune : pool) total += RuneWeight(*rune);
    if (total <= 0) return 0;
    int roll = static_cast<int>(rng.Next() % static_cast<std::uint32_t>(total));
    for (std::size_t index = 0; index < pool.size(); ++index) {
        roll -= RuneWeight(*pool[index]);
        if (roll < 0) return index;
    }
    return pool.size() - 1;
}

[[nodiscard]] bool IsOwned(const FDungeonRun& run, std::string_view runeId) {
    for (const auto& id : run.runes) {
        if (id == runeId) return true;
    }
    for (const auto& member : run.party) {
        for (const auto& slot : member.skills) {
            for (const auto& id : slot.runeIds) {
                if (id == runeId) return true;
            }
        }
    }
    return false;
}

/// The party's skills: the captured loadout (filled by CaptureRunParty) and, right after StartRun when the
/// run party carries no slots yet, the same unit content the first battle will equip.
[[nodiscard]] std::vector<const FSkillContent*> PartySkills(
    entt::registry& registry, const FContentRegistry& content, const FDungeonRun& run) {
    std::vector<std::string> ids;
    for (const auto& member : run.party) {
        for (const auto& slot : member.skills) {
            if (!slot.skillId.empty() && std::find(ids.begin(), ids.end(), slot.skillId) == ids.end()) {
                ids.push_back(slot.skillId);
            }
        }
    }
    if (ids.empty()) {
        try {
            for (const auto& member : DungeonDefinition(registry).at("party")) {
                const auto* unit = content.units.find(member.at("unit").get<std::string>());
                if (unit == nullptr) continue;
                for (const auto& id : unit->skillIds) {
                    if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
                }
            }
        } catch (const nlohmann::json::exception& error) {
            LOG_WARN("Reward offers could not read the party skills from the dungeon definition: {}", error.what());
        }
    }
    std::vector<const FSkillContent*> skills;
    skills.reserve(ids.size());
    for (const auto& id : ids) {
        if (const auto* skill = content.skills.find(id)) skills.push_back(skill);
    }
    return skills;
}

/// Legality is judged against the base skill alone: the rune must fit some party skill with a free socket,
/// and camp can swap already-fitted runes, so the current loadout's other runes are not part of the gate.
[[nodiscard]] bool RuneFitsSkills(const std::vector<const FSkillContent*>& skills, const FRuneContent& rune) {
    for (const auto* skill : skills) {
        if (skill->runeSockets <= 0) continue;
        if (!CheckRuneTagRequirements(*skill, {&rune}).has_value()) return true;
    }
    return false;
}

/// WP-40 F5: when the rolled rarity has no unowned legal rune left, the card falls to the next rarity in this
/// order (rare: down first; uncommon: down before up; common: up).
constexpr std::array<std::array<ERuneRarity, 3>, 3> kRarityFallback{{
    {ERuneRarity::Common, ERuneRarity::Uncommon, ERuneRarity::Rare},
    {ERuneRarity::Uncommon, ERuneRarity::Common, ERuneRarity::Rare},
    {ERuneRarity::Rare, ERuneRarity::Uncommon, ERuneRarity::Common},
}};

[[nodiscard]] std::vector<std::string> CandidatesFor(
    entt::registry& registry, std::uint32_t drawIndex, std::string_view rarityTable) {
    std::vector<std::string> candidates;
    auto* rewards = registry.ctx().find<FRewards>();
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* content = registry.ctx().find<FContentRegistry>();
    if (rewards == nullptr || run == nullptr || content == nullptr) return candidates;

    // The party skill list is a JSON walk plus content lookups: resolve it once, not once per rune.
    const auto skills = PartySkills(registry, *content, *run);

    // Pool = every unowned legal rune + at most one owned duplicate (so an owned-out rune set still offers a
    // choice instead of a dead card row).
    std::vector<const FRuneContent*> unowned;
    std::vector<const FRuneContent*> owned;
    for (const auto& rune : content->runes.rows) {
        // R6/WP-21c: a rune with an unlocks.json row is offered only after it is bought — the single point
        // every room/boss/camp draw passes.
        if (IsUnlockGated(registry, rune.id)) continue;
        if (!RuneFitsSkills(skills, rune)) continue;
        (IsOwned(*run, rune.id) ? owned : unowned).push_back(&rune);
    }

    const auto requested = static_cast<std::size_t>(std::max(kMinOfferCount, rewards->tables.count));
    FRewardRng rng(StreamSeed(run->seed, run->depth, run->room, drawIndex, kDrawSalt));
    FRewardRng tierRng(StreamSeed(run->seed, run->depth, run->room, drawIndex, kTierSalt));
    const FRarityWeights* weights = FindRarityTable(rewards->tables, rarityTable);

    // Cards come from the unowned pool first (weighted, no repeats), so a duplicate never takes a slot while
    // enough unowned runes exist.
    std::vector<const FRuneContent*> pool = unowned;
    const std::size_t fromUnowned = std::min(requested, pool.size());
    candidates.reserve(std::min(requested, pool.size() + (owned.empty() ? 0u : 1u)));
    for (std::size_t slot = 0; slot < fromUnowned; ++slot) {
        std::vector<const FRuneContent*> sub;
        if (weights == nullptr) {
            sub = pool;
        } else {
            // One tier roll per slot, always consumed; then walk the fallback order to a non-empty tier.
            const auto total = static_cast<std::uint32_t>(weights->common + weights->uncommon + weights->rare);
            const auto roll = static_cast<int>(tierRng.Next() % total);
            const std::size_t rolled =
                roll < weights->common ? 0u : (roll < weights->common + weights->uncommon ? 1u : 2u);
            for (const ERuneRarity tier : kRarityFallback[rolled]) {
                for (const auto* rune : pool) {
                    if (rune->rarity == tier) sub.push_back(rune);
                }
                if (!sub.empty()) break;
            }
        }
        const std::size_t picked = PickWeighted(rng, sub);
        candidates.push_back(sub[picked]->id);
        pool.erase(std::find(pool.begin(), pool.end(), sub[picked]));
    }
    // Only a short unowned pool earns the single owned duplicate card, always in the last slot.
    if (!owned.empty() && fromUnowned < requested) {
        candidates.push_back(owned[PickWeighted(rng, owned)]->id);
    }
    return candidates;
}

[[nodiscard]] bool IsKnownDuplicatePolicy(std::string_view policy) {
    return policy == kRewardDuplicatePolicyRefund || policy == kRewardDuplicatePolicyAllow;
}

[[nodiscard]] bool IsKnownBossOffer(std::string_view offer) {
    return offer == kRewardBossOfferGuaranteed || offer == kRewardBossOfferExtra ||
           offer == kRewardBossOfferNone;
}

} // namespace

const FRarityWeights* FindRarityTable(const FRewardTables& tables, std::string_view name) {
    const FRarityWeights* normal = nullptr;
    for (const auto& table : tables.rarityTables) {
        if (table.name == name) return &table;
        if (table.name == kRarityTableNormal) normal = &table;
    }
    return normal;
}

bool LoadRewards(entt::registry& registry, const FAssetManager& assets) {
    const auto path = assets.resolvePath("data/rewards");
    if (!path) {
        LOG_ERROR("rewards.json is missing: the reward offer settings are required for boot.");
        return false;
    }
    const auto document = FJsonLoader::Load(*path, 1);
    if (!document) return false;  // the reason is already logged

    FRewards rewards;
    try {
        const auto& offer = document->at("rune_offer");
        if (!offer.is_object()) {
            LOG_ERROR("rewards.json rune_offer must be an object.");
            return false;
        }
        rewards.tables.count = offer.at("count").get<int>();
        rewards.tables.chance = offer.at("chance").get<double>();
        rewards.tables.pityThreshold = offer.at("pity_threshold").get<int>();
        rewards.tables.minLegalCandidates = offer.at("min_legal_candidates").get<int>();
        rewards.tables.skipRefundMaterials = offer.at("skip_refund_materials").get<int>();
        rewards.tables.duplicateRefundMaterials = offer.at("duplicate_refund_materials").get<int>();
        rewards.tables.duplicatePolicy = offer.at("duplicate_policy").get<std::string>();
        rewards.tables.bossOffer = document->at("boss_offer").get<std::string>();
    } catch (const nlohmann::json::exception& error) {
        LOG_ERROR("rewards.json is unusable: {}", error.what());
        return false;
    }

    const FRewardTables& tables = rewards.tables;
    if (tables.count < kMinOfferCount) {
        LOG_ERROR("rewards.json rune_offer.count must be at least {}.", kMinOfferCount);
        return false;
    }
    if (!(tables.chance >= 0.0 && tables.chance <= 1.0)) {
        LOG_ERROR("rewards.json rune_offer.chance must be inside [0, 1].");
        return false;
    }
    if (tables.pityThreshold < 0) {
        LOG_ERROR("rewards.json rune_offer.pity_threshold must be 0 (off) or higher.");
        return false;
    }
    if (tables.minLegalCandidates < 0 || tables.minLegalCandidates > tables.count) {
        LOG_ERROR("rewards.json rune_offer.min_legal_candidates must be between 0 and count.");
        return false;
    }
    if (tables.skipRefundMaterials < 0 || tables.duplicateRefundMaterials < 0) {
        LOG_ERROR("rewards.json refunds must not be negative.");
        return false;
    }
    if (!IsKnownDuplicatePolicy(tables.duplicatePolicy)) {
        LOG_ERROR("rewards.json rune_offer.duplicate_policy must be 'refund' or 'allow'.");
        return false;
    }
    if (!IsKnownBossOffer(tables.bossOffer)) {
        LOG_ERROR("rewards.json boss_offer must be 'guaranteed', 'extra' or 'none'.");
        return false;
    }

    // WP-40: optional odds tables; absent = tiering off. Present = every table valid and "normal" among them.
    if (document->contains("rarity_tables")) {
        const auto& tablesJson = document->at("rarity_tables");
        if (!tablesJson.is_object()) {
            LOG_ERROR("rewards.json rarity_tables must be an object.");
            return false;
        }
        for (const auto& [name, value] : tablesJson.items()) {
            if (!value.is_object()) {
                LOG_ERROR("rewards.json rarity_tables.{} must be an object.", name);
                return false;
            }
            FRarityWeights weights;
            weights.name = name;
            int* slots[] = {&weights.common, &weights.uncommon, &weights.rare};
            const char* keys[] = {"common", "uncommon", "rare"};
            for (std::size_t slot = 0; slot < 3; ++slot) {
                const auto found = value.find(keys[slot]);
                if (found == value.end() || !found->is_number_integer()) {
                    LOG_ERROR("rewards.json rarity_tables.{}.{} must be an integer weight.", name, keys[slot]);
                    return false;
                }
                const auto weight = found->get<long long>();
                if (weight < 0 || weight > 1000000) {
                    LOG_ERROR("rewards.json rarity_tables.{}.{} must be between 0 and 1000000.", name, keys[slot]);
                    return false;
                }
                *slots[slot] = static_cast<int>(weight);
            }
            if (weights.common + weights.uncommon + weights.rare <= 0) {
                LOG_ERROR("rewards.json rarity_tables.{} weights sum to 0.", name);
                return false;
            }
            rewards.tables.rarityTables.push_back(std::move(weights));
        }
        const bool hasNormal = std::any_of(rewards.tables.rarityTables.begin(), rewards.tables.rarityTables.end(),
            [](const FRarityWeights& table) { return table.name == kRarityTableNormal; });
        if (!hasNormal) {
            LOG_ERROR("rewards.json rarity_tables must contain a 'normal' table.");
            return false;
        }
    }

    // Re-initialization (tests, debug reloads) must replace the module state instead of keeping a stale one.
    registry.ctx().erase<FRewards>();
    auto& stored = registry.ctx().emplace<FRewards>(std::move(rewards));
    LOG_INFO("rewards ready: count {} chance {:.2f} pity {} min legal {} boss '{}' rarity tables {}.", stored.tables.count,
             stored.tables.chance, stored.tables.pityThreshold, stored.tables.minLegalCandidates,
             stored.tables.bossOffer, stored.tables.rarityTables.size());
    return true;
}

void ResetRewardsForRun(entt::registry& registry) {
    auto* rewards = registry.ctx().find<FRewards>();
    if (rewards == nullptr) return;  // the module is not loaded: nothing to reset
    const auto* run = registry.ctx().find<FDungeonRun>();
    rewards->runSeed = run != nullptr ? run->seed : 0;
    rewards->pity = 0;
    rewards->nextDrawIndex = 0;
    rewards->offer = FRuneOffer{};
    rewards->bossExtraPending = false;
}

void EnsureRewardsForRun(entt::registry& registry) {
    auto* rewards = registry.ctx().find<FRewards>();
    if (rewards == nullptr) return;
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr || rewards->runSeed == run->seed) return;
    ResetRewardsForRun(registry);
}

void RollRoomOffer(entt::registry& registry, bool bossRoom) {
    EnsureRewardsForRun(registry);
    auto* rewards = registry.ctx().find<FRewards>();
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (rewards == nullptr || run == nullptr) return;
    if (rewards->offer.active) return;  // one offer per room: a repeated call never re-rolls or re-pities

    const FRewardTables& tables = rewards->tables;
    if (bossRoom) {
        if (tables.bossOffer == kRewardBossOfferNone) return;
        // guaranteed / extra both open an offer here; they never stack with a pity force (one offer).
    } else {
        const bool forced = tables.pityThreshold > 0 && rewards->pity >= tables.pityThreshold;
        if (!forced) {
            FRewardRng rng(StreamSeed(run->seed, run->depth, run->room, rewards->nextDrawIndex, kChanceSalt));
            if (rng.Unit() >= tables.chance) {
                ++rewards->pity;  // a miss extends the dry streak
                LOG_INFO("Rune offer missed: pity is now {}.", rewards->pity);
                return;
            }
        }
    }

    const std::string_view table = bossRoom ? kRarityTableBoss : kRarityTableNormal;
    auto candidates = RollRuneCandidates(registry, table);  // consumes one draw index
    // An empty offer is a dead modal: min_legal_candidates 0 still needs one card to be a choice.
    const auto minimum = static_cast<std::size_t>(std::max(1, tables.minLegalCandidates));
    if (candidates.size() < minimum) {
        // A short pool is not a dry streak: the draw cursor already advanced with this call, only pity is kept.
        LOG_INFO("Rune offer withheld: {} legal candidate(s) < {}.", candidates.size(), minimum);
        return;
    }
    rewards->offer.active = true;
    rewards->offer.candidates = std::move(candidates);
    const FRarityWeights* used = FindRarityTable(tables, table);
    rewards->offer.rarityTable = used != nullptr ? used->name : std::string{};
    rewards->pity = 0;  // any offer resets the dry streak, forced or not
    rewards->bossExtraPending = bossRoom && tables.bossOffer == kRewardBossOfferExtra;
    LOG_INFO("Rune offer opened: {} candidate(s){}.", rewards->offer.candidates.size(), bossRoom ? " (boss room)" : "");
}

bool ResolveRuneOffer(entt::registry& registry, int pickedIndex) {
    EnsureRewardsForRun(registry);
    auto* rewards = registry.ctx().find<FRewards>();
    auto* run = registry.ctx().find<FDungeonRun>();
    if (rewards == nullptr || run == nullptr) return false;
    if (!rewards->offer.active) return false;
    if (pickedIndex >= static_cast<int>(rewards->offer.candidates.size())) return false;

    const FRewardTables& tables = rewards->tables;
    if (pickedIndex < 0) {
        run->materials += tables.skipRefundMaterials;
        AppendRunEvent(registry, ERunEventKind::RewardPicked, {}, tables.skipRefundMaterials);
        LOG_INFO("Rune offer skipped: +{} material(s).", tables.skipRefundMaterials);
    } else {
        const std::string runeId = rewards->offer.candidates[static_cast<std::size_t>(pickedIndex)];
        if (IsOwned(*run, runeId) && tables.duplicatePolicy == kRewardDuplicatePolicyRefund) {
            run->materials += tables.duplicateRefundMaterials;
            AppendRunEvent(registry, ERunEventKind::RewardPicked, runeId, tables.duplicateRefundMaterials);
            LOG_INFO("Rune '{}' is already owned: +{} material(s).", runeId, tables.duplicateRefundMaterials);
        } else {
            run->runes.push_back(runeId);
            AppendRunEvent(registry, ERunEventKind::RewardPicked, runeId, 0);
            LOG_INFO("Rune '{}' collected.", runeId);
        }
    }

    rewards->offer.active = false;
    rewards->offer.candidates.clear();
    rewards->offer.rarityTable.clear();
    const bool extraOffer = rewards->bossExtraPending;
    rewards->bossExtraPending = false;
    if (extraOffer) {
        // boss_offer="extra": the boss room pays a second, still rejectable offer after the first decision.
        auto candidates = RollRuneCandidates(registry, kRarityTableBoss);
        const auto minimum = static_cast<std::size_t>(std::max(1, tables.minLegalCandidates));
        if (candidates.size() >= minimum) {
            rewards->offer.active = true;
            rewards->offer.candidates = std::move(candidates);
            const FRarityWeights* used = FindRarityTable(tables, kRarityTableBoss);
            rewards->offer.rarityTable = used != nullptr ? used->name : std::string{};
        }
    }
    if (!SaveDungeonRun(registry)) {
        LOG_ERROR("A rune decision could not be saved; a reload may repeat it.");
    }
    return true;
}

std::vector<std::string> RollRuneCandidates(entt::registry& registry, std::string_view rarityTable) {
    EnsureRewardsForRun(registry);
    auto* rewards = registry.ctx().find<FRewards>();
    if (rewards == nullptr) return {};
    auto candidates = CandidatesFor(registry, rewards->nextDrawIndex, rarityTable);
    ++rewards->nextDrawIndex;  // every draw call advances the cursor, even when the pool came up short
    return candidates;
}

std::vector<std::string> PeekRuneCandidates(entt::registry& registry, std::string_view rarityTable) {
    EnsureRewardsForRun(registry);
    auto* rewards = registry.ctx().find<FRewards>();
    if (rewards == nullptr) return {};
    return CandidatesFor(registry, rewards->nextDrawIndex, rarityTable);
}

RuneOfferForecast RuneOfferForecastOf(entt::registry& registry) {
    RuneOfferForecast forecast;
    EnsureRewardsForRun(registry);
    const auto* rewards = registry.ctx().find<FRewards>();
    if (rewards == nullptr) return forecast;
    forecast.pity = rewards->pity;
    forecast.chance = rewards->tables.chance;
    forecast.guaranteed =
        rewards->tables.pityThreshold > 0 && rewards->pity >= rewards->tables.pityThreshold;
    return forecast;
}

std::vector<std::string> RuneFittingSkillNames(entt::registry& registry, std::string_view runeId) {
    std::vector<std::string> names;
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* content = registry.ctx().find<FContentRegistry>();
    if (run == nullptr || content == nullptr) return names;
    const auto* rune = content->runes.find(runeId);
    if (rune == nullptr) return names;
    for (const auto* skill : PartySkills(registry, *content, *run)) {
        if (skill->runeSockets <= 0) continue;
        if (CheckRuneTagRequirements(*skill, {rune}).has_value()) continue;
        const std::string& name = skill->displayName.empty() ? skill->id : skill->displayName;
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    return names;
}

std::vector<FRuneFitPreview> RuneFitPreviews(entt::registry& registry, std::string_view runeId) {
    std::vector<FRuneFitPreview> previews;
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* content = registry.ctx().find<FContentRegistry>();
    if (run == nullptr || content == nullptr) return previews;
    const auto* rune = content->runes.find(runeId);
    if (rune == nullptr) return previews;
    for (const auto* skill : PartySkills(registry, *content, *run)) {
        if (skill->runeSockets <= 0) continue;
        if (CheckRuneTagRequirements(*skill, {rune}).has_value()) continue;

        // The first party slot (party order, then slot order) that holds this skill.
        const std::vector<std::string>* slotRunes = nullptr;
        for (const auto& member : run->party) {
            for (const auto& slot : member.skills) {
                if (slot.skillId == skill->id) {
                    slotRunes = &slot.runeIds;
                    break;
                }
            }
            if (slotRunes != nullptr) break;
        }
        std::vector<const FRuneContent*> fitted;
        bool alreadyFitted = false;
        if (slotRunes != nullptr) {
            for (const auto& id : *slotRunes) {
                if (id == runeId) alreadyFitted = true;
                if (const auto* fittedRune = content->runes.find(id)) fitted.push_back(fittedRune);
            }
        }

        FRuneFitPreview preview;
        preview.skillId = skill->id;
        preview.skillName = skill->displayName.empty() ? skill->id : skill->displayName;
        preview.alreadyFitted = alreadyFitted;
        preview.socketsFull = static_cast<int>(fitted.size()) >= skill->runeSockets;
        auto withRune = fitted;
        withRune.push_back(rune);
        if (alreadyFitted) {
            preview.before = ResolveSkill(*skill, fitted);
            preview.after = preview.before;
        } else if (!preview.socketsFull && !CheckRuneTagRequirements(*skill, withRune).has_value()) {
            preview.before = ResolveSkill(*skill, fitted);
            preview.after = ResolveSkill(*skill, withRune);
        } else {
            preview.before = ResolveSkill(*skill, {});
            preview.after = ResolveSkill(*skill, {rune});
        }
        previews.push_back(std::move(preview));
    }
    return previews;
}

bool IsRuneOwned(const entt::registry& registry, std::string_view runeId) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    return run != nullptr && IsOwned(*run, runeId);
}

} // namespace game

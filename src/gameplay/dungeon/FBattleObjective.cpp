#include "gameplay/dungeon/FBattleObjective.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/FAssetDigest.h"
#include "core/Logger.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillApCap.h"
#include "gameplay/dungeon/FDungeonRun.h"

namespace game {
namespace {

constexpr int kMaxRewardMaterials = 10;
constexpr int kMaxRounds = 60;

/// One required non-empty string field; logs and returns false on a violation.
[[nodiscard]] bool ReadString(const nlohmann::json& row, const char* key, std::size_t index, std::string& out) {
    const auto found = row.find(key);
    if (found == row.end() || !found->is_string() || found->get<std::string>().empty()) {
        LOG_ERROR("battle_objectives[{}]: '{}' must be a non-empty string.", index, key);
        return false;
    }
    out = found->get<std::string>();
    return true;
}

/// An integer field in [minimum, maximum]; absent is allowed only when `required` is false.
[[nodiscard]] bool ReadInt(const nlohmann::json& row, const char* key, std::size_t index, int minimum, int maximum,
                           bool required, int& out) {
    const auto found = row.find(key);
    if (found == row.end()) {
        if (required) LOG_ERROR("battle_objectives[{}]: '{}' is required.", index, key);
        return !required;
    }
    if (!found->is_number_integer()) {
        LOG_ERROR("battle_objectives[{}]: '{}' must be a whole number.", index, key);
        return false;
    }
    const int value = found->get<int>();
    if (value < minimum || value > maximum) {
        LOG_ERROR("battle_objectives[{}]: '{}' is {} - outside {}..{}.", index, key, value, minimum, maximum);
        return false;
    }
    out = value;
    return true;
}

[[nodiscard]] bool ParseRow(const nlohmann::json& row, std::size_t index, FBattleObjectiveDef& def) {
    if (!row.is_object()) {
        LOG_ERROR("battle_objectives[{}] must be an object.", index);
        return false;
    }
    for (const auto& [key, value] : row.items()) {
        (void)value;
        if (key != "id" && key != "display_name" && key != "text" && key != "kind" && key != "rounds" &&
            key != "reward_materials") {
            LOG_ERROR("battle_objectives[{}]: unknown key '{}'.", index, key);
            return false;
        }
    }
    std::string kind;
    if (!ReadString(row, "id", index, def.id) || !ReadString(row, "display_name", index, def.displayName) ||
        !ReadString(row, "text", index, def.text) || !ReadString(row, "kind", index, kind)) {
        return false;
    }
    if (kind == "no_ally_down") {
        def.kind = EBattleObjectiveKind::NoAllyDown;
        if (row.contains("rounds")) {
            LOG_ERROR("battle_objectives[{}] ('{}'): 'rounds' is not allowed for no_ally_down.", index, def.id);
            return false;
        }
    } else if (kind == "win_by_round") {
        def.kind = EBattleObjectiveKind::WinByRound;
        if (!ReadInt(row, "rounds", index, 1, kMaxRounds, true, def.rounds)) return false;
    } else {
        LOG_ERROR("battle_objectives[{}] ('{}'): unknown kind '{}'.", index, def.id, kind);
        return false;
    }
    return ReadInt(row, "reward_materials", index, 0, kMaxRewardMaterials, true, def.rewardMaterials);
}

} // namespace

std::optional<FBattleObjectiveTable> LoadBattleObjectives(const FAssetManager& assets) {
    FBattleObjectiveTable table;
    const auto asset = assets.digest("data/dungeon");
    if (!asset) return table;
    const nlohmann::json& data = asset->json();
    const auto found = data.find("battle_objectives");
    if (found == data.end()) return table;
    if (!found->is_array()) {
        LOG_ERROR("dungeon 'battle_objectives' must be an array.");
        return std::nullopt;
    }
    std::set<std::string> seen;
    std::size_t index = 0;
    for (const nlohmann::json& row : *found) {
        FBattleObjectiveDef def;
        if (!ParseRow(row, index, def)) return std::nullopt;
        if (!seen.insert(def.id).second) {
            LOG_ERROR("battle_objectives[{}]: id '{}' is repeated.", index, def.id);
            return std::nullopt;
        }
        table.rows.push_back(std::move(def));
        ++index;
    }
    return table;
}

const FBattleObjectiveDef* PickBattleObjective(
    const FBattleObjectiveTable& table, std::uint32_t seed, int depth, int room) {
    if (table.rows.empty()) return nullptr;
    std::uint32_t h = seed ^ (static_cast<std::uint32_t>(depth) * 0x9E3779B1u) ^
                      (static_cast<std::uint32_t>(room) * 0x85EBCA77u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    return &table.rows[h % table.rows.size()];
}

bool IsBattleObjectiveFailed(const FBattleObjectiveDef& def, int rounds, bool anyAllyDown) {
    switch (def.kind) {
    case EBattleObjectiveKind::NoAllyDown: return anyAllyDown;
    case EBattleObjectiveKind::WinByRound: return rounds > def.rounds;
    }
    return true;
}

bool StakesEnabled() {
    // std::getenv, not SDL_getenv: SDL caches the environment, so a value a test sets later would not be seen.
    const char* value = std::getenv("GAME01P_STAKES");
    return value == nullptr || std::string_view(value) != "0";
}

void ApplyBattleStakes(entt::registry& registry) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    auto* combat = registry.ctx().find<FCombatRuntime>();
    if (run == nullptr || combat == nullptr) return;

    constexpr std::uint32_t kCritSalt = 0x5bd1e995u;
    combat->critRng.state = combat->rng.state ^ kCritSalt;
    if (combat->critRng.state == 0) combat->critRng.state = kCritSalt;

    const auto* assets = registry.ctx().find<FAssetManager>();
    const std::optional<gameplay::FTempoTuning> tuning =
        assets != nullptr ? gameplay::LoadTempoTuning(*assets) : std::nullopt;
    if (tuning) {
        combat->critChancePercent = tuning->critChancePercent;
        combat->critMultiplierPercent = tuning->critMultiplierPercent;
    } else {
        LOG_ERROR("battle stakes: crit tuning is unavailable; crits stay off this battle.");
    }

    registry.ctx().erase<FBattleObjective>();
    if (assets == nullptr) return;
    const std::optional<FBattleObjectiveTable> table = LoadBattleObjectives(*assets);
    if (!table) {
        LOG_ERROR("battle stakes: the battle objectives are unusable; this battle has none.");
        return;
    }
    const FBattleObjectiveDef* row = PickBattleObjective(*table, run->seed, run->depth, run->room);
    if (row == nullptr) return;
    registry.ctx().emplace<FBattleObjective>(FBattleObjective{*row});
    if (auto* log = registry.ctx().find<FCombatLog>()) {
        log->push("Objective: " + row->displayName + " - " + row->text + " (+" +
                  std::to_string(row->rewardMaterials) + " material).");
    }
}

} // namespace game

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace game {

struct FAssetManager;

enum class EBattleObjectiveKind { NoAllyDown, WinByRound };

struct FBattleObjectiveDef {
    std::string id;
    std::string displayName;
    std::string text;
    EBattleObjectiveKind kind{EBattleObjectiveKind::NoAllyDown};
    int rounds{0};            // WinByRound only
    int rewardMaterials{0};
};

struct FBattleObjectiveTable {
    std::vector<FBattleObjectiveDef> rows;
};

/// Registry context: the objective of the battle in progress. Absent = this battle has none.
/// `settled`/`met` are latched by ResolveRoomVictory so the HUD keeps the result after the stats reset.
struct FBattleObjective {
    FBattleObjectiveDef def;
    bool settled{false};
    bool met{false};
};

/// Reads `battle_objectives` from `data/dungeon`. Missing document or key = empty table.
/// nullopt + LOG_ERROR when the present value is malformed.
[[nodiscard]] std::optional<FBattleObjectiveTable> LoadBattleObjectives(const FAssetManager& assets);

[[nodiscard]] const FBattleObjectiveDef* PickBattleObjective(
    const FBattleObjectiveTable& table, std::uint32_t seed, int depth, int room);

[[nodiscard]] bool IsBattleObjectiveFailed(const FBattleObjectiveDef& def, int rounds, bool anyAllyDown);

/// False only when the environment variable GAME01P_STAKES is exactly "0".
[[nodiscard]] bool StakesEnabled();

/// Battle start wiring: crit tuning + crit RNG on FCombatRuntime, and this battle's FBattleObjective.
void ApplyBattleStakes(entt::registry& registry);

} // namespace game

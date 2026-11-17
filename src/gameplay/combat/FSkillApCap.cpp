#include "gameplay/combat/FSkillApCap.h"

#include "core/AssetManager.h"
#include "core/FAssetDigest.h"
#include "core/Logger.h"
#include "gameplay/data/FContentRegistry.h"

#include <nlohmann/json.hpp>

namespace game::gameplay {
namespace {

/// One optional integer balance key: absent keeps `out`'s default; a present value must be an integer in
/// [minimum, 1000]. Logs and returns false on a violation.
[[nodiscard]] bool ReadOptionalBalanceInt(
    const nlohmann::json& balance, const char* key, int minimum, int& out) {
    const auto found = balance.find(key);
    if (found == balance.end()) {
        return true;
    }
    if (!found->is_number_integer()) {
        LOG_ERROR("dungeon balance '{}' must be a whole number (WP-16 tempo tuning).", key);
        return false;
    }
    const int value = found->get<int>();
    if (value < minimum || value > 1000) {
        LOG_ERROR("dungeon balance '{}' is {} — outside {}..1000.", key, value, minimum);
        return false;
    }
    out = value;
    return true;
}

} // namespace

std::optional<FTempoTuning> LoadTempoTuning(const FAssetManager& assets) {
    const auto asset = assets.digest("data/dungeon");
    if (!asset) {
        // A content-only boot (tests, tooling) legitimately runs without the dungeon document: the two
        // keys are optional, so their document is too. The dungeon loader owns the hard requirement.
        LOG_WARN("tempo tuning: 'data/dungeon' is not loaded; the defaults apply.");
        return FTempoTuning{};
    }
    FTempoTuning tuning;
    const nlohmann::json& data = asset->json();
    const auto balance = data.find("balance");
    if (balance == data.end() || !balance->is_object()) {
        // The dungeon loader owns the required shape of `balance`; without it the two optional keys are
        // simply absent, which keeps every default.
        return tuning;
    }
    if (!ReadOptionalBalanceInt(*balance, "skill_ap_cap", 1, tuning.skillApCap) ||
        !ReadOptionalBalanceInt(*balance, "push_collision_damage", 0, tuning.pushCollisionDamage) ||
        !ReadOptionalBalanceInt(*balance, "crit_chance_percent", 0, tuning.critChancePercent) ||
        !ReadOptionalBalanceInt(*balance, "crit_multiplier_percent", 100, tuning.critMultiplierPercent)) {
        return std::nullopt;
    }
    if (tuning.critChancePercent > 100) {
        LOG_ERROR("dungeon balance crit_chance_percent is {} — a chance is at most 100.", tuning.critChancePercent);
        return std::nullopt;
    }
    return tuning;
}

bool ValidateSkillApCap(int cap, const FContentRegistry& content) {
    if (cap < 1) {
        LOG_ERROR("skill_ap_cap {} is below 1; every skill must be usable in one turn (30-decisions D-1).", cap);
        return false;
    }
    bool valid = true;
    for (const FSkillContent& skill : content.skills.rows) {
        if (skill.apCost > cap) {
            LOG_ERROR("content: skill '{}' costs {} AP, above skill_ap_cap {} — raise the data key or lower the skill.",
                      skill.id, skill.apCost, cap);
            valid = false;
        }
    }
    return valid;
}

bool ValidateSkillApCap(const FAssetManager& assets, const FContentRegistry& content) {
    const std::optional<FTempoTuning> tuning = LoadTempoTuning(assets);
    if (!tuning) {
        LOG_ERROR("tempo tuning is unusable; boot stopped (see the errors above).");
        return false;
    }
    return ValidateSkillApCap(tuning->skillApCap, content);
}

} // namespace game::gameplay

#include "gameplay/ai/FAiNodeRegistry.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "ecs/components/FActiveStatuses.h"
#include "gameplay/ai/FAiBlackboard.h"
#include "gameplay/ai/FAiLeaves.h"
#include "gameplay/ai/FAiWorld.h"
#include "gameplay/ai/FBtComposites.h"
#include "gameplay/ai/FBtDecorators.h"

namespace game::ai {

// ---------------------------------------------------------------------------------------------------------
// FNodeParams
// ---------------------------------------------------------------------------------------------------------

FNodeParams::FNodeParams(const nlohmann::json& node, std::string path) : node_(&node), path_(std::move(path)) {
    used_ = {"type", "children"}; // structural keys are handled by the tree builder
}

const nlohmann::json* FNodeParams::take(std::string_view key) {
    used_.emplace_back(key);
    const auto found = node_->find(std::string(key));
    return found == node_->end() ? nullptr : &*found;
}

void FNodeParams::fail(std::string message) {
    if (!error_) {
        error_ = path_ + ": " + std::move(message);
    }
}

int FNodeParams::integer(std::string_view key, int minimum, int maximum) {
    const nlohmann::json* value = take(key);
    if (value == nullptr) {
        fail("missing required number '" + std::string(key) + "'");
        return minimum;
    }
    if (!value->is_number_integer()) {
        fail("'" + std::string(key) + "' must be a whole number");
        return minimum;
    }
    const int number = value->get<int>();
    if (number < minimum || number > maximum) {
        fail("'" + std::string(key) + "' must be in [" + std::to_string(minimum) + ", " + std::to_string(maximum) + "]");
        return minimum;
    }
    return number;
}

int FNodeParams::integer(std::string_view key, int minimum, int maximum, int fallback) {
    if (node_->find(std::string(key)) == node_->end()) {
        used_.emplace_back(key);
        return fallback;
    }
    return integer(key, minimum, maximum);
}

bool FNodeParams::boolean(std::string_view key, bool fallback) {
    const nlohmann::json* value = take(key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_boolean()) {
        fail("'" + std::string(key) + "' must be true or false");
        return fallback;
    }
    return value->get<bool>();
}

std::string FNodeParams::text(std::string_view key) {
    const nlohmann::json* value = take(key);
    if (value == nullptr) {
        fail("missing required text '" + std::string(key) + "'");
        return {};
    }
    if (!value->is_string() || value->get_ref<const std::string&>().empty()) {
        fail("'" + std::string(key) + "' must be a non-empty string");
        return {};
    }
    return value->get<std::string>();
}

std::string FNodeParams::text(std::string_view key, std::string fallback) {
    if (node_->find(std::string(key)) == node_->end()) {
        used_.emplace_back(key);
        return fallback;
    }
    return text(key);
}

std::vector<std::string> FNodeParams::textList(std::string_view key) {
    std::vector<std::string> list;
    const nlohmann::json* value = take(key);
    if (value == nullptr) {
        return list;
    }
    if (!value->is_array()) {
        fail("'" + std::string(key) + "' must be an array of strings");
        return list;
    }
    for (const nlohmann::json& entry : *value) {
        if (!entry.is_string() || entry.get_ref<const std::string&>().empty()) {
            fail("'" + std::string(key) + "' entries must be non-empty strings");
            return {};
        }
        list.push_back(entry.get<std::string>());
    }
    return list;
}

std::optional<std::string> FNodeParams::finish() {
    if (error_) {
        return error_;
    }
    for (const auto& [key, value] : node_->items()) {
        if (std::ranges::find(used_, key) == used_.end()) {
            return path_ + ": unknown parameter '" + key + "'";
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------------------
// FAiNodeRegistry
// ---------------------------------------------------------------------------------------------------------

void FAiNodeRegistry::add(std::string name, int minChildren, int maxChildren, FFactory factory) {
    if (auto it = std::ranges::find(types_, name, &FType::name); it != types_.end()) {
        *it = FType{std::move(name), minChildren, maxChildren, std::move(factory)};
        return;
    }
    types_.push_back(FType{std::move(name), minChildren, maxChildren, std::move(factory)});
}

const FAiNodeRegistry::FType* FAiNodeRegistry::find(std::string_view name) const {
    const auto it = std::ranges::find(types_, name, &FType::name);
    return it == types_.end() ? nullptr : &*it;
}

namespace {

constexpr int kMaxChildren = 32;

/// Wraps a factory body: runs it, then reports parameter problems if the body left any.
template <typename TBody>
FAiNodeRegistry::FFactory Checked(TBody body) {
    return [body = std::move(body)](FNodeSpec& spec) -> FNodeBuild {
        FBtNodePtr node = body(spec);
        if (const auto problem = spec.params.finish()) {
            return std::unexpected(*problem);
        }
        return node;
    };
}

FSkillFilter ReadFilter(FNodeSpec& spec) {
    FNodeParams& params = spec.params;
    FSkillFilter filter;
    const std::string role = params.text("role", "any");
    if (const auto mask = ParseRoleMask(role)) {
        filter.roles = *mask;
    } else {
        params.fail("unknown role '" + role + "' (damage, heal, buff, debuff, hostile, friendly, any)");
    }
    filter.skillIds = params.textList("skills");
    for (const std::string& id : filter.skillIds) {
        if (spec.content.skills.find(id) == nullptr) {
            params.fail("unknown skill '" + id + "'");
        }
    }
    filter.tags = params.textList("tags");
    filter.selfOnly = params.boolean("self_only", false);
    filter.minHits = params.integer("min_hits", 1, 8, 1);
    return filter;
}

[[nodiscard]] FBtNodePtr Condition(std::string name, std::string detail, FCondition::FTest test) {
    return std::make_unique<FCondition>(std::move(name), std::move(detail), std::move(test));
}

[[nodiscard]] bool InRange(int value, int minimum, int maximum) { return value >= minimum && value <= maximum; }

/// Living units on the actor's own side, optionally counting the actor itself.
[[nodiscard]] int CountSide(const FBtContext& ctx, bool hostile, bool includeSelf) {
    int count = 0;
    for (const FUnitView& unit : ctx.world.units) {
        if ((unit.party == ctx.hostilePartySide) != hostile) continue;
        if (!hostile && !includeSelf && unit.entity == ctx.actor) continue;
        ++count;
    }
    return count;
}

void RegisterComposites(FAiNodeRegistry& registry) {
    registry.add("Sequence", 1, kMaxChildren, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        return std::make_unique<FSequence>(std::move(spec.children));
    }));
    registry.add("Selector", 1, kMaxChildren, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        return std::make_unique<FSelector>(std::move(spec.children));
    }));
    registry.add("ActiveSelector", 1, kMaxChildren, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        return std::make_unique<FActiveSelector>(std::move(spec.children));
    }));
    registry.add("Parallel", 1, kMaxChildren, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const auto policy = [&spec](std::string_view key) {
            const std::string value = spec.params.text(key);
            if (value != "one" && value != "all") {
                spec.params.fail("'" + std::string(key) + "' must be \"one\" or \"all\"");
            }
            return value == "all" ? EParallelPolicy::RequireAll : EParallelPolicy::RequireOne;
        };
        const EParallelPolicy success = policy("success");
        const EParallelPolicy failure = policy("failure");
        return std::make_unique<FParallel>(std::move(spec.children), success, failure);
    }));
}

void RegisterDecorators(FAiNodeRegistry& registry) {
    registry.add("Repeat", 1, 1, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const int times = spec.params.integer("times", 1, 8);
        return std::make_unique<FRepeat>(std::move(spec.children.front()), times);
    }));
    registry.add("Inverter", 1, 1, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        return std::make_unique<FInverter>(std::move(spec.children.front()));
    }));
    registry.add("Succeeder", 1, 1, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        return std::make_unique<FSucceeder>(std::move(spec.children.front()));
    }));
    registry.add("Watch", 1, 1, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        return std::make_unique<FWatch>(std::move(spec.children.front()));
    }));
    registry.add("Cooldown", 1, 1, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        std::string key = spec.params.text("key");
        const int turns = spec.params.integer("turns", 1, 20);
        return std::make_unique<FCooldown>(std::move(spec.children.front()), std::move(key), turns);
    }));
    registry.add("Once", 1, 1, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        std::string key = spec.params.text("key");
        return std::make_unique<FOnce>(std::move(spec.children.front()), std::move(key));
    }));
}

void RegisterActions(FAiNodeRegistry& registry) {
    registry.add("UseSkill", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        return std::make_unique<FUseSkill>(ReadFilter(spec));
    }));
    registry.add("Engage", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        return std::make_unique<FEngage>(ReadFilter(spec));
    }));
    registry.add("Retreat", 0, 0, Checked([](FNodeSpec&) -> FBtNodePtr { return std::make_unique<FRetreat>(); }));
    registry.add("Wait", 0, 0, Checked([](FNodeSpec&) -> FBtNodePtr { return std::make_unique<FWait>(); }));
}

void RegisterConditions(FAiNodeRegistry& registry) {
    registry.add("SelfHpBelow", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const int pct = spec.params.integer("pct", 1, 100);
        return Condition("SelfHpBelow", std::to_string(pct) + "%", [pct](FBtContext& ctx) {
            const FUnitView* self = ctx.world.self();
            return self != nullptr && self->hp * 100 < pct * self->hpMax;
        });
    }));
    registry.add("SelfHpAtLeast", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const int pct = spec.params.integer("pct", 1, 100);
        return Condition("SelfHpAtLeast", std::to_string(pct) + "%", [pct](FBtContext& ctx) {
            const FUnitView* self = ctx.world.self();
            return self != nullptr && self->hp * 100 >= pct * self->hpMax;
        });
    }));
    registry.add("AllyHpBelow", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const int pct = spec.params.integer("pct", 1, 100);
        const bool includeSelf = spec.params.boolean("include_self", true);
        return Condition("AllyHpBelow", std::to_string(pct) + "%", [pct, includeSelf](FBtContext& ctx) {
            for (const FUnitView& unit : ctx.world.units) {
                if (unit.party == ctx.hostilePartySide) continue;
                if (!includeSelf && unit.entity == ctx.actor) continue;
                if (unit.hp * 100 < pct * unit.hpMax) return true;
            }
            return false;
        });
    }));
    registry.add("EnemyWithin", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const int distance = spec.params.integer("distance", 1, 16);
        return Condition("EnemyWithin", std::to_string(distance), [distance](FBtContext& ctx) {
            const FUnitView* self = ctx.world.self();
            return self != nullptr && NearestHostileDistance(ctx, self->cell) <= distance;
        });
    }));
    registry.add("CanUseSkill", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        FSkillFilter filter = ReadFilter(spec);
        std::string detail = DescribeFilter(filter);
        return Condition("CanUseSkill", std::move(detail), [filter = std::move(filter)](FBtContext& ctx) {
            const FUnitView* self = ctx.world.self();
            return self != nullptr && FindBestOption(ctx, self->cell, filter, true).has_value();
        });
    }));
    registry.add("SelfHasStatus", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        std::string status = spec.params.text("status");
        if (!status.empty() && spec.content.statuses.find(status) == nullptr) {
            spec.params.fail("unknown status '" + status + "'");
        }
        return Condition("SelfHasStatus", status, [status](FBtContext& ctx) {
            const FUnitView* self = ctx.world.self();
            return self != nullptr && std::ranges::any_of(self->effects, [&](const FStatusEffect& effect) {
                       return effect.statusId == status;
                   });
        });
    }));
    registry.add("TurnIs", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const int every = spec.params.integer("every", 1, 20);
        const int offset = spec.params.integer("offset", 0, 19, 0);
        if (offset >= every) {
            spec.params.fail("'offset' must be smaller than 'every'");
        }
        return Condition("TurnIs", "every " + std::to_string(every) + "+" + std::to_string(offset),
                         [every, offset](FBtContext& ctx) {
                             return ctx.blackboard.turnsTaken >= 1 &&
                                    (ctx.blackboard.turnsTaken - 1) % every == offset;
                         });
    }));
    registry.add("TurnAtLeast", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const int turn = spec.params.integer("turn", 1, 99);
        return Condition("TurnAtLeast", std::to_string(turn),
                         [turn](FBtContext& ctx) { return ctx.blackboard.turnsTaken >= turn; });
    }));
    registry.add("AlliesAlive", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const int minimum = spec.params.integer("min", 0, 16, 0);
        const int maximum = spec.params.integer("max", 0, 16, 16);
        return Condition("AlliesAlive", std::to_string(minimum) + ".." + std::to_string(maximum),
                         [minimum, maximum](FBtContext& ctx) {
                             return InRange(CountSide(ctx, false, false), minimum, maximum);
                         });
    }));
    registry.add("EnemiesAlive", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const int minimum = spec.params.integer("min", 0, 16, 0);
        const int maximum = spec.params.integer("max", 0, 16, 16);
        return Condition("EnemiesAlive", std::to_string(minimum) + ".." + std::to_string(maximum),
                         [minimum, maximum](FBtContext& ctx) {
                             return InRange(CountSide(ctx, true, false), minimum, maximum);
                         });
    }));
    // WP-15b: the action-side boss phase (`FBossGoals::behaviorPhase` — promoted a round after the
    // transition). A unit without FBossGoals is never at a phase, so ai.boss keeps its pre-WP-15 behavior
    // for the fallback guardian.
    registry.add("BossPhaseAtLeast", 0, 0, Checked([](FNodeSpec& spec) -> FBtNodePtr {
        const int phase = spec.params.integer("phase", 1, 99);
        return Condition("BossPhaseAtLeast", "phase >= " + std::to_string(phase),
                         [phase](FBtContext& ctx) { return SelfBossPhaseAtLeast(ctx, phase); });
    }));
}

} // namespace

FAiNodeRegistry FAiNodeRegistry::builtin() {
    FAiNodeRegistry registry;
    RegisterComposites(registry);
    RegisterDecorators(registry);
    RegisterActions(registry);
    RegisterConditions(registry);
    return registry;
}

} // namespace game::ai

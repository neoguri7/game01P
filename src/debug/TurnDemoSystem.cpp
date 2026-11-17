#include "debug/TurnDemo.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FTurnParticipant.h"
#include "ecs/systems/TurnSystem.h"
#include <algorithm>
#include <array>
#include <type_traits>

namespace game {
namespace {

void DestroyOwnedUnits(entt::registry& registry, FTurnDemo& demo) {
    for (const auto& unit : demo.units) {
        if (registry.valid(unit.entity)) registry.destroy(unit.entity);
    }
    demo.units.clear();
}

void AddUnit(entt::registry& registry, FTurnDemo& demo, std::string label, std::int32_t speed) {
    const auto entity = registry.create();
    registry.emplace<FSpeed>(entity, speed);
    registry.emplace<FTurnParticipant>(entity);
    demo.units.push_back({entity, std::move(label)});
}

bool IsOwned(const FTurnDemo& demo, entt::entity entity) {
    return std::ranges::any_of(demo.units, [entity](const auto& unit) { return unit.entity == entity; });
}

} // namespace

void TurnDemoSystem::onRegister(entt::registry& registry) {
    if (!registry.ctx().contains<FTurnDemo>()) registry.ctx().emplace<FTurnDemo>();
}

void TurnDemoSystem::update(entt::registry& registry, float /*deltaTime*/) {
    auto* demo = registry.ctx().find<FTurnDemo>();
    auto* runtime = registry.ctx().find<FTurnRuntime>();
    if (!demo || !runtime) return;

    while (!demo->requests.empty()) {
        auto request = std::move(demo->requests.front());
        demo->requests.pop_front();
        std::visit([&](const auto& typed) {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, FResetTurnDemo>) {
                runtime->stop();
                runtime->lastError.reset();
                demo->requests.clear();
                demo->lastError.reset();
                DestroyOwnedUnits(registry, *demo);
                constexpr std::array<std::int32_t, 5> speeds{30, 20, 20, 10, 0};
                for (std::size_t index = 0; index < speeds.size(); ++index) {
                    AddUnit(registry, *demo, std::string(1, static_cast<char>('A' + index)), speeds[index]);
                }
                demo->nextLabel = 1;
            } else if constexpr (std::is_same_v<T, FAddTurnDemoUnit>) {
                if (typed.speed < 0) {
                    demo->lastError = ETurnError::InvalidSpeed;
                } else {
                    AddUnit(registry, *demo, "Extra " + std::to_string(demo->nextLabel++), typed.speed);
                }
            } else {
                if (!IsOwned(*demo, typed.entity) || !registry.valid(typed.entity)) {
                    demo->lastError = ETurnError::InvalidEntity;
                    return;
                }
                if constexpr (std::is_same_v<T, FSetTurnDemoSpeed>) {
                    const auto result = SetTurnUnitSpeed(registry, typed.entity, typed.speed);
                    if (!result) demo->lastError = result.error();
                } else if constexpr (std::is_same_v<T, FSetTurnDemoParticipation>) {
                    if (typed.participating) registry.get_or_emplace<FTurnParticipant>(typed.entity);
                    else registry.remove<FTurnParticipant>(typed.entity);
                } else {
                    registry.destroy(typed.entity);
                }
            }
        }, request);
        runtime->controller.synchronize(registry);
    }
}

std::string TurnDemoLabel(const entt::registry& registry, entt::entity entity) {
    const bool valid = registry.valid(entity);
    if (const auto* demo = registry.ctx().find<FTurnDemo>()) {
        for (const auto& unit : demo->units) {
            if (unit.entity == entity) return unit.label + (valid ? "" : " (deleted)");
        }
    }
    return std::to_string(entt::to_integral(entity)) + (valid ? "" : " (deleted)");
}

void ShutdownTurnDemo(entt::registry& registry) {
    if (auto* runtime = registry.ctx().find<FTurnRuntime>()) runtime->stop();
    if (auto* demo = registry.ctx().find<FTurnDemo>()) {
        demo->requests.clear();
        DestroyOwnedUnits(registry, *demo);
    }
    registry.ctx().erase<FTurnDemo>();
}

} // namespace game

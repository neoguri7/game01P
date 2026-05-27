#include "gameplay/tactical_d20/TacticalD20VisualFeedbackSystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FTacticalD20CommandFeedback.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"

#include <string>
#include <vector>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

float FeedbackLifetime(entt::registry& registry, bool accepted) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const auto feedback = config ? config->visualFeedback : FTacticalD20VisualFeedbackConfig{};
    return accepted ? feedback.acceptedSeconds : feedback.rejectedSeconds;
}

void AddFeedback(entt::registry& registry, const FTacticalD20CommandDropValidatedEvent& event) {
    if (event.token == entt::null || !registry.valid(event.token)) return;
    const bool accepted = event.valid;
    const std::string message = accepted ? "accepted" : event.invalidReason;
    registry.emplace_or_replace<FTacticalD20CommandFeedback>(event.token, message, FeedbackLifetime(registry, accepted), accepted);
}

void UpdateFeedback(entt::registry& registry, float dt) {
    std::vector<entt::entity> expired;
    auto view = registry.view<FTacticalD20CommandFeedback>();
    for (auto entity : view) {
        auto& feedback = view.get<FTacticalD20CommandFeedback>(entity);
        feedback.lifetimeSeconds -= dt;
        if (feedback.lifetimeSeconds <= 0.f) expired.push_back(entity);
    }
    for (auto entity : expired) registry.remove<FTacticalD20CommandFeedback>(entity);
}

} // namespace

void TacticalD20VisualFeedbackSystem::update(entt::registry& registry, float dt) {
    ZoneScopedN("TacticalD20VisualFeedbackSystem::update");
    if (const auto* bus = registry.ctx().find<FEventBus>()) {
        for (const auto& event : bus->frameEvents<FTacticalD20CommandDropValidatedEvent>()) AddFeedback(registry, event);
    }
    UpdateFeedback(registry, dt);
}

} // namespace game

#include "ecs/systems/TurnSystem.h"
#include "core/events/FEventBus.h"
#include <type_traits>

namespace game {
namespace {

void FlushEvents(entt::registry& registry, FTurnRuntime& runtime) {
	auto* bus = registry.ctx().find<FEventBus>();
	for (const auto& event : runtime.controller.takeEvents()) {
		if (const auto* ended = std::get_if<FTurnSessionEnded>(&event);
			ended && ended->reason == ETurnSessionEndReason::InvalidSpeed) {
			runtime.lastError = ETurnError::InvalidSpeed;
		}
		if (bus) {
			std::visit([bus](const auto& typed) { bus->queueFrame(typed); }, event);
		}
	}
}

} // namespace

void ShutdownTurnRuntime(entt::registry& registry) {
	if (auto* runtime = registry.ctx().find<FTurnRuntime>()) runtime->stop();
	registry.ctx().erase<FTurnRuntime>();
}

void ecs::TurnSystem::onRegister(entt::registry& registry) {
	if (!registry.ctx().contains<FTurnRuntime>()) registry.ctx().emplace<FTurnRuntime>();
}

void ecs::TurnSystem::update(entt::registry& registry, float /*deltaTime*/) {
	auto* runtime = registry.ctx().find<FTurnRuntime>();
	if (!runtime) return;
	runtime->controller.synchronize(registry);
	FlushEvents(registry, *runtime);

	while (!runtime->requests.empty()) {
		auto request = std::move(runtime->requests.front());
		runtime->requests.pop_front();
		std::visit([&](const auto& typed) {
			using T = std::decay_t<decltype(typed)>;
			if constexpr (std::is_same_v<T, FStopTurnSession>) {
				runtime->stop();
			} else {
				auto result = [&]() -> std::expected<void, ETurnError> {
					if constexpr (std::is_same_v<T, FStartTurnSession>) {
						return runtime->controller.start(registry, typed.seed);
					} else {
						return runtime->controller.endTurn(registry, typed.token);
					}
				}();
				if (!result) runtime->lastError = result.error();
			}
		}, request);
		FlushEvents(registry, *runtime);
	}
}

} // namespace game

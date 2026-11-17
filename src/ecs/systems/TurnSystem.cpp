#include "ecs/systems/TurnSystem.h"
#include "core/events/FEventBus.h"
#include "gameplay/combat/FBattleFlow.h"
#include <algorithm>
#include <type_traits>
#include <utility>

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

/// WP-05a: true when the deque already holds a deferred FEndTurn for the same turn. The HUD's end-turn
/// button can queue the same token twice; the second copy is redundant.
[[nodiscard]] bool HasQueuedEndTurn(const std::deque<FTurnRequest>& requests, const FTurnToken& token) {
	return std::ranges::any_of(requests, [&token](const FTurnRequest& request) {
		const auto* end = std::get_if<FEndTurn>(&request);
		return end != nullptr && end->token == token;
	});
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

	// WP-05a: an FEndTurn is a *game* decision, so it waits for the presentation to settle. The wait is
	// level-triggered: the predicate is evaluated every frame, nothing is stored. Only the front request is
	// rotated to the back; a Start/Stop behind a deferred EndTurn is still processed in this update. The
	// loop is bounded by the initial size, so a fully deferred queue cannot spin here, and runtime->stop()
	// clears the deque — a deferred EndTurn is disposed with it.
	const bool settled = IsPresentationSettled(registry);
	std::size_t remaining = runtime->requests.size();
	while (remaining-- > 0 && !runtime->requests.empty()) {
		if (!settled && std::holds_alternative<FEndTurn>(runtime->requests.front())) {
			auto deferred = std::move(runtime->requests.front());
			runtime->requests.pop_front();
			if (!HasQueuedEndTurn(runtime->requests, std::get<FEndTurn>(deferred).token)) {
				runtime->requests.push_back(std::move(deferred));
			}
			continue;
		}
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

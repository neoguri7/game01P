#pragma once

#include <entt/entt.hpp>
#include <algorithm>
#include <stdexcept>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace game {

class FScene {
public:
	FScene(entt::registry& registry, std::string name)
		: registry(registry), name(std::move(name)) {}

	~FScene() { Unload(); }

	FScene(const FScene&) = delete;
	FScene& operator=(const FScene&) = delete;
	FScene(FScene&&) = delete;
	FScene& operator=(FScene&&) = delete;

	void Load() noexcept { loaded = true; }

	void Unload() noexcept {
		for (const auto entity : entities) {
			if (registry.valid(entity)) registry.destroy(entity);
		}
		entities.clear();
		loaded = false;
	}

	[[nodiscard]] bool IsLoaded() const noexcept { return loaded; }
	[[nodiscard]] const std::string& GetName() const noexcept { return name; }

	[[nodiscard]] std::optional<entt::entity> CreateEntity() {
		if (!loaded) return std::nullopt;
		const auto entity = registry.create();
		try {
			entities.push_back(entity);
		} catch (...) {
			registry.destroy(entity);
			throw;
		}
		return entity;
	}

	template<typename Component, typename... Args>
	Component& Emplace(entt::entity entity, Args&&... args) {
		if (!loaded || !registry.valid(entity) || std::ranges::find(entities, entity) == entities.end()) {
			throw std::logic_error("component can only be added to an entity owned by a loaded scene");
		}
		return registry.emplace<Component>(entity, std::forward<Args>(args)...);
	}

private:
	entt::registry& registry;
	std::string name;
	std::vector<entt::entity> entities;
	bool loaded{false};
};

} // namespace game

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

namespace game {

/// One registered modal view: a catalog id and a draw function. The view is a plain function so the
/// registry stays in the presentation layer and the host states only call it.
struct FModalView {
    std::string id;
    void (*render)(entt::registry&) = nullptr;
};

/// The registry is context-owned. Registering twice is a wiring bug: the first registration wins and the
/// duplicate is logged.
struct FModalViewRegistry {
    std::vector<FModalView> views;
};

/// A view's "close" button asks its host to pop on the next Update. The host consumes the flag; the view
/// never pops a state itself (the app layer owns the stack).
struct FModalViewCloseRequest {
    bool requested{false};
};

/// Registers the run-phase views (reward, event, camp, runEnd). Called once from InitializeDungeonUi.
void RegisterModalViews(entt::registry& registry);
/// Registers one view. Duplicate ids are logged and the first registration wins.
void RegisterModalView(entt::registry& registry, std::string_view id, void (*render)(entt::registry&));
/// The view registered under `id`, or nullptr.
[[nodiscard]] const FModalView* FindModalView(const entt::registry& registry, std::string_view id);

/// Asks the current host to close on its next Update.
void RequestModalViewClose(entt::registry& registry);
/// True once, when a close request was posted since the last call; clears the request.
[[nodiscard]] bool TakeModalViewCloseRequest(entt::registry& registry);

} // namespace game

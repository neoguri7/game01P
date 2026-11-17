#pragma once

#include <optional>
#include <string>

#include <entt/entt.hpp>

namespace game {

struct FBossIntroTiming { float holdSeconds{2.0f}; float fadeSeconds{0.6f}; };
struct FBossIntroState { std::string name; std::string note; float elapsed{0.f}; bool done{false}; };

/// data/flow `boss_intro`, loaded once into the context. Missing file/block, non-number, or a value outside
/// [0, 10] -> the defaults above and one LOG_WARN.
[[nodiscard]] const FBossIntroTiming& BossIntroTiming(entt::registry& registry);
/// The card of the battle at ordinal run.room of the current dungeon; nullopt when there is no boss slot.
[[nodiscard]] std::optional<FBossIntroState> MakeBossIntro(const entt::registry& registry);
/// Erases any FBossIntroState, then emplaces MakeBossIntro's card unless CurrentSettings(registry).skipAnimations.
void BeginBossIntro(entt::registry& registry);
/// Erases FBossIntroState.
void EndBossIntro(entt::registry& registry);
/// elapsed += max(0, dt); done = true when dismiss, skipAnimations, or elapsed >= hold + fade.
void AdvanceBossIntro(FBossIntroState& intro, const FBossIntroTiming& timing, float deltaSeconds, bool dismiss,
                      bool skipAnimations);
/// 0 when done; 1 while elapsed <= hold; then clamp(1 - (elapsed - hold) / fade, 0, 1) (fade 0 -> 0).
[[nodiscard]] float BossIntroAlpha(const FBossIntroState& intro, const FBossIntroTiming& timing);
/// Advances the context card with dismiss = mouse click / Confirm / Cancel this frame and the skip setting;
/// erases it once done. No-op without a card.
void TickBossIntro(entt::registry& registry, float deltaSeconds);
/// Draws the card on the foreground draw list (no window, takes no input). No-op without a card or window.
void DrawBossIntro(entt::registry& registry);

} // namespace game

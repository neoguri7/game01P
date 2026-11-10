#pragma once

#include <entt/entt.hpp>

namespace game {

/// The path strip as one line of ImGui text in a borderless window whose top-right corner is (rightX, topY).
/// `scale` is the HUD scale (font size 12 * scale). Nothing is drawn for an empty strip.
void DrawPathStrip(entt::registry& registry, float rightX, float topY, float scale);

/// The fork panel: SeparatorText("CHOOSE A PATH"), the line "Party HP <a>/<b> | Materials <n>", then per
/// option a ui::Button(title, {-1, 38}) and a TextWrapped detail. Returns 0 or 1 for the clicked option,
/// -1 otherwise. `disabled` greys both buttons. Draws nothing and returns -1 when no fork is pending.
[[nodiscard]] int DrawForkPanel(entt::registry& registry, bool disabled);

} // namespace game

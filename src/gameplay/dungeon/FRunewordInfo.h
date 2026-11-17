#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FSkillLoadout.h"
#include "gameplay/data/FContentRegistry.h"

namespace game {

struct FRunewordEntry {
    std::string id;
    std::string name;
    std::string text;
    std::vector<std::string> runeNames;  // display names, in requires_runes order
};
/// Effect-bearing engines of which the run owns at least one rune (IsRuneOwned: inventory or socketed), in data order.
[[nodiscard]] std::vector<FRunewordEntry> KnownRunewords(entt::registry& registry);
/// "<name>: <rune A> + <rune B>: <text>"
[[nodiscard]] std::string RunewordRecipeLine(const FRunewordEntry& entry);
/// For a rune offered on a card. Walks the effect-bearing engines that contain runeId, in data order:
/// the first whose other runes are all owned gives "Runeword: completes <name>: <text>";
/// if none completes but at least one contains the rune: "Runeword ingredient"; else "".
/// Whether runeId itself is already owned makes no difference.
[[nodiscard]] std::string RunewordCardLine(entt::registry& registry, std::string_view runeId);
/// For a skill slot. ActiveRuneword non-null: "Runeword: <name>: <text>". Else the first effect-bearing
/// engine with at least one rune in the slot: "Runeword <held>/<total>: <name> needs <missing display names, ", "-joined>".
/// Else "".
[[nodiscard]] std::string RunewordSlotLine(const gameplay::FContentRegistry& content, const FSkillSlot& slot);

} // namespace game

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

namespace game {

/// Room type keys as authored in `data/dungeon themes[].dungeons[].rooms[].type`. `battle` is implicit: it
/// is the existing battle flow and never sits in the table.
inline constexpr std::string_view kRoomTypeBattle = "battle";
inline constexpr std::string_view kRoomTypeEvent = "event";
/// The slot key the `event` type requires (the authored JSON key of FRunRoomSlot::eventPool).
inline constexpr std::string_view kRoomSlotKeyEventPool = "event_pool";

/// One modal-shaped room type (WP-09 §3): the modal the run flow opens for the slot and the slot key the
/// data validation requires on every slot of this type.
struct FRoomTypeDef {
    std::string id;
    std::string modalId;
    std::string requiredSlotKey;
};

/// The room type table. Ctx-owned (A8: no global mutable state); filled only by RegisterRoomTypes at the
/// composition root — never by static self-registration (the runtime is a static library and the linker
/// drops unreferenced registrars).
///
/// Recipe boundary (honest): a new **modal-shaped** type of the same form as `event` = one line in
/// RegisterRoomTypes + one view + one data line. A type with its **own behaviour** (its own progression or
/// choice rules) edits the FRunFlow call site, and when its host sits between modals it also extends the
/// FRunFlowStateMap modal-host list.
struct FRoomTypeTable {
    std::vector<FRoomTypeDef> types;
};

/// Installs (or replaces) the table with the W9 set: `event` only.
void RegisterRoomTypes(entt::registry& registry);

/// The registered type, or nullptr (also for `battle`, which is implicit, and before RegisterRoomTypes).
[[nodiscard]] const FRoomTypeDef* FindRoomType(const entt::registry& registry, std::string_view id);

/// `battle` || a registered type.
[[nodiscard]] bool IsKnownRoomType(const entt::registry& registry, std::string_view id);

} // namespace game

#include "gameplay/dungeon/FRoomTypeRegistry.h"

#include <utility>

#include "gameplay/dungeon/FRunFlow.h"

namespace game {

void RegisterRoomTypes(entt::registry& registry) {
    // W9 registers one modal-shaped type. The modal id is the run-flow constant (never re-spelled here)
    // and the required slot key is the authored JSON key of FRunRoomSlot::eventPool, so the data
    // validation can check the slot shape without knowing the type.
    FRoomTypeTable table;
    table.types.push_back(FRoomTypeDef{
        std::string(kRoomTypeEvent), std::string(kRunModalEvent), std::string(kRoomSlotKeyEventPool)});
    // Replace any previous table: tests and debug reloads re-register, and a stale table would keep
    // dispatching to types this build no longer knows.
    registry.ctx().insert_or_assign<FRoomTypeTable>(std::move(table));
}

const FRoomTypeDef* FindRoomType(const entt::registry& registry, std::string_view id) {
    const FRoomTypeTable* table = registry.ctx().find<FRoomTypeTable>();
    if (table == nullptr) return nullptr;
    for (const FRoomTypeDef& type : table->types) {
        if (type.id == id) return &type;
    }
    // Unknown ids and `battle` (implicit, never in the table) both answer nullptr.
    return nullptr;
}

bool IsKnownRoomType(const entt::registry& registry, std::string_view id) {
    return id == kRoomTypeBattle || FindRoomType(registry, id) != nullptr;
}

} // namespace game

#include "states/FStateCatalog.h"

#include <utility>

#include "core/Logger.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberBriefingState.h"
#include "states/FEmberHubState.h"
#include "states/FEmberModalHostState.h"
#include "states/FEmberPreparationState.h"
#include "states/FEmberRunEndState.h"
#include "states/FEmberTitleState.h"

namespace game {

bool FStateCatalog::add(std::string_view id, FFactory factory) {
    if (id.empty()) {
        LOG_ERROR("FStateCatalog::add rejected an empty state id.");
        return false;
    }
    if (!factory) {
        LOG_ERROR("FStateCatalog::add rejected an empty factory for '{}'.", id);
        return false;
    }
    if (find(id) != nullptr) {
        LOG_ERROR("FStateCatalog::add rejected duplicate state id '{}'.", id);
        return false;
    }
    entries.push_back({std::string(id), std::move(factory)});
    return true;
}

const FStateCatalog::FFactory* FStateCatalog::find(std::string_view id) const {
    for (const auto& entry : entries) {
        if (entry.id == id) return &entry.factory;
    }
    return nullptr;
}

std::unique_ptr<FBaseState> FStateCatalog::create(std::string_view id) const {
    const FFactory* factory = find(id);
    return factory != nullptr ? (*factory)() : nullptr;
}

bool FStateCatalog::contains(std::string_view id) const {
    return find(id) != nullptr;
}

std::vector<std::string> FStateCatalog::ids() const {
    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const auto& entry : entries) names.push_back(entry.id);
    return names;
}

void FStateCatalog::setStartIds(std::string start, std::string debugBattle) {
    startStateId = std::move(start);
    debugBattleStateId = std::move(debugBattle);
}

FStateCatalog FStateCatalog::MakeEmber() {
    FStateCatalog catalog;
    (void)catalog.add(kEmberTitleId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FEmberTitleState>()); });
    (void)catalog.add(kEmberHubId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FEmberHubState>()); });
    (void)catalog.add(kEmberBattleId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FEmberBattleState>()); });
    (void)catalog.add(kEmberPrepareId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FEmberPreparationState>()); });
    (void)catalog.add(kEmberBriefingId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FEmberBriefingState>()); });
    (void)catalog.add(kEmberRunEndId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FEmberRunEndState>()); });
    (void)catalog.add(kRunModalReward, [] {
        return std::unique_ptr<FBaseState>(std::make_unique<FEmberModalHostState>(std::string(kRunModalReward), EHostMode::RunPhase));
    });
    (void)catalog.add(kRunModalEvent, [] {
        return std::unique_ptr<FBaseState>(std::make_unique<FEmberModalHostState>(std::string(kRunModalEvent), EHostMode::RunPhase));
    });
    (void)catalog.add(kRunModalCamp, [] {
        return std::unique_ptr<FBaseState>(std::make_unique<FEmberModalHostState>(std::string(kRunModalCamp), EHostMode::RunPhase));
    });
    (void)catalog.add(kEmberSettingsId, [] {
        return std::unique_ptr<FBaseState>(std::make_unique<FEmberModalHostState>(std::string(kEmberSettingsId), EHostMode::Menu));
    });
    (void)catalog.add(kEmberCompendiumId, [] {
        return std::unique_ptr<FBaseState>(std::make_unique<FEmberModalHostState>(std::string(kEmberCompendiumId), EHostMode::Menu));
    });
    // [room types] A new room type appends its host here; its view registers in FModalViewRegistry.cpp.
    catalog.setStartIds(std::string(kEmberTitleId), std::string(kEmberBattleId));
    return catalog;
}

} // namespace game

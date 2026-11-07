#include "states/FStateCatalog.h"

#include <utility>

#include "core/Logger.h"
#include "states/FBattleState.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberHubState.h"
#include "states/FEmberTitleState.h"
#include "states/FHubState.h"
#include "states/FTitleState.h"

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

FStateTransition FStateCatalog::fromLegacy(EStateTransition legacy) const {
    switch (legacy) {
    case EStateTransition::ToTitle:
        return {EStateChangeKind::ReplaceTop, legacyIds.title};
    case EStateTransition::ToHub:
        return {EStateChangeKind::ReplaceTop, legacyIds.hub};
    case EStateTransition::ToBattle:
    case EStateTransition::ToPrototype:
        return {EStateChangeKind::ReplaceTop, legacyIds.battle};
    case EStateTransition::Quit:
        return {EStateChangeKind::Quit, {}};
    case EStateTransition::None:
    case EStateTransition::ToGameOver:
        break;
    }
    return {};
}

FStateCatalog FStateCatalog::MakeEmber() {
    FStateCatalog catalog;
    (void)catalog.add(kEmberTitleId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FEmberTitleState>()); });
    (void)catalog.add(kEmberHubId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FEmberHubState>()); });
    (void)catalog.add(kEmberBattleId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FEmberBattleState>()); });
    catalog.setStartIds(std::string(kEmberTitleId), std::string(kEmberBattleId));
    catalog.legacyIds = {std::string(kEmberTitleId), std::string(kEmberHubId), std::string(kEmberBattleId)};
    return catalog;
}

FStateCatalog FStateCatalog::MakeForest() {
    FStateCatalog catalog;
    (void)catalog.add(kForestTitleId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FTitleState>()); });
    (void)catalog.add(kForestHubId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FHubState>()); });
    (void)catalog.add(kForestBattleId, [] { return std::unique_ptr<FBaseState>(std::make_unique<FBattleState>()); });
    catalog.setStartIds(std::string(kForestTitleId), std::string(kForestBattleId));
    catalog.legacyIds = {std::string(kForestTitleId), std::string(kForestHubId), std::string(kForestBattleId)};
    return catalog;
}

} // namespace game

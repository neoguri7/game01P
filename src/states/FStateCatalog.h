#pragma once

#include "states/FBaseState.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace game {

/// App-flow state registry: id -> factory.
///
/// The catalog is the only owner of state ids. Engine and GameStateMachine pass ids through verbatim and
/// never assemble them ("prefix + .battle" style); a state that needs a target id names it in its own header.
///
/// Registration is explicit inside MakeEmber()/MakeForest(). No static self-registration: game01P_runtime is a
/// STATIC library, so the linker drops an unreferenced .obj and its initializer would silently disappear.
class FStateCatalog {
public:
    using FFactory = std::function<std::unique_ptr<FBaseState>()>;

    /// Register a factory under `id`. Empty ids, empty factories and duplicate ids are rejected (logged).
    bool add(std::string_view id, FFactory factory);

    /// Instantiate the state registered under `id`; nullptr when the id is unknown.
    [[nodiscard]] std::unique_ptr<FBaseState> create(std::string_view id) const;

    [[nodiscard]] bool contains(std::string_view id) const;

    /// Registered ids in registration order (diagnostics/tests).
    [[nodiscard]] std::vector<std::string> ids() const;

    /// Id of the state a normal boot starts in. Engine uses it verbatim.
    [[nodiscard]] std::string_view startId() const noexcept { return startStateId; }

    /// Id of the state GAME01P_START=battle starts in. Engine uses it verbatim (no id assembly).
    [[nodiscard]] std::string_view debugBattleId() const noexcept { return debugBattleStateId; }

    /// Set the two boot ids (normal start, GAME01P_START=battle). MakeEmber()/MakeForest() use it; a custom
    /// catalog (tests, future modes) needs it to be bootable through the Engine.
    void setStartIds(std::string start, std::string debugBattle);

    /// WP-01a bridge — deleted in WP-01b: map the legacy closed enum onto (kind, catalog id).
    /// Every legacy transition replaces the top; the catalog is the only place that knows the mode's ids.
    [[nodiscard]] FStateTransition fromLegacy(EStateTransition legacy) const;

    static FStateCatalog MakeEmber();
    static FStateCatalog MakeForest();

private:
    struct FEntry {
        std::string id;
        FFactory factory;
    };

    /// WP-01a bridge — deleted in WP-01b: the ids the closed EStateTransition enum still means for this mode.
    /// These are "the title" / "the hub" / "the battle", which today coincide with the boot ids but are not
    /// defined by them.
    struct FLegacyIds {
        std::string title;
        std::string hub;
        std::string battle;
    };

    [[nodiscard]] const FFactory* find(std::string_view id) const;

    std::vector<FEntry> entries;
    std::string startStateId;
    std::string debugBattleStateId;
    FLegacyIds legacyIds;
};

} // namespace game

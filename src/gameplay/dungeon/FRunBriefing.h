#pragma once

#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FRunPath.h"

namespace game {

/// The route node kinds (the path strip's kinds plus the entrance). Pictures are chosen in presentation
/// (NodeIconId, FFlowArt.h); the model carries no asset ids.
enum class EBriefingNodeKind { Entrance, Battle, Elite, Event, Mystery, Boss, Camp };

struct FBriefingNode {
    EBriefingNodeKind kind{EBriefingNodeKind::Battle};
    std::string label;                         // "Entrance" | the FPathNode label | "Camp"
    std::optional<EBriefingNodeKind> altKind;  // the undecided fork's variant kind; nullopt otherwise
    std::string altLabel;                      // the undecided fork's variant label ("Elite: Thorned" | "?"); "" otherwise
};

struct FBriefingDungeon {
    std::string name;
    std::string bossMark;  // "Final boss" if any room has boss "final", else "Mid boss" if "mid", else ""
    bool cleared{false};   // its index in the theme < the current dungeonInTheme
    bool current{false};   // its index == the current dungeonInTheme
};

struct FBriefingUnit {
    std::string unitId;
    std::string name;      // units.json name (FUnitContent::displayName); the id when the unit is unknown
    std::string sprite;    // FUnitContent::spriteId; "" when unknown
};

struct FBriefingBoss {
    std::string name;      // FBossDef::name
    std::string sprite;    // the body unit's spriteId
    std::string mark;      // "MID BOSS" | "FINAL BOSS"
    std::string note;      // phases[0].notes ("" when empty)
    std::string gateLine;  // "" or "Cannot drop below <hpAbove>% health while its <count> <part name, plural> stand."
};

struct FBriefingMember {
    std::string name;      // FRunMember::displayName; the unit name when empty
    std::string role;      // the member unit's displayName (ResolveRunMemberUnit)
    std::string sprite;    // the member unit's spriteId
    int health{0};         // DungeonEffectiveHealth
    int maxHealth{0};      // DungeonMemberMaxHealth
    int level{1};
};

struct FRunBriefing {
    std::string themeId;
    std::string themeName;
    std::string dungeonName;
    int dungeonNumber{1};     // dungeonInTheme + 1
    int dungeonCount{1};      // dungeons in the theme
    int depth{1};             // run.depth
    int cycle{0};             // position.cycle
    std::string infoLine;
    std::string objective;
    std::string danger;
    std::string eliteLine;    // "" or "Elite option (<display name>): <text>."
    std::string eliteStakes;  // "" or "Elite: enemies +<health_percent>% max HP; a win offers a rune of <tier>."
    std::vector<FBriefingNode> route;
    std::vector<FBriefingDungeon> expedition;
    std::optional<FBriefingBoss> boss;
    std::vector<FBriefingUnit> threats;
    std::vector<FBriefingMember> party;
    std::string rewardLine;
    std::string enterLabel;
};

/// The briefing of the run's current dungeon (PositionOf(run.depth - 1)). nullopt without a run, a run
/// sequence or FContentRegistry. Reads the context only, except EnsureRunPath (via BuildPathStrip), which may
/// derive the dungeon's fork exactly as the HUD would on the first frame of the battle.
[[nodiscard]] std::optional<FRunBriefing> BuildRunBriefing(entt::registry& registry);
/// EPathNodeKind -> EBriefingNodeKind (same names).
[[nodiscard]] EBriefingNodeKind BriefingKindOf(EPathNodeKind kind);

} // namespace game

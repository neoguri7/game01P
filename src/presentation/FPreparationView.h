#pragma once

#include <string>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FRunStart.h"

namespace game {

/// The hub's three tabs (R6 P2: the preparation screen IS the hub — no separate hub screen, so a restart
/// reaches the first battle without an extra step).
enum class EHubTab { Expedition, Unlocks, Records };

inline constexpr int kHubTabCount = 3;

/// The tab's English display name (the body font has no Hangul glyphs).
[[nodiscard]] const char* HubTabName(EHubTab tab);

/// What the view asks for this frame (also the test seam — tests emplace it in ctx before Update).
enum class EPreparationCommand { None, Start, Cancel };
struct FPreparationCommand { EPreparationCommand value{EPreparationCommand::None}; };

/// The preparation screen's whole live state: the draft (the only editable thing), the active tab and the
/// last validation result. The state owns it; the view edits the draft only through the FRunStart draft
/// functions and never decides a rule itself.
struct FPreparationScreen {
    FPreparationDraft draft;
    EHubTab tab{EHubTab::Expedition};
    std::string startBlockedReason;   // ValidateRunSetup result, refreshed by the state each frame
};

/// Draws the hub (seal header line and the Start/Back footer common to all tabs, the tab bar, the active
/// tab body). Writes FPreparationCommand into ctx on Start/Cancel clicks; edits screen.draft only through
/// the FRunStart draft functions.
void DrawPreparationScreen(entt::registry& registry, FPreparationScreen& screen);

} // namespace game

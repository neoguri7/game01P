#include "states/FEmberPreparationState.h"

#include <cstdint>
#include <string>

#include <SDL3/SDL.h>

#include "core/InputState.h"
#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunSeed.h"
#include "gameplay/dungeon/FRunStart.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberBriefingState.h"
#include "states/FEmberTitleState.h"

FStateTransition FEmberPreparationState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (!initialized) {
        // WP-21b: the carrier is consumed exactly once; a missing carrier is "start from the checkpoint"
        // (fresh=false), which is the safe default. A run already installed means this screen must not
        // create a second one.
        game::FRunStartRequest request{};
        if (auto* carrier = Registry.ctx().find<game::FRunStartRequest>()) {
            request = *carrier;
            Registry.ctx().erase<game::FRunStartRequest>();
        } else {
            LOG_WARN("Preparation started without a run-start request; starting from the checkpoint.");
        }

        if (Registry.ctx().contains<game::FDungeonRun>()) {
            LOG_ERROR("Preparation found a run already installed; returning to the title instead of starting a second run.");
            return {EStateChangeKind::Root, std::string(kEmberTitleId)};
        }
        screen = game::FPreparationScreen{};
        screen.draft = game::MakeDefaultDraft(Registry, request);
        initialized = true;
    } else if (Registry.ctx().contains<game::FDungeonRun>()) {
        // A run was installed after a start; a second run must never begin from this screen.
        LOG_ERROR("Preparation found a run already installed; returning to the title instead of starting a second run.");
        return {EStateChangeKind::Root, std::string(kEmberTitleId)};
    }

    // The Start button/key is validated with the same rule every frame, so the reason shown under a blocked
    // Start is always the current one even while only the render path edited the draft.
    screen.startBlockedReason = game::ValidateRunSetup(Registry, screen.draft.setup).value_or(std::string{});

    // The view posts a command on a click; the keyboard maps Confirm = Start and Cancel = Cancel. Exactly
    // one command is taken per frame (and erased), so a click can never start two runs.
    game::EPreparationCommand command = game::EPreparationCommand::None;
    if (auto* posted = Registry.ctx().find<game::FPreparationCommand>()) {
        command = posted->value;
        Registry.ctx().erase<game::FPreparationCommand>();
    }
    if (command == game::EPreparationCommand::None) {
        if (const auto* input = Registry.ctx().find<game::FInputState>()) {
            if (input->isActionPressed(game::EInputAction::Confirm)) {
                command = game::EPreparationCommand::Start;
            } else if (input->isActionPressed(game::EInputAction::Cancel)) {
                command = game::EPreparationCommand::Cancel;
            }
        }
    }

    if (command == game::EPreparationCommand::Cancel) {
        return {EStateChangeKind::Root, std::string(kEmberTitleId)};
    }
    if (command == game::EPreparationCommand::Start) {
        if (!screen.startBlockedReason.empty()) {
            LOG_WARN("The preparation screen cannot start: {}", screen.startBlockedReason);
            return {};
        }
        // WP-28 A3: GAME01P_SEED overrides the ticks seed; a retry-from-checkpoint in the same process
        // advances by one (see FRunSeed.h). This is the only setup-driven new-run path.
        const bool started = game::StartDungeonRunFromSetup(
            Registry, game::ResolveRunSeed(static_cast<std::uint32_t>(SDL_GetTicks())), screen.draft.fresh,
            screen.draft.startCleared, screen.draft.setup);
        if (!started) {
            LOG_ERROR("The preparation screen could not start the expedition; staying on the draft.");
            return {};
        }
        return {EStateChangeKind::Root, std::string(kEmberBriefingId)};
    }
    return {};
}

void FEmberPreparationState::Render(entt::registry& Registry)
{
    game::DrawPreparationScreen(Registry, screen);
}

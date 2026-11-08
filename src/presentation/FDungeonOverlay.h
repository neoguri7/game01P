#pragma once

#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include "core/Time.h"
#include "core/SystemManager.h"
#include "presentation/FUiLayout.h"

namespace game {

struct FDungeonRun;

struct FDungeonUiState {
    bool debugVisible{false};
    bool confirmAbandon{false};
    bool confirmNewExpedition{false};  // starting over would overwrite a saved expedition
    bool newExpeditionFresh{false};    // which "new expedition" button asked
    int selectedGrowthMember{0};
    // WP-27: the rune picked in this reward modal, kept so the same modal can fit it on the spot. View-
    // owned, never saved: set only on a Reward-phase pick, kept after a fit (the modal reports what was
    // fitted) and cleared on Continue.
    std::string pickedRuneId;
    // WP-27h: "<Member> · <Skill>" of the fit this pick already made. View-owned, never saved; set by the
    // FIT RUNE button's success path and cleared with pickedRuneId. Its presence is the modal's "this pick
    // is decided" state — the FIT RUNE block stays hidden even when a second unfitted copy of the same
    // rune sits in the collected pool.
    std::string pickedRuneFit;
};

// ---------------------------------------------------------------------------------------------
// WP-24b onboarding: one non-blocking strip over every expedition screen, text from data/onboarding.
// ---------------------------------------------------------------------------------------------

/// The three run states the first-run hints teach. The vocabulary is data (`onboarding.json`'s `at`).
enum class EOnboardingTrigger { BattleFirst, RewardFirst, CampFirst };

struct FOnboardingStep {
    EOnboardingTrigger at{EOnboardingTrigger::BattleFirst};
    std::string text;
};

/// The parsed `data/onboarding` document. A missing file or an invalid row is a warning, never a boot
/// failure: the strip simply has fewer (or no) steps.
struct FOnboardingContent {
    std::vector<FOnboardingStep> steps;
};

/// In-session only (never saved): the walk through the ordered steps.
struct FOnboardingProgress {
    int nextIndex{0};    // first step not yet left behind
    int shownIndex{-1};  // step currently on screen, or -1
};

/// The pure result of one step decision. `completed` means the last step was just left behind, so the
/// caller persists onboarding_done.
struct FOnboardingDecision {
    FOnboardingProgress progress;
    int shownIndex{-1};
    bool completed{false};
};

/// `battle_first` = phase Battle, depth 1, room 0; `reward_first` / `camp_first` follow the run phase.
[[nodiscard]] std::optional<EOnboardingTrigger> OnboardingTriggerFor(const FDungeonRun* run);

/// Loads data/onboarding once into the context (invalid rows are dropped with a warning).
void LoadOnboardingContent(entt::registry& registry);
[[nodiscard]] const FOnboardingContent* FindOnboardingContent(const entt::registry& registry);

/// The ordered walk, render-free so the tests can drive it directly:
/// - the shown step is the first step at index >= progress.nextIndex whose trigger matches;
/// - when the current run state stops matching a shown step, progress moves past it;
/// - leaving the last step completes the walk.
[[nodiscard]] FOnboardingDecision OnboardingStepFor(const FOnboardingContent& content,
                                                    const FOnboardingProgress& progress,
                                                    std::optional<EOnboardingTrigger> trigger);

/// True only on an expedition screen: a run exists AND its battle grid is on screen. The run-phase modals
/// (reward/event/camp) push over the battle and keep the grid; the title, hub and preparation screens have
/// no grid, so the strip is never drawn there.
[[nodiscard]] bool ExpeditionScreenActive(const entt::registry& registry);

/// Clears the in-session walk (the replay button calls it next to the persisted flag).
void ResetOnboardingProgress(entt::registry& registry);

/// "Replay tutorial hints": clears the persisted flag and the in-session step progress. Lives here (not in
/// FSettings) so the settings module never has to know the onboarding state.
void ReplayOnboarding(entt::registry& registry);

/// The hint to draw this frame: `text` is null when nothing is shown. `side` means the card goes beside the
/// centered run-phase modal (reward/camp) instead of the top-center battle strip.
struct FOnboardingHintView {
    const std::string* text{nullptr};
    bool side{false};
};

/// Advances the walk and saves onboarding_done after the last step. The text points into the context-owned
/// content, so it stays valid for the rest of the frame.
[[nodiscard]] FOnboardingHintView UpdateOnboarding(entt::registry& registry);

/// Draws the hint straight into ImGui's foreground draw list (no window at all, so it cannot take input and
/// sits above modals and their dim). The card height follows the wrapped text at the current font scale.
void DrawOnboardingHint(const ui::FUiLayout& layout, const std::string& text, bool side);

void InitializeDungeonUi(entt::registry& registry);
/// Debug-only overlay (WP-02b) plus the WP-24b text-scale apply and onboarding strip.
void RenderDungeonOverlay(entt::registry& registry, const Time& time, const SystemManager& systems);

} // namespace game

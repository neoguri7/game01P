#include "presentation/FDungeonOverlay.h"

#include <algorithm>
#include <cfloat>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "core/AssetManager.h"
#include "core/InputState.h"
#include "core/Logger.h"
#include "debug/DebugOverlay.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/grid/FGridTypes.h"
#include "presentation/FModalViewRegistry.h"
#include "presentation/FSettings.h"
#include "presentation/FTitleMenuView.h"
#include "presentation/FUiFrame.h"
#include "presentation/FUiTheme.h"

namespace game {
namespace {

constexpr int kOnboardingSchemaVersion = 1;
// Design-px font size of the hint card; the layout scale and the user's text scale multiply it at draw time
// (a draw-list AddText does not apply style.FontScaleMain itself).
constexpr float kOnboardingTextSize = 17.f;
constexpr float kOnboardingRounding = 4.f;
constexpr ImU32 kOnboardingBack = IM_COL32(12, 9, 10, 228);
constexpr ImU32 kOnboardingTrim = IM_COL32(201, 160, 82, 170);

std::optional<EOnboardingTrigger> ParseOnboardingTrigger(std::string_view name) {
    if (name == "battle_first") return EOnboardingTrigger::BattleFirst;
    if (name == "reward_first") return EOnboardingTrigger::RewardFirst;
    if (name == "camp_first") return EOnboardingTrigger::CampFirst;
    return std::nullopt;
}

/// Applies the known keys of a parsed onboarding document. Unknown triggers, wrong types and empty texts
/// drop that one step with a warning; the survivors keep their authored order.
void ApplyOnboardingDocument(const nlohmann::json& document, FOnboardingContent& content) {
    if (!document.is_object()) {
        LOG_WARN("onboarding.json is not an object; no tutorial hints will be shown.");
        return;
    }
    const auto version = document.find("schema_version");
    if (version != document.end() &&
        (!version->is_number_integer() || version->get<int>() != kOnboardingSchemaVersion)) {
        LOG_WARN("onboarding.json declares an unknown schema version; no tutorial hints will be shown.");
        return;
    }
    const auto steps = document.find("steps");
    if (steps == document.end() || !steps->is_array()) {
        LOG_WARN("onboarding.json has no steps array; no tutorial hints will be shown.");
        return;
    }
    for (const auto& row : *steps) {
        if (!row.is_object()) {
            LOG_WARN("onboarding.json step is not an object; dropped.");
            continue;
        }
        const auto at = row.find("at");
        const auto text = row.find("text");
        if (at == row.end() || !at->is_string() || text == row.end() || !text->is_string()) {
            LOG_WARN("onboarding.json step needs string 'at' and 'text'; dropped.");
            continue;
        }
        const std::string trigger = at->get<std::string>();
        const std::optional<EOnboardingTrigger> parsed = ParseOnboardingTrigger(trigger);
        if (!parsed) {
            LOG_WARN("onboarding.json step has an unknown trigger '{}'; dropped.", trigger);
            continue;
        }
        std::string body = text->get<std::string>();
        if (body.empty()) {
            LOG_WARN("onboarding.json step '{}' has an empty text; dropped.", trigger);
            continue;
        }
        content.steps.push_back(FOnboardingStep{*parsed, std::move(body)});
    }
    LOG_INFO("onboarding: {} step(s) loaded.", content.steps.size());
}

} // namespace

std::optional<EOnboardingTrigger> OnboardingTriggerFor(const FDungeonRun* run) {
    if (run == nullptr) return std::nullopt;
    if (run->phase == EDungeonPhase::Battle && run->depth == 1 && run->room == 0) {
        return EOnboardingTrigger::BattleFirst;
    }
    if (run->phase == EDungeonPhase::Reward) return EOnboardingTrigger::RewardFirst;
    if (run->phase == EDungeonPhase::Camp) return EOnboardingTrigger::CampFirst;
    return std::nullopt;
}

void LoadOnboardingContent(entt::registry& registry) {
    if (registry.ctx().contains<FOnboardingContent>()) return;
    FOnboardingContent content;
    const auto* assets = registry.ctx().find<FAssetManager>();
    if (assets == nullptr) {
        LOG_WARN("onboarding.json needs the asset manager; no tutorial hints will be shown.");
    } else if (const auto digest = assets->digest("data/onboarding"); digest) {
        ApplyOnboardingDocument(digest->json(), content);
    }
    registry.ctx().emplace<FOnboardingContent>(std::move(content));
}

const FOnboardingContent* FindOnboardingContent(const entt::registry& registry) {
    return registry.ctx().find<FOnboardingContent>();
}

FOnboardingDecision OnboardingStepFor(const FOnboardingContent& content, const FOnboardingProgress& progress,
                                      std::optional<EOnboardingTrigger> trigger) {
    FOnboardingDecision decision;
    const int count = static_cast<int>(content.steps.size());
    int next = std::clamp(progress.nextIndex, 0, count);
    int shown = progress.shownIndex;

    // Leaving the shown step (the run state stopped matching) moves progress past it; leaving the last
    // step completes the whole walk.
    if (shown >= 0 && (shown >= count || !trigger.has_value() || content.steps[shown].at != *trigger)) {
        decision.completed = shown == count - 1;
        next = std::max(next, shown + 1);
        shown = -1;
    }

    // The first step not yet left behind whose trigger matches. Steps that never match (a deep continue)
    // are skipped by the index rule.
    if (shown < 0 && trigger.has_value()) {
        for (int index = next; index < count; ++index) {
            if (content.steps[index].at == *trigger) {
                shown = index;
                break;
            }
        }
    }

    decision.progress.nextIndex = next;
    decision.progress.shownIndex = shown;
    decision.shownIndex = shown;
    return decision;
}

bool ExpeditionScreenActive(const entt::registry& registry) {
    // The battle state owns the only expedition screen, and its grid outlives every run-phase modal
    // (reward/event/camp push over the battle). The title, hub and preparation screens have no grid.
    return registry.ctx().contains<FDungeonRun>() && registry.ctx().contains<FGridMetrics>();
}

void ResetOnboardingProgress(entt::registry& registry) {
    registry.ctx().insert_or_assign<FOnboardingProgress>(FOnboardingProgress{});
}

void ReplayOnboarding(entt::registry& registry) {
    SetOnboardingDone(registry, false);
    ResetOnboardingProgress(registry);
}

FOnboardingHintView UpdateOnboarding(entt::registry& registry) {
    FOnboardingHintView view;
    if (!ExpeditionScreenActive(registry)) return view;
    if (CurrentSettings(registry).onboardingDone) return view;
    LoadOnboardingContent(registry);
    const auto* content = FindOnboardingContent(registry);
    if (content == nullptr || content->steps.empty()) return view;

    auto* stored = registry.ctx().find<FOnboardingProgress>();
    if (stored == nullptr) stored = &registry.ctx().emplace<FOnboardingProgress>();
    const auto* run = registry.ctx().find<FDungeonRun>();
    const FOnboardingDecision decision = OnboardingStepFor(*content, *stored, OnboardingTriggerFor(run));
    *stored = decision.progress;
    if (decision.completed) SetOnboardingDone(registry, true);

    if (stored->shownIndex < 0 || stored->shownIndex >= static_cast<int>(content->steps.size())) return view;
    const FOnboardingStep& step = content->steps[static_cast<std::size_t>(stored->shownIndex)];
    view.text = &step.text;
    // Reward and camp hints would sit under their centered modal: put those cards beside it instead.
    view.side = step.at != EOnboardingTrigger::BattleFirst;
    return view;
}

void DrawOnboardingHint(const ui::FUiLayout& layout, const std::string& text, bool side) {
    const ui::FRect& anchor = side ? layout.onboarding.side : layout.onboarding.strip;
    if (anchor.w <= 0.f) return;
    ImFont* font = ImGui::GetFont();
    if (font == nullptr) return;
    const float fontScale = ImGui::GetStyle().FontScaleMain > 0.f ? ImGui::GetStyle().FontScaleMain : 1.f;
    const float size = kOnboardingTextSize * layout.scale * fontScale;
    const float padding = layout.onboarding.padding;
    const float wrapWidth = std::max(1.f, anchor.w - padding * 2.f);
    // The card height follows the wrapped text, so no authored height can clip or overflow at 1.25.
    const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, wrapWidth, text.c_str());
    const ImVec2 min{anchor.x, anchor.y};
    const ImVec2 max{anchor.x + anchor.w, anchor.y + measured.y + padding * 2.f};

    // The foreground draw list is above every window (and above the modal dim), and a draw list cannot take
    // input: a hint may overlap a modal button and still never eat its click.
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(min, max, kOnboardingBack, kOnboardingRounding);
    draw->AddRect(min, max, kOnboardingTrim, kOnboardingRounding);
    draw->AddText(font, size, {min.x + padding, min.y + padding}, ui::palette::kText, text.c_str(), nullptr,
                  wrapWidth);
}

void InitializeDungeonUi(entt::registry& registry) {
    registry.ctx().emplace<FDungeonUiState>();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigNavCaptureKeyboard = false;
    ui::ApplyTheme();
    // The composition root registers the views and the title rows once. States and views only look them up.
    RegisterModalViews(registry);
    RegisterTitleMenuItems(registry);
}

/// The overlay is the debug layer (WP-02b) plus the WP-24b session UX: the one global text scale and the
/// single onboarding hint card. Every title, hub and run modal is drawn by the state that owns it.
void RenderDungeonOverlay(entt::registry& registry, const Time& time, const SystemManager& systems) {
    ui::ApplyTextScale(CurrentSettings(registry).textScale);
    auto& ui = registry.ctx().get<FDungeonUiState>();
    if (const auto* input = registry.ctx().find<FInputState>();
        input && input->isActionPressed(EInputAction::DebugToggle)) ui.debugVisible = !ui.debugVisible;
    if (ui.debugVisible) RenderDebugOverlay(registry, time, systems);

    if (const FOnboardingHintView hint = UpdateOnboarding(registry); hint.text != nullptr) {
        if (const auto* layout = ui::FindUiLayout(registry)) DrawOnboardingHint(*layout, *hint.text, hint.side);
    }
}

} // namespace game

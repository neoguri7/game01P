#include "presentation/FDungeonViews.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/EngineState.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FPassiveTreePanel.h"
#include "presentation/FUiTheme.h"

namespace game {
namespace {

std::string Signed(int value) {
    return value > 0 ? "+" + std::to_string(value) : std::to_string(value);
}

std::string Join(const std::vector<std::string>& values) {
    std::string joined;
    for (const auto& value : values) {
        if (!joined.empty()) joined += ", ";
        joined += value;
    }
    return joined;
}

/// The card's effect line: the same rune fields the resolver reads, in the order they are applied. The WP-03
/// preview needs a battle actor, and the reward modal has none, so the card spells out the deltas instead.
std::string RuneEffectSummary(const gameplay::FRuneContent& rune) {
    std::vector<std::string> parts;
    if (!rune.statusOnHit.empty()) parts.push_back("On hit: " + rune.statusOnHit);
    if (!rune.shapeOverride.empty()) parts.push_back("Shape: " + rune.shapeOverride);
    if (rune.powerDelta != 0) parts.push_back("Power " + Signed(rune.powerDelta));
    if (rune.apDelta != 0) parts.push_back("AP " + Signed(rune.apDelta));
    if (rune.rangeDelta != 0) parts.push_back("Range " + Signed(rune.rangeDelta));
    if (rune.areaRadiusDelta != 0) parts.push_back("Area " + Signed(rune.areaRadiusDelta));
    const std::string statusTarget = rune.statusId.empty() ? "all statuses" : rune.statusId;
    if (rune.statusDurationDelta != 0) {
        parts.push_back("Duration " + Signed(rune.statusDurationDelta) + " (" + statusTarget + ")");
    }
    if (rune.statusMagnitudeDelta != 0) {
        parts.push_back("Magnitude " + Signed(rune.statusMagnitudeDelta) + " (" + statusTarget + ")");
    }
    if (rune.lifeOnHit != 0) parts.push_back("Life on hit " + Signed(rune.lifeOnHit));
    if (!rune.addsTags.empty()) parts.push_back("Adds " + Join(rune.addsTags));
    if (!rune.requiresAllTags.empty()) parts.push_back("Requires all " + Join(rune.requiresAllTags));
    if (!rune.requiresAnyTags.empty()) parts.push_back("Requires any " + Join(rune.requiresAnyTags));
    if (!rune.excludesTags.empty()) parts.push_back("Excludes " + Join(rune.excludesTags));
    if (parts.empty()) parts.push_back("No numeric change");
    return Join(parts);
}

void DrawRuneOfferForecastLine(const RuneOfferForecast& forecast) {
    if (forecast.guaranteed) {
        ImGui::Text("Next battle room rune offer: guaranteed (pity)");
    } else {
        ImGui::Text("Next battle room rune offer: chance %d%%",
                    static_cast<int>(std::lround(forecast.chance * 100.0)));
    }
}

/// One offer card: name, what it does, which party skills accept it, the duplicate refund, and its Pick
/// button. Returns true when the pick resolved the offer.
bool DrawRuneOfferCard(
    entt::registry& registry, const FRewards& rewards, const gameplay::FContentRegistry* content,
    std::size_t index, const std::string& runeId) {
    ImGui::PushID(static_cast<int>(index));
    const auto* rune = content != nullptr ? content->runes.find(runeId) : nullptr;
    ImGui::SeparatorText(rune != nullptr ? rune->displayName.c_str() : runeId.c_str());
    if (rune != nullptr) ImGui::TextWrapped("%s", RuneEffectSummary(*rune).c_str());
    const auto fitting = RuneFittingSkillNames(registry, runeId);
    ImGui::TextDisabled("Fits: %s", fitting.empty() ? "no current party skill" : Join(fitting).c_str());
    if (IsRuneOwned(registry, runeId) && rewards.tables.duplicatePolicy == kRewardDuplicatePolicyRefund) {
        ImGui::TextColored({.84f, .72f, .36f, 1}, "Already owned — +%d materials",
                           rewards.tables.duplicateRefundMaterials);
    }
    const bool resolved = ui::Button("Pick", {180, 34}) && ResolveRuneOffer(registry, static_cast<int>(index));
    ImGui::PopID();
    return resolved;
}

} // namespace

const std::string& MemberName(const FRunMember& member) {
    return member.displayName.empty() ? member.id : member.displayName;
}

void DrawGrowthPanel(
    entt::registry& registry, const FDungeonGrowth& growth, FDungeonRun& run, std::size_t memberIndex) {
    if (memberIndex >= run.party.size()) return;
    FRunMember& member = run.party[memberIndex];
    ImGui::PushID(static_cast<int>(memberIndex));
    ImGui::Text("Level %d   EXP %d / %d   Node points %d", member.level, member.experience,
                growth.ExperienceForNextLevel(member.level), member.passivePoints);
    const FPassiveStatBonuses bonuses = growth.passiveTree.Bonuses(member.allocatedNodes);
    ImGui::TextDisabled("Tree bonuses: +%d HP, +%d ATK, +%d MATK, +%d DEF, +%d MDEF, +%d speed",
                        bonuses.maxHealth, bonuses.attack, bonuses.magicAttack, bonuses.defense,
                        bonuses.magicDefense, bonuses.speed);
    ui::DrawPassiveTreePanel(ui::FPassiveTreePanel{
        .nodes = &growth.passiveTree.nodes,
        .rootNodeId = growth.passiveTree.rootNodeId,
        .allocated = &member.allocatedNodes,
        .points = member.passivePoints,
        .canAllocate = [&](std::string_view nodeId) { return CanAllocateDungeonNode(registry, memberIndex, nodeId); },
        .allocate = [&](std::string_view nodeId) { (void)AllocateDungeonNode(registry, memberIndex, nodeId); },
    });
    ImGui::PopID();
}

void DrawRuneOfferForecast(entt::registry& registry) {
    if (registry.ctx().find<FRewards>() == nullptr || registry.ctx().find<FDungeonRun>() == nullptr) return;
    DrawRuneOfferForecastLine(RuneOfferForecastOf(registry));
}

/// The offer cards plus the skip button (WP-17; shared with the camp screen since WP-10). A pick or a skip
/// resolves the offer in place (no run transition); the phase only moves on through ContinueReward or
/// LeaveCamp. The candidate list is copied: resolving it clears the live one.
void DrawRuneOffers(entt::registry& registry, FRewards& rewards, const gameplay::FContentRegistry* content) {
    const std::vector<std::string> candidates = rewards.offer.candidates;
    ImGui::SeparatorText("CHOOSE A RUNE");
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (DrawRuneOfferCard(registry, rewards, content, index, candidates[index])) return;
    }
    const std::string skipLabel = rewards.tables.skipRefundMaterials > 0
        ? "Skip (+" + std::to_string(rewards.tables.skipRefundMaterials) + " materials)"
        : std::string("Skip");
    if (ui::Button(skipLabel.c_str(), {-1, 32})) (void)ResolveRuneOffer(registry, -1);
}

/// The room-secured screen: the rune offer (3 cards + skip), the forecast, then the growth panel. The only
/// transition button posts the intent; pick/skip are direct, non-transition calls.
void RenderDungeonRewardView(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    auto& ui = registry.ctx().get<FDungeonUiState>();
    const auto& window = registry.ctx().get<FEngineWindowState>();
    ImGui::SetNextWindowPos({window.width * .5f, window.height * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({580, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({580, 0}, {580, static_cast<float>(window.height - 80)});
    ImGui::OpenPopup("Expedition choice");
    if (ImGui::BeginPopupModal("Expedition choice", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        DrawDungeonIcon(registry, EDungeonSprite::Chest);
        ImGui::SameLine();
        ui::HeadingText("ROOM SECURED", 26.f);
        ImGui::TextWrapped("%s", run.message.c_str());
        ImGui::Separator();
        auto* rewards = registry.ctx().find<FRewards>();
        const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        ImGui::Text("Materials %d  |  collected runes %zu", run.materials, run.runes.size());
        if (rewards == nullptr) {
            ImGui::TextColored({.9f, .31f, .31f, 1}, "Reward offer settings are missing.");
        } else {
            if (rewards->offer.active) DrawRuneOffers(registry, *rewards, content);
            else ImGui::TextDisabled("No rune offer this room.");
            DrawRuneOfferForecast(registry);
        }
        ImGui::Separator();
        const auto* growth = registry.ctx().find<FDungeonGrowth>();
        if (growth == nullptr) {
            ImGui::TextColored({.9f, .31f, .31f, 1}, "Run growth content is missing.");
        } else if (!run.party.empty()) {
            ui.selectedGrowthMember = std::clamp(ui.selectedGrowthMember, 0, static_cast<int>(run.party.size()) - 1);
            if (ImGui::BeginCombo("Character", MemberName(run.party[ui.selectedGrowthMember]).c_str())) {
                for (std::size_t index = 0; index < run.party.size(); ++index) {
                    const bool selected = static_cast<int>(index) == ui.selectedGrowthMember;
                    if (ImGui::Selectable(MemberName(run.party[index]).c_str(), selected)) {
                        ui.selectedGrowthMember = static_cast<int>(index);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            DrawGrowthPanel(registry, *growth, run, static_cast<std::size_t>(ui.selectedGrowthMember));
        }
        // The offer is a decision: Continue stays disabled until the player picked or skipped it.
        const bool offerOpen = rewards != nullptr && rewards->offer.active;
        if (offerOpen) ImGui::TextDisabled("Pick a rune or skip the offer to continue.");
        ImGui::BeginDisabled(offerOpen);
        if (ui::Button("Continue", {-1, 42})) {
            PostRunIntent(registry, ERunIntent::ContinueReward);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

} // namespace game

#include "presentation/FDungeonViews.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/EngineState.h"
#include "gameplay/combat/FEffectHooks.h"  // WP-27: the tempo kind names (push/pull/chain)
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

/// WP-27: the display name of a status id (id fallback; `content` may be null).
std::string StatusName(const gameplay::FContentRegistry* content, const std::string& statusId) {
    return ContentDisplayName(content != nullptr ? content->statuses.find(statusId) : nullptr, statusId);
}

/// WP-27: one hook's effect text. Known kinds speak the shared kind vocabulary; an unknown kind keeps the
/// authored name and numbers (a hook is never silently dropped from the card).
std::string HookEffectText(const gameplay::FEffectSpec& effect, const gameplay::FContentRegistry* content) {
    if (effect.kind == kEffectKindChain) {
        return "chain x" + std::to_string(effect.amount) + " (dist " + std::to_string(effect.distance) + ")";
    }
    if (effect.kind == kEffectKindPush || effect.kind == kEffectKindPull) {
        return effect.kind + " " + std::to_string(effect.distance);
    }
    if (effect.kind == gameplay::kEffectKindApplyStatusTarget) {
        return "apply " + StatusName(content, effect.statusId);
    }
    std::string text = effect.kind.empty() ? std::string("effect") : effect.kind;
    if (effect.amount != 0) text += " x" + std::to_string(effect.amount);
    if (effect.distance != 0) text += " (dist " + std::to_string(effect.distance) + ")";
    return text;
}

/// WP-27: the event label of one hook — the typed vocabulary wins; anything else keeps its authored name.
std::string HookEventLabel(const gameplay::FHook& hook) {
    switch (hook.event) {
    case gameplay::EHookEvent::OnHit: return "On hit";
    case gameplay::EHookEvent::OnKill: return "On kill";
    case gameplay::EHookEvent::OnDamageTaken: return "When hit";
    default: break;
    }
    return hook.eventName.empty() ? gameplay::HookEventName(hook.event) : hook.eventName;
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
    if (rune != nullptr) ImGui::TextWrapped("%s", RuneEffectSummary(*rune, content).c_str());
    const auto fitting = RuneFittingSkillNames(registry, runeId);
    // WP-27: TextWrapped, not TextDisabled — a long "Fits:" line must fold instead of being clipped.
    ImGui::TextWrapped("Fits: %s", fitting.empty() ? "no current party skill" : Join(fitting).c_str());
    if (IsRuneOwned(registry, runeId) && rewards.tables.duplicatePolicy == kRewardDuplicatePolicyRefund) {
        ImGui::TextColored({.84f, .72f, .36f, 1}, "Already owned — +%d materials",
                           rewards.tables.duplicateRefundMaterials);
    }
    const bool resolved = ui::Button("Pick", {180, 34}) && ResolveRuneOffer(registry, static_cast<int>(index));
    if (resolved) {
        // WP-27: remember the pick so the reward modal can fit it on the spot. Only the Reward phase may
        // record it — a camp-purchased offer must not leave a stale id for the next reward modal.
        const auto* run = registry.ctx().find<FDungeonRun>();
        if (run != nullptr && run->phase == EDungeonPhase::Reward) {
            registry.ctx().get<FDungeonUiState>().pickedRuneId = runeId;
        }
    }
    ImGui::PopID();
    return resolved;
}

} // namespace

/// WP-27: the card's effect line — the same rune fields the resolver reads, in the order they are applied,
/// plus one line per data-authored hook. The WP-03 preview needs a battle actor and the reward modal has
/// none, so the card spells out the deltas and hooks instead; a rune with hooks never reads "No numeric
/// change". Status ids resolve through `content` (display name, id fallback).
std::string RuneEffectSummary(const gameplay::FRuneContent& rune, const gameplay::FContentRegistry* content) {
    std::vector<std::string> parts;
    if (!rune.statusOnHit.empty()) parts.push_back("On hit: " + StatusName(content, rune.statusOnHit));
    if (!rune.shapeOverride.empty()) parts.push_back("Shape: " + rune.shapeOverride);
    if (rune.powerDelta != 0) parts.push_back("Power " + Signed(rune.powerDelta));
    if (rune.apDelta != 0) parts.push_back("AP " + Signed(rune.apDelta));
    if (rune.rangeDelta != 0) parts.push_back("Range " + Signed(rune.rangeDelta));
    if (rune.areaRadiusDelta != 0) parts.push_back("Area " + Signed(rune.areaRadiusDelta));
    const std::string statusTarget = rune.statusId.empty() ? "all statuses" : StatusName(content, rune.statusId);
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
    for (const gameplay::FHook& hook : rune.hooks) {
        parts.push_back(HookEventLabel(hook) + ": " + HookEffectText(hook.effect, content));
    }
    if (parts.empty()) parts.push_back("No numeric change");
    return Join(parts);
}

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

namespace {

/// WP-27: appends one sentence to the run message instead of replacing it — the reward step's own line
/// ("Room secured. ... Level up: ...") must survive an equip.
void AppendRunMessage(FDungeonRun& run, const std::string& text) {
    if (text.empty()) return;
    if (!run.message.empty()) run.message += " ";
    run.message += text;
}

/// WP-27: the first collected-pool copy of `runeId`, or nothing when the copy is already fitted (a refund
/// whose rune is owned-and-fitted adds no pool copy). One lookup so the FIT RUNE block and the Picked line
/// can never disagree about whether a fit is still possible.
std::optional<std::size_t> FindUnfittedCopy(const FDungeonRun& run, const std::string& runeId) {
    for (std::size_t index = 0; index < run.runes.size(); ++index) {
        if (run.runes[index] == runeId) return index;  // first match: duplicate copies are de facto absent
    }
    return std::nullopt;
}

/// WP-27: the fit-on-the-spot block for the rune just picked in the reward modal. It exists only while an
/// unfitted copy sits in the collected pool (`run.runes`): a duplicate refund adds no copy, so a pick whose
/// rune is already fitted shows no block — Continue stays the only action. The (member, skill) buttons come
/// straight from ValidateDungeonRuneEquip in the deterministic member → slot order; the block never
/// re-implements the rules, it renders the refusals as-is.
///
/// Layout (WP-27 review): one compact row per member with a legal slot — the member name as a fixed-width
/// label column, the member's legal skills as text-sized buttons after it. Button widths come from
/// CalcTextSize, so a skill name is never clipped at any text scale; a row wraps at the modal's content
/// edge, indented to the label column. The visible "Equip on:" line above the rows reads as one action.
void DrawPickedRuneFitBlock(entt::registry& registry, FDungeonRun& run, FDungeonUiState& ui,
                            const gameplay::FContentRegistry* content) {
    if (ui.pickedRuneId.empty()) return;
    const auto runeIndex = FindUnfittedCopy(run, ui.pickedRuneId);
    if (!runeIndex) return;  // the copy is already fitted: nothing to attach
    ImGui::SeparatorText("FIT RUNE");
    // Collect the legal pairs first (the rules own every verdict); the same walk remembers the first
    // verbatim refusal for the zero-legal fallback.
    std::vector<std::vector<std::size_t>> legalSlots(run.party.size());
    std::optional<std::string> firstProblem;
    bool anyLegal = false;
    for (std::size_t memberIndex = 0; memberIndex < run.party.size(); ++memberIndex) {
        for (std::size_t slotIndex = 0; slotIndex < run.party[memberIndex].skills.size(); ++slotIndex) {
            const auto problem = ValidateDungeonRuneEquip(registry, memberIndex, slotIndex, *runeIndex);
            if (problem) {
                if (!firstProblem) firstProblem = problem;  // verbatim rule text, first in the walk order
                continue;
            }
            legalSlots[memberIndex].push_back(slotIndex);
            anyLegal = true;
        }
    }
    if (!anyLegal) {
        if (firstProblem) ImGui::TextWrapped("%s", firstProblem->c_str());
        ImGui::TextDisabled("Equip later at camp.");
        return;
    }
    ImGui::TextUnformatted("Equip on:");
    const ImGuiStyle& style = ImGui::GetStyle();
    float labelWidth = 0.f;
    for (std::size_t memberIndex = 0; memberIndex < run.party.size(); ++memberIndex) {
        if (legalSlots[memberIndex].empty()) continue;
        labelWidth = std::max(labelWidth, ImGui::CalcTextSize(MemberName(run.party[memberIndex]).c_str()).x);
    }
    labelWidth += style.ItemSpacing.x * 2.f;  // the fixed label column: longest shown name + breathing room
    for (std::size_t memberIndex = 0; memberIndex < run.party.size(); ++memberIndex) {
        if (legalSlots[memberIndex].empty()) continue;
        FRunMember& member = run.party[memberIndex];
        const std::string memberName = MemberName(member);
        const float rowStartX = ImGui::GetCursorPosX();  // the label column anchors on this line start
        const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        ImGui::AlignTextToFramePadding();  // WP-27: the label baseline matches the framed buttons on the row
        ImGui::TextUnformatted(memberName.c_str());
        bool columnPlaced = false;
        float previousMaxX = 0.f;
        for (const std::size_t slotIndex : legalSlots[memberIndex]) {
            const FSkillSlot& slot = member.skills[slotIndex];
            const auto* skill = content != nullptr ? content->skills.find(slot.skillId) : nullptr;
            const std::string skillName = ContentDisplayName(skill, slot.skillId);
            const float buttonWidth = ImGui::CalcTextSize(skillName.c_str()).x + style.FramePadding.x * 2.f;
            if (!columnPlaced) {
                ImGui::SameLine(rowStartX + labelWidth);  // the fixed label column
                columnPlaced = true;
            } else if (previousMaxX + style.ItemSpacing.x + buttonWidth <= rowRight) {
                ImGui::SameLine();
            } else {
                ImGui::SetCursorPosX(rowStartX + labelWidth);  // wrap, keeping the label column
            }
            const std::string buttonId =
                skillName + "##m" + std::to_string(memberIndex) + "s" + std::to_string(slotIndex);
            if (ui::Button(buttonId.c_str(), {buttonWidth, 0})) {
                const std::string runeName = ContentDisplayName(
                    content != nullptr ? content->runes.find(ui.pickedRuneId) : nullptr, ui.pickedRuneId);
                if (const auto error = EquipDungeonRune(registry, memberIndex, slotIndex, *runeIndex)) {
                    AppendRunMessage(run, "Cannot fit rune: " + *error);
                } else {
                    AppendRunMessage(run, runeName + " fitted to " + memberName + " · " + skillName + ".");
                    ui.pickedRuneId.clear();  // one copy equips once; the block ends
                    return;
                }
            }
            const float buttonMaxX = ImGui::GetItemRectMax().x;
            if (ImGui::IsItemHovered()) {
                const std::string tooltip = "Equip on " + memberName + " · " + skillName;
                ImGui::SetTooltip("%s", tooltip.c_str());
            }
            previousMaxX = buttonMaxX;
        }
    }
}

} // namespace

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
            else if (ui.pickedRuneId.empty()) ImGui::TextDisabled("No rune offer this room.");
            else {
                // WP-27: the offer was just decided — say what was picked instead of claiming there was
                // none. Only promise a fit while an unfitted copy is really in the pool (a refund of an
                // already fitted rune adds none): the same lookup the FIT RUNE block uses.
                const auto* picked = content != nullptr ? content->runes.find(ui.pickedRuneId) : nullptr;
                const std::string pickedName = ContentDisplayName(picked, ui.pickedRuneId);
                if (FindUnfittedCopy(run, ui.pickedRuneId)) {
                    ImGui::TextDisabled("Picked: %s — fit it now or later at camp.", pickedName.c_str());
                } else {
                    ImGui::TextDisabled("Picked: %s — already fitted.", pickedName.c_str());
                }
            }
            DrawRuneOfferForecast(registry);
            // WP-27: the just-picked rune's fit block lives in the offer section and shows while the offer
            // is already resolved, as long as an unfitted copy sits in the collected pool. A boss "extra"
            // offer keeps both: the new cards above, the first pick's block below (a second pick just
            // replaces pickedRuneId).
            DrawPickedRuneFitBlock(registry, run, ui, content);
        }
        ImGui::Separator();
        // The offer is a decision: Continue stays disabled until the player picked or skipped it. It sits
        // directly under the offer section (WP-27 §4) so the primary action needs no scrolling.
        const bool offerOpen = rewards != nullptr && rewards->offer.active;
        if (offerOpen) ImGui::TextDisabled("Pick a rune or skip the offer to continue.");
        ImGui::BeginDisabled(offerOpen);
        if (ui::Button("Continue", {-1, 42})) {
            ui.pickedRuneId.clear();  // WP-27: the fit offer dies with the reward step
            PostRunIntent(registry, ERunIntent::ContinueReward);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
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
        ImGui::EndPopup();
    }
}

} // namespace game

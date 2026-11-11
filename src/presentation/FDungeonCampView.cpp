#include "presentation/FDungeonViews.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/EngineState.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonCamp.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FCampLoadoutHost.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FUiTheme.h"

namespace game {

/// The shared leave/abandon confirm. `allowLeave` offers "Save and leave" (the battle's option);
/// `viaIntent` posts the run intent instead of setting the run's flags (the camp's host owns its phase).
void DrawAbandonConfirm(entt::registry& registry, bool allowLeave, bool viaIntent) {
    auto& ui = registry.ctx().get<FDungeonUiState>();
    if (!ui.confirmAbandon) return;
    auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) {
        ui.confirmAbandon = false;
        return;
    }
    ImGui::OpenPopup("Abandon expedition?");
    if (ImGui::BeginPopupModal("Abandon expedition?", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar)) {
        ui::DecorateCurrentWindow();
        ui::HeadingText("LEAVE EXPEDITION?", 22.f);
        ImGui::Text("The expedition is saved at every room. Leaving mid-battle restarts this room");
        ImGui::Text("when you continue. Abandoning deletes the save: run EXP, nodes, runes and materials");
        ImGui::Text("are lost. The cleared-dungeon checkpoint remains.");
        if (ui::Button("Keep exploring", {180, 36})) {
            ui.confirmAbandon = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (allowLeave) {
            if (ui::Button("Save and leave", {180, 36})) {
                run->leaveRequested = true;
                ui.confirmAbandon = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ui::Button("Abandon expedition (checkpoint kept)", {260, 36})) {
            if (viaIntent) {
                PostRunIntent(registry, ERunIntent::AbandonRun);
            } else {
                run->abandonRequested = true;
            }
            ui.confirmAbandon = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

namespace {

/// WP-10 curation: the camp surfaces at most this many reinforcement candidates per member up front; any
/// remaining unsaturated stats stay reachable under "More stats" (nothing is silently unpurchasable).
constexpr int kCampReinforceCandidates = 3;

/// One reinforcement row: display name +amount, the shared curve's price, current/cap, and the refusal
/// reason on hover.
void DrawCampUpgradeRow(entt::registry& registry, FDungeonRun& run, const FDungeonGrowth& growth,
                        std::size_t memberIndex, std::size_t upgradeIndex, const FCampSession& session) {
    const FStatUpgrade& upgrade = growth.statUpgrades[upgradeIndex];
    const int current = DungeonMemberStatBonus(run.party[memberIndex], upgrade.stat);
    const auto problem = ValidateDungeonStatUpgrade(registry, memberIndex, upgradeIndex);
    const std::string label = upgrade.displayName + " +" + std::to_string(upgrade.amount) + "  (" +
        std::to_string(session.upgradeCost) + " materials, " + std::to_string(current) + "/" +
        std::to_string(upgrade.cap) + ")";
    ImGui::BeginDisabled(problem.has_value());
    const bool bought = ImGui::SmallButton(label.c_str());
    ImGui::EndDisabled();
    if (problem && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", problem->c_str());
    }
    if (!bought) return;
    if (const auto error = UpgradeDungeonStat(registry, memberIndex, upgradeIndex)) {
        run.message = "Cannot reinforce: " + *error;
    } else {
        run.message = upgrade.displayName + " reinforced.";
    }
}

/// The camp's per-member section: a 2–3 candidate reinforcement shortlist, the rest of the stat list under
/// "More stats", then the free loadout services (rune fitting, passive nodes) one level deeper.
void DrawCampMemberSection(entt::registry& registry, FDungeonRun& run, std::size_t memberIndex,
                           const FCampSession& session) {
    const auto* growth = registry.ctx().find<FDungeonGrowth>();
    if (growth == nullptr) return;
    ImGui::SeparatorText("Reinforce");
    ImGui::Text("Next purchase: %d materials (shared curve, %u bought so far)", session.upgradeCost,
                session.upgradesTotal);
    std::vector<std::size_t> candidates;
    for (std::size_t upgradeIndex = 0; upgradeIndex < growth->statUpgrades.size(); ++upgradeIndex) {
        const FStatUpgrade& upgrade = growth->statUpgrades[upgradeIndex];
        const int current = DungeonMemberStatBonus(run.party[memberIndex], upgrade.stat);
        if (current + upgrade.amount <= upgrade.cap) candidates.push_back(upgradeIndex);
    }
    if (candidates.empty()) ImGui::TextDisabled("All stats are at their run cap.");
    const std::size_t shortlist =
        std::min<std::size_t>(candidates.size(), static_cast<std::size_t>(kCampReinforceCandidates));
    for (std::size_t index = 0; index < shortlist; ++index) {
        ImGui::PushID(static_cast<int>(candidates[index]));
        DrawCampUpgradeRow(registry, run, *growth, memberIndex, candidates[index], session);
        ImGui::PopID();
    }
    if (candidates.size() > shortlist) {
        // Nothing is hidden: the shortlist is curation, the full stat list stays one click away.
        const std::string moreLabel = "More stats (" + std::to_string(candidates.size() - shortlist) + ")";
        if (ImGui::TreeNodeEx(moreLabel.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
            for (std::size_t index = shortlist; index < candidates.size(); ++index) {
                ImGui::PushID(static_cast<int>(candidates[index]));
                DrawCampUpgradeRow(registry, run, *growth, memberIndex, candidates[index], session);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }

    if (ImGui::TreeNodeEx("Skills & runes (free)", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        auto& member = run.party[memberIndex];
        const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        ImGui::PushID("runes");
        for (std::size_t slotIndex = 0; slotIndex < member.skills.size(); ++slotIndex) {
            const auto& slot = member.skills[slotIndex];
            ImGui::PushID(static_cast<int>(slotIndex));
            // WP-27: the same display-name rule the reward cards use (id fallback); the raw id stays on hover.
            const auto* skill = content != nullptr ? content->skills.find(slot.skillId) : nullptr;
            ImGui::Text("%s", ContentDisplayName(skill, slot.skillId).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", slot.skillId.c_str());
            for (std::size_t runeIndex = 0; runeIndex < slot.runeIds.size(); ++runeIndex) {
                const auto* fitted = content != nullptr ? content->runes.find(slot.runeIds[runeIndex]) : nullptr;
                const auto label = ContentDisplayName(fitted, slot.runeIds[runeIndex]) + "  [remove]";
                if (ImGui::SmallButton(label.c_str())) {
                    if (const auto error = RemoveDungeonRune(registry, memberIndex, slotIndex, runeIndex)) {
                        run.message = "Cannot remove rune: " + *error;
                    } else {
                        run.message = "Rune returned to the collected inventory.";
                    }
                    break;
                }
            }
            if (ImGui::BeginCombo("Fit rune", "Choose collected rune")) {
                for (std::size_t index = 0; index < run.runes.size(); ++index) {
                    ImGui::PushID(static_cast<int>(index));
                    const auto problem = ValidateDungeonRuneEquip(registry, memberIndex, slotIndex, index);
                    ImGui::BeginDisabled(problem.has_value());
                    const auto* collected = content != nullptr ? content->runes.find(run.runes[index]) : nullptr;
                    if (ImGui::Selectable(ContentDisplayName(collected, run.runes[index]).c_str())) {
                        if (const auto error = EquipDungeonRune(registry, memberIndex, slotIndex, index)) {
                            run.message = "Cannot fit rune: " + *error;
                        } else {
                            run.message = "Rune fitted to the selected skill.";
                        }
                        ImGui::EndDisabled();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::EndDisabled();
                    if (problem && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("%s", problem->c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::TextDisabled("Fitting and removing runes is free (D-17).");
        ImGui::TreePop();
    }
    ImGui::SeparatorText("Passive nodes");
    DrawGrowthPanel(registry, *growth, run, memberIndex);
}

} // namespace

/// The camp screen (WP-10 rewrite): the arrival heal, the forecast and the optional next-dungeon preview at
/// the top; a curated services column (paid heal / respec / camp rune-offer purchase, one decision per
/// purchase, prices always visible); then one collapsible per member (reinforcement shortlist + the free
/// loadout edits). The descent is the only run transition; abandon stays behind the shared confirm.
void RenderDungeonCampView(entt::registry& registry) {
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
        DrawDungeonIcon(registry, EDungeonSprite::Stairs);
        ImGui::SameLine();
        ui::HeadingText("DUNGEON CLEARED / CAMP", 26.f);
        ImGui::TextWrapped("%s", run.message.c_str());
        ImGui::Separator();
        const auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
        if (!checkpoint.error.empty()) ImGui::TextColored({.9f, .31f, .31f, 1}, "%s", checkpoint.error.c_str());
        ImGui::Text("Checkpoint: dungeon %d cleared  |  materials %d", run.cleared, run.materials);
        DrawRuneOfferForecast(registry);
        // WP-08 authors `next_dungeon_hint`; the preview is conditional on that data (nothing shown today).
        const auto& data = DungeonDefinition(registry);
        if (const auto hint = data.find("next_dungeon_hint");
            hint != data.end() && hint->is_string() && !hint->get_ref<const std::string&>().empty()) {
            ImGui::TextDisabled("Next dungeon: %s", hint->get_ref<const std::string&>().c_str());
        }
        const FCampSession session = BuildCampSession(registry);
        const auto& recovery = DungeonRecovery(registry);
        if (recovery.campAutoHeal > 0) ImGui::TextWrapped("Healed up to %d HP on the way in.", recovery.campAutoHeal);

        auto* rewards = registry.ctx().find<FRewards>();
        const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        const bool offerOpen = rewards != nullptr && rewards->offer.active;

        ImGui::SeparatorText("REST OR REINFORCE");
        switch (session.choice) {
        case ECampChoice::None: {
            ImGui::TextWrapped(
                "Take one at this camp: rest, or reinforce stats. Rune offers, node reallocation and the loadout "
                "stay open either way.");
            const auto restProblem = ValidateDungeonCampRest(registry);
            const FCampRestPreview preview = DungeonCampRestPreview(registry);
            const std::string restLabel = "Rest  /  free  /  +" + std::to_string(session.restHealPercent) +
                "% max HP each  /  party HP " + std::to_string(preview.partyHp) + " -> " +
                std::to_string(preview.partyHpAfter) + " of " + std::to_string(preview.partyHpMax);
            ImGui::BeginDisabled(restProblem.has_value());
            if (ui::Button(restLabel.c_str(), {-1, 42})) {
                if (const auto error = RestAtDungeonCamp(registry)) {
                    run.message = "Cannot rest: " + *error;
                } else {
                    run.message = "The party rests.";
                }
            }
            ImGui::EndDisabled();
            if (restProblem) ImGui::TextDisabled("%s", restProblem->c_str());
            ImGui::TextDisabled("Reinforce: buy any stat upgrade below. The first purchase gives up Rest at this camp.");
            break;
        }
        case ECampChoice::Rest:
            ImGui::TextDisabled("Rested at this camp. Stat upgrades are closed until the next camp.");
            break;
        case ECampChoice::Reinforce:
            ImGui::TextDisabled("Reinforcing at this camp. Rest is closed until the next camp.");
            break;
        }

        ImGui::SeparatorText("CAMP SERVICES");
        if (offerOpen) {
            ImGui::TextDisabled("Rune offer bought — pick one or skip it before descending.");
            DrawRuneOffers(registry, *rewards, content);
        } else {
            // The camp rune offer (E1 late sink): price, per-camp cap and the discount, all from the data.
            const auto offerProblem = rewards != nullptr ? ValidateCampRuneOfferPurchase(registry)
                                                         : std::optional<std::string>("The reward offer module is unavailable.");
            std::string offerLabel = "Buy rune offer  /  " + std::to_string(session.runeOfferCost) + " materials";
            if (session.runeOfferDiscounted) offerLabel += " (special)";
            offerLabel += "  /  " + std::to_string(session.runeDrawsThisCamp) + " of " +
                std::to_string(session.runeOfferCampCap) + " this camp";
            ImGui::BeginDisabled(offerProblem.has_value());
            if (ui::Button(offerLabel.c_str(), {-1, 42})) {
                if (const auto error = PurchaseCampRuneOffer(registry)) {
                    run.message = "Cannot buy a rune offer: " + *error;
                } else {
                    run.message = "Rune offer purchased — pick one or skip it.";
                }
            }
            ImGui::EndDisabled();
            if (offerProblem) ImGui::TextDisabled("%s", offerProblem->c_str());
        }
        {
            const auto respecProblem = ValidateDungeonRespec(registry);
            const std::string respecLabel =
                "Reallocate passive nodes  /  " + std::to_string(session.respecCost) + " materials";
            ImGui::BeginDisabled(respecProblem.has_value());
            if (ui::Button(respecLabel.c_str(), {-1, 42})) {
                if (const auto error = RespecDungeonNodes(registry)) {
                    run.message = "Cannot reallocate: " + *error;
                } else {
                    run.message = "Allocated nodes returned to the node points.";
                }
            }
            ImGui::EndDisabled();
            if (respecProblem) ImGui::TextDisabled("%s", respecProblem->c_str());
        }

        ImGui::SeparatorText("LOADOUT (free)");
        auto& selection = CampLoadoutSelection(registry);
        if (const auto edit = DrawLoadoutPanel(BuildCampLoadoutModel(registry, selection))) {
            (void)ApplyCampLoadoutEdit(registry, selection, *edit);
        }

        for (std::size_t memberIndex = 0; memberIndex < run.party.size(); ++memberIndex) {
            auto& member = run.party[memberIndex];
            ImGui::PushID(static_cast<int>(memberIndex));
            ImGui::SetNextItemOpen(memberIndex == 0, ImGuiCond_FirstUseEver);
            if (ImGui::TreeNode(MemberName(member).c_str())) {
                DrawCampMemberSection(registry, run, memberIndex, session);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (offerOpen) ImGui::TextDisabled("Pick a rune or skip the offer to descend.");
        ImGui::BeginDisabled(offerOpen);
        if (ui::Button("Descend to the next dungeon", {-1, 42})) {
            PostRunIntent(registry, ERunIntent::LeaveCamp);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (ui::Button("End expedition at checkpoint", {-1, 30})) {
            ui.confirmAbandon = true;
        }
        ImGui::EndPopup();
    }
    DrawAbandonConfirm(registry, false, true);
}

} // namespace game

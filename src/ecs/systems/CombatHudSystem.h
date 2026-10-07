#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include <entt/entt.hpp>
#include <imgui.h>

#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSprite.h"
#include "ecs/components/FStats.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/grid/FBattleLayout.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridSelection.h"
#include "presentation/FUiTheme.h"

namespace game::ecs {

/// Presentation only: skill selection is shared with grid input; combat systems own resolution.
struct CombatHudSystem {
    void update(entt::registry&, float) {}

    void render(entt::registry& registry) {
        auto* outcome = registry.ctx().find<FBattleOutcome>();
        auto* combat = registry.ctx().find<FCombatRuntime>();
        auto* runtime = registry.ctx().find<FTurnRuntime>();
        const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        if (!outcome || !combat || !runtime || !content) return;
        const auto size = ImGui::GetIO().DisplaySize;
        const FViewport viewport{size.x, size.y};
        const auto layout = BattleLayout(viewport);
        const auto snapshot = runtime->controller.snapshot();
        const auto actor = snapshot.currentTurn ? snapshot.currentTurn->actor : entt::null;
        auto* selection = registry.ctx().find<FGridSelection>();
        const bool playerTurn = !outcome->resolved && !combat->autoPlay && IsAlive(registry, actor)
            && registry.all_of<FPartyMember>(actor);
        DrawWorldHealth(registry, viewport, actor);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));

        BeginPanel("Forest initiative", {8.f, 8.f}, {size.x - 16.f, layout.headerHeight - 16.f});
        ImGui::BeginGroup();
        ImGui::TextColored({0.83f, 0.74f, 0.46f, 1.f}, "THE MOSSBOUND CROSSING");
        ImGui::TextDisabled("FOREST I  /  Round %llu", static_cast<unsigned long long>(snapshot.roundNumber));
        ImGui::EndGroup();
        const float chipWidth = std::clamp((size.x - 290.f) / std::max(1.f, static_cast<float>(snapshot.entries.size())) - 6.f, 60.f, 138.f);
        for (const auto& entry : snapshot.entries) {
            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(entt::to_integral(entry.entity)));
            const bool current = entry.entity == actor;
            const auto label = std::string(current ? "> " : "") + UnitDisplayName(registry, entry.entity);
            ImGui::BeginDisabled(!IsAlive(registry, entry.entity));
            if (ui::Button(label.c_str(), {chipWidth, 36.f}) && selection) selection->selected = entry.entity;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s | Initiative %d%s", UnitDisplayName(registry, entry.entity).c_str(), entry.speed,
                entry.state == ETurnEntryState::Completed ? " | Acted" : "");
            ImGui::PopID();
        }
        ImGui::End();

        BeginPanel("Expedition party", {8.f, layout.headerHeight}, {layout.partyWidth - 16.f, size.y - layout.headerHeight - layout.dockHeight - 8.f});
        ImGui::TextColored({0.83f, 0.74f, 0.46f, 1.f}, "YOUR PARTY");
        for (const auto entity : registry.view<FPartyMember, FHealth>()) {
            ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
            ImGui::Separator();
            const auto* sprite = registry.try_get<FSprite>(entity);
            const auto* resources = registry.ctx().find<FResourceManager>();
            const auto* image = sprite && resources ? resources->find(sprite->texture) : nullptr;
            if (image) {
                ImGui::Image(TextureId(image), {40.f, 40.f});
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::TextUnformatted(UnitDisplayName(registry, entity).c_str());
            ImGui::TextDisabled(entity == actor ? "Acting now" : (IsAlive(registry, entity) ? "Ready" : "Down"));
            ImGui::EndGroup();
            const auto& hp = registry.get<FHealth>(entity);
            const auto label = std::to_string(hp.current) + " / " + std::to_string(hp.max);
            ImGui::ProgressBar(HealthFraction(hp), {-1.f, 14.f}, label.c_str());
            if (const auto* ap = registry.try_get<FActionPoints>(entity)) ImGui::TextDisabled("Move %d  Skill %d", ap->move, ap->skill);
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::TextWrapped("Select an action, then click a target in the clearing.");
        ImGui::TextDisabled("Wheel: zoom");
        ImGui::TextDisabled("WASD: pan");
        if (ui::Button("Recenter", {-1.f, 25.f})) {
            if (const auto* metrics = registry.ctx().find<FGridMetrics>()) {
                for (const auto entity : registry.view<FCamera, FTag>()) {
                    if (registry.get<FTag>(entity).value == "battle_camera") registry.get<FCamera>(entity) = FitBattleCamera(*metrics, viewport);
                }
            }
        }
        ImGui::End();

        if (layout.journalWidth > 0.f) {
            BeginPanel("Field journal", {size.x - layout.journalWidth, layout.headerHeight},
                       {layout.journalWidth - 8.f, size.y - layout.headerHeight - layout.dockHeight - 8.f});
            ImGui::TextColored({0.83f, 0.74f, 0.46f, 1.f}, "CLEAR THE CROSSING");
            ImGui::TextWrapped("Defeat the hostile scouts. Trees beyond the grid mark the clearing's edge.");
            entt::entity inspected = selection ? selection->selected : entt::null;
            if (selection && selection->hovered) {
                const auto hovered = FindGridOccupant(registry, selection->hovered->x, selection->hovered->y);
                if (hovered != entt::null) inspected = hovered;
            }
            if (registry.valid(inspected)) {
                ImGui::SeparatorText("Inspect");
                ImGui::TextUnformatted(UnitDisplayName(registry, inspected).c_str());
                if (const auto* hp = registry.try_get<FHealth>(inspected)) ImGui::Text("Health %d / %d", hp->current, hp->max);
                if (const auto* stats = registry.try_get<FStats>(inspected)) ImGui::TextDisabled("Armor %d | Resist %d", stats->defense, stats->magicDefense);
            }
            ImGui::SeparatorText("Battle journal");
            ImGui::BeginChild("Recent events", {0.f, 0.f});
            if (const auto* log = registry.ctx().find<FCombatLog>()) {
                const auto start = log->lines.size() > 8 ? log->lines.size() - 8 : 0;
                for (std::size_t i = start; i < log->lines.size(); ++i) ImGui::TextWrapped("%s", log->lines[i].c_str());
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.f) ImGui::SetScrollHereY(1.f);
            }
            ImGui::EndChild();
            ImGui::End();
        }

        BeginPanel("Combat actions", {8.f, size.y - layout.dockHeight}, {size.x - 16.f, layout.dockHeight - 8.f});
        if (outcome->resolved) {
            ImGui::TextColored({0.83f, 0.74f, 0.46f, 1.f}, outcome->victory ? "VICTORY — CROSSING SECURED" : "DEFEAT — EXPEDITION LOST");
            ImGui::TextWrapped(outcome->victory ? "The path through the forest is open. Return to camp to prepare the next encounter." : "Return to camp and revise your party's skills and support runes.");
            if (ui::Button("Return to camp", {180.f, 38.f})) outcome->exitRequested = true;
        } else if (snapshot.currentTurn) {
            const auto* ap = registry.try_get<FActionPoints>(actor);
            ImGui::Text("%s  |  Movement %d  |  Skill AP %d", UnitDisplayName(registry, actor).c_str(), ap ? ap->move : 0, ap ? ap->skill : 0);
            ImGui::SameLine();
            ImGui::TextDisabled(playerTurn ? "Choose an action, then click its target" : "Enemy turn...");
            ImGui::BeginDisabled(!playerTurn);
            const auto* loadout = registry.try_get<FSkillLoadout>(actor);
            const float count = 2.f + (loadout ? static_cast<float>(loadout->slots.size()) : 0.f);
            const float buttonWidth = std::clamp((size.x - 45.f) / count - 8.f, 60.f, 126.f);
            if (ActionButton(registry, "Move", "boots", combat->moveSelected, buttonWidth, ap && ap->move > 0)) combat->moveSelected = true;
            if (loadout) {
                for (int index = 0; index < static_cast<int>(loadout->slots.size()); ++index) {
                    const auto& slot = loadout->slots[index];
                    const auto skill = ResolveSkillSlot(slot, *content);
                    const auto* row = content->skills.find(slot.skillId);
                    if (!skill || !row) continue;
                    ImGui::SameLine();
                    ImGui::PushID(index);
                    const char* icon = skill->damageType == gameplay::kDamageTypeMagical ? "spell" : (skill->range > 2 ? "bow" : "sword");
                    if (ActionButton(registry, row->displayName.c_str(), icon, !combat->moveSelected && combat->selectedSkill == index,
                                     buttonWidth, ap && ap->skill >= skill->apCost)) {
                        combat->selectedSkill = index;
                        combat->moveSelected = false;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s | AP %d", row->displayName.c_str(), skill->apCost);
                        ImGui::Text("Range %d | Power %d | %s", skill->range, skill->power, skill->shape.c_str());
                        for (const auto& id : slot.runeIds) {
                            if (const auto* rune = content->runes.find(id)) ImGui::Text("Support: %s", rune->displayName.c_str());
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::SameLine();
            if (ui::Button("End turn", {buttonWidth, 65.f})) runtime->requests.push_back(FEndTurn{*snapshot.currentTurn});
            ImGui::EndDisabled();
            if (combat->moveSelected) {
                ImGui::TextDisabled("MOVE  |  Click a blue reachable cell. Movement AP is separate from skill AP.");
            } else if (loadout && combat->selectedSkill >= 0 && combat->selectedSkill < static_cast<int>(loadout->slots.size())) {
                const auto& slot = loadout->slots[combat->selectedSkill];
                if (const auto skill = ResolveSkillSlot(slot, *content)) ImGui::TextDisabled("AP %d  |  Range %d  |  Power %d  |  %s  |  Target: %s  |  Supports %zu",
                    skill->apCost, skill->range, skill->power, skill->shape.c_str(), skill->target.c_str(), slot.runeIds.size());
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    [[nodiscard]] std::string name() const { return "CombatHudSystem"; }

private:
    static void BeginPanel(const char* title, ImVec2 position, ImVec2 size) {
        ImGui::SetNextWindowPos(position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);
        ui::DrawPanel(ImGui::GetWindowDrawList(), position, {position.x + size.x, position.y + size.y});
    }

    static ImTextureID TextureId(const FTextureInfo* texture) {
        return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->texture));
    }

    static float HealthFraction(const FHealth& hp) {
        return hp.max > 0 ? std::clamp(static_cast<float>(hp.current) / hp.max, 0.f, 1.f) : 0.f;
    }

    static bool ActionButton(entt::registry& registry, const char* label, const char* icon, bool selected, float width, bool affordable) {
        ImGui::BeginDisabled(!affordable);
        const auto p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(label, {width, 65.f});
        auto* draw = ImGui::GetWindowDrawList();
        const bool hover = ImGui::IsItemHovered();
        const ImVec2 end{p.x + width, p.y + 65.f};
        draw->AddRectFilled(p, end, ImGui::GetColorU32(hover ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
        draw->AddRect(p, end, selected ? ui::palette::kGoldBright : ui::palette::kBronze, 0.f, 0, 2.f);
        const auto* assets = registry.ctx().find<FAssetManager>();
        auto* resources = registry.ctx().find<FResourceManager>();
        if (assets && resources) {
            if (const auto handle = resources->loadTexture(*assets, std::string("art/icon_") + icon)) {
                if (const auto* texture = resources->find(*handle)) draw->AddImage(TextureId(texture), {p.x + width * 0.5f - 16.f, p.y + 4.f},
                    {p.x + width * 0.5f + 16.f, p.y + 36.f}, {0.f, 0.f}, {1.f, 1.f}, IM_COL32(255, 255, 255, affordable ? 255 : 85));
            }
        }
        draw->PushClipRect(p, end, true);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {p.x + 5.f, p.y + 38.f},
                      affordable ? IM_COL32(230, 232, 207, 255) : IM_COL32(125, 139, 126, 255), label, nullptr, width - 10.f);
        draw->PopClipRect();
        ImGui::EndDisabled();
        return clicked;
    }

    static void DrawWorldHealth(entt::registry& registry, const FViewport& viewport, entt::entity actor) {
        const auto* metrics = registry.ctx().find<FGridMetrics>();
        if (!metrics) return;
        FCamera camera{};
        for (const auto entity : registry.view<const FCamera, const FTag>()) {
            if (registry.get<FTag>(entity).value == "battle_camera") camera = registry.get<FCamera>(entity);
        }
        const auto layout = BattleLayout(viewport);
        auto* draw = ImGui::GetBackgroundDrawList();
        draw->PushClipRect({layout.partyWidth, layout.headerHeight}, {viewport.width - layout.journalWidth, viewport.height - layout.dockHeight}, true);
        for (const auto entity : registry.view<const FGridPosition, const FHealth>()) {
            const auto& hp = registry.get<FHealth>(entity);
            if (hp.current <= 0) continue;
            const auto cell = registry.get<FGridPosition>(entity);
            const auto screen = WorldToScreen(GridCellTopLeftToWorld(*metrics, cell.x, cell.y), camera, viewport);
            const float width = metrics->tileSize * camera.zoom * 0.7f;
            const float x = screen.x + metrics->tileSize * camera.zoom * 0.15f;
            const float y = screen.y + 1.f;
            draw->AddRectFilled({x - 1.f, y - 1.f}, {x + width + 1.f, y + 5.f}, IM_COL32(13, 23, 24, 235));
            draw->AddRectFilled({x, y}, {x + width * HealthFraction(hp), y + 4.f}, registry.all_of<FPartyMember>(entity)
                ? IM_COL32(80, 140, 255, 255) : IM_COL32(230, 80, 80, 255));
            if (entity == actor) draw->AddTriangleFilled({x + width * 0.5f - 4.f, y - 9.f}, {x + width * 0.5f + 4.f, y - 9.f},
                {x + width * 0.5f, y - 4.f}, IM_COL32(255, 220, 60, 255));
        }
        draw->PopClipRect();
    }
};

} // namespace game::ecs

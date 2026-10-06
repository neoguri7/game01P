#pragma once

#include <string>

#include <entt/entt.hpp>
#include <imgui.h>

#include "ecs/components/FActionPoints.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FStats.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"

namespace game::ecs {

/// The battle UI (docs/design/skill-rune-progression.md 기반 최소 플레이 화면): turn order, party/enemy
/// status, skill + target picking for the current party actor, the battle log and the result banner.
/// It only queues requests; TurnFlowSystem/ActionResolverSystem own the rules.
struct CombatHudSystem {
    int selectedSkill{0};
    entt::entity boundActor{entt::null};

    void update(entt::registry& /*registry*/, float /*deltaTime*/) {}

    void render(entt::registry& registry) {
        auto* outcome = registry.ctx().find<FBattleOutcome>();
        if (!outcome) {
            return; // not in a battle
        }

        ImGui::SetNextWindowPos(ImVec2(660, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
        ImGui::Begin("전투");

        auto* runtime = registry.ctx().find<FTurnRuntime>();
        auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        auto* log = registry.ctx().find<FCombatLog>();
        auto* queue = registry.ctx().find<FActionQueue>();

        const FTurnSnapshot snapshot = runtime ? runtime->controller.snapshot() : FTurnSnapshot{};
        ImGui::Text("라운드 %llu", static_cast<unsigned long long>(snapshot.roundNumber));
        if (snapshot.currentTurn) {
            ImGui::SameLine();
            ImGui::Text("| 현재 턴: %s", UnitDisplayName(registry, snapshot.currentTurn->actor).c_str());
        }

        ImGui::SeparatorText("아군");
        for (const auto entity : registry.view<FPartyMember, FHealth>()) {
            const auto& health = registry.get<FHealth>(entity);
            const bool alive = IsAlive(registry, entity);
            ImGui::Text("%s %d/%d", UnitDisplayName(registry, entity).c_str(), health.current, health.max);
            if (const auto* stats = registry.try_get<FStats>(entity)) {
                ImGui::SameLine();
                ImGui::TextDisabled("공%d 마%d 방%d 마방%d", stats->attack, stats->magicAttack,
                                    stats->defense, stats->magicDefense);
            }
            if (const auto* points = registry.try_get<FActionPoints>(entity)) {
                ImGui::SameLine();
                ImGui::TextDisabled("(이동 %d/%d, 스킬 %d/%d)", points->move, points->moveMax,
                                    points->skill, points->skillMax);
            }
            if (!alive) {
                ImGui::SameLine();
                ImGui::TextDisabled("쓰러짐");
            }
        }

        ImGui::SeparatorText("적");
        for (const auto entity : registry.view<FEnemy, FHealth>()) {
            const auto& health = registry.get<FHealth>(entity);
            ImGui::Text("%s %d/%d", UnitDisplayName(registry, entity).c_str(), health.current, health.max);
            if (!IsAlive(registry, entity)) {
                ImGui::SameLine();
                ImGui::TextDisabled("쓰러짐");
            }
        }

        if (outcome->resolved) {
            ImGui::Separator();
            if (outcome->victory) {
                ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "승리!");
            } else {
                ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "패배 — 파티 전멸");
            }
            if (ImGui::Button("거점으로")) {
                outcome->exitRequested = true;
            }
        } else if (snapshot.currentTurn && content && queue) {
            const entt::entity actor = snapshot.currentTurn->actor;
            if (registry.all_of<FPartyMember>(actor) && IsAlive(registry, actor)) {
                const auto* loadout = registry.try_get<FSkillLoadout>(actor);
                if (boundActor != actor) {
                    boundActor = actor;
                    selectedSkill = 0;
                }

                ImGui::SeparatorText("행동");
                if (loadout && !loadout->slots.empty()) {
                    for (int index = 0; index < static_cast<int>(loadout->slots.size()); ++index) {
                        const FSkillSlot& slot = loadout->slots[index];
                        const auto resolved = ResolveSkillSlot(slot, *content);
                        std::string label = slot.skillId;
                        if (resolved) {
                            if (const auto* row = content->skills.find(slot.skillId)) {
                                label = row->displayName;
                            }
                            label += " (AP " + std::to_string(resolved->apCost) + ")";
                            if (!slot.runeIds.empty()) {
                                label += " [룬 " + std::to_string(slot.runeIds.size()) + "]";
                            }
                        }
                        ImGui::RadioButton(label.c_str(), &selectedSkill, index);
                    }

                    if (selectedSkill >= 0 && selectedSkill < static_cast<int>(loadout->slots.size())) {
                        const FSkillSlot& slot = loadout->slots[selectedSkill];
                        const auto resolved = ResolveSkillSlot(slot, *content);
                        if (resolved) {
                            ImGui::TextDisabled("형태 %s | 사거리 %d | 위력 %d | %s%s", resolved->shape.c_str(),
                                                resolved->range, resolved->power,
                                                resolved->damageType == gameplay::kDamageTypeMagical ? "마법" : "물리",
                                                resolved->statuses.empty() ? "" : " | 상태 부여");
                        }
                        ImGui::SeparatorText("대상");
                        // One button per target, side read from the caster's perspective — the same rule
                        // ActionResolverSystem enforces: "ally"/"self" list the caster's side ("self" only
                        // the caster), "enemy"/"cell" the other side ("cell" uses enemy units as anchors
                        // until grid-cell picking exists).
                        const auto drawTarget = [&](entt::entity target) {
                            if (!IsAlive(registry, target)) {
                                return;
                            }
                            const auto* cell = registry.try_get<FGridPosition>(target);
                            const auto* health = registry.try_get<FHealth>(target);
                            if (cell == nullptr || health == nullptr) {
                                return;
                            }
                            const std::string label = UnitDisplayName(registry, target) + " ("
                                + std::to_string(cell->x) + "," + std::to_string(cell->y) + ") HP "
                                + std::to_string(health->current) + "/" + std::to_string(health->max);
                            if (ImGui::Button(label.c_str())) {
                                queue->pending.push_back(FActionRequest{actor, target, slot.skillId});
                            }
                        };
                        if (resolved->target == "self") {
                            drawTarget(actor);
                        } else if (resolved->target == "ally") {
                            for (const auto target : registry.view<FPartyMember, FHealth, FGridPosition>()) {
                                drawTarget(target);
                            }
                        } else {
                            for (const auto target : registry.view<FEnemy, FHealth, FGridPosition>()) {
                                drawTarget(target);
                            }
                        }
                    }
                } else {
                    ImGui::TextDisabled("사용할 스킬이 없다");
                }

                if (ImGui::Button("턴 종료")) {
                    runtime->requests.push_back(FEndTurn{*snapshot.currentTurn});
                }
            } else {
                ImGui::SeparatorText("행동");
                ImGui::TextDisabled("적의 턴...");
            }
        }

        ImGui::SeparatorText("전투 기록");
        ImGui::BeginChild("log", ImVec2(0, 160), ImGuiChildFlags_Borders);
        if (log) {
            for (const std::string& line : log->lines) {
                ImGui::TextWrapped("%s", line.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::End();
    }

    [[nodiscard]] std::string name() const { return "CombatHudSystem"; }
};

} // namespace game::ecs

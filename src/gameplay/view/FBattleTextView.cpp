#include "gameplay/view/FBattleTextView.h"

#include "gameplay/components/FApPool.h"
#include "gameplay/components/FBattleDefeat.h"
#include "gameplay/components/FBattleOngoing.h"
#include "gameplay/components/FBattleRound.h"
#include "gameplay/components/FBattleVictory.h"
#include "gameplay/components/FDisplayName.h"
#include "gameplay/components/FDowned.h"
#include "gameplay/components/FGridPosition.h"
#include "gameplay/components/FHealth.h"
#include "gameplay/components/FInitiative.h"
#include "gameplay/components/FSkillSet.h"
#include "gameplay/components/FTeamEnemy.h"
#include "gameplay/components/FTeamPlayer.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FSkillContent.h"
#include "gameplay/rules/FGridDistance.h"
#include "gameplay/rules/FTargetSelection.h"
#include "gameplay/rules/FTurnActor.h"
#include "gameplay/run/FBattleLog.h"
#include "gameplay/run/FBattleState.h"
#include "gameplay/run/FCommandSelection.h"

#include <entt/entt.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace game::gameplay {
namespace {

/// A unit's printable identity on the grid: 'A'.. for the party, 'a'.. for enemies, assigned in spawn order.
/// why a glyph instead of the entity id: the grid is 8 columns wide, and a readable map needs one character per
/// cell; the legend below it turns the glyph back into a name and a number.
struct FUnitGlyph {
    entt::entity entity{entt::null};
    char glyph{'?'};
};

struct FBattleHeader {
    int round{0};
    const char* phase{"없음"};
};

[[nodiscard]] std::vector<FUnitGlyph> collectGlyphs(entt::registry& registry) {
    std::vector<entt::entity> units;
    for (const auto entity : registry.view<FTeamPlayer, FGridPosition>()) {
        units.push_back(entity);
    }
    for (const auto entity : registry.view<FTeamEnemy, FGridPosition>()) {
        units.push_back(entity);
    }
    // why sorted by entity id: spawn order is the order a player reads the legend in, and sorting makes the map
    // independent of registry storage iteration (R16).
    std::sort(units.begin(), units.end());

    std::vector<FUnitGlyph> glyphs;
    glyphs.reserve(units.size());
    char nextParty = 'A';
    char nextEnemy = 'a';
    for (const entt::entity entity : units) {
        const bool playerSide = registry.all_of<FTeamPlayer>(entity);
        glyphs.push_back(FUnitGlyph{entity, playerSide ? nextParty++ : nextEnemy++});
    }
    return glyphs;
}

[[nodiscard]] std::string unitName(entt::registry& registry, entt::entity unit) {
    if (unit == entt::null || !registry.valid(unit)) {
        return "?";
    }
    const FDisplayName* name = registry.try_get<FDisplayName>(unit);
    return name != nullptr ? name->text : fmt::format("entity {}", static_cast<int>(unit));
}

[[nodiscard]] char glyphAt(const std::vector<FUnitGlyph>& glyphs, entt::registry& registry, int x, int y) {
    for (const FUnitGlyph& glyph : glyphs) {
        const FGridPosition* cell = registry.try_get<FGridPosition>(glyph.entity);
        if (cell != nullptr && cell->x == x && cell->y == y) {
            return glyph.glyph;
        }
    }
    return '.';
}

[[nodiscard]] FBattleHeader header(entt::registry& registry, const FBattleState& state) {
    FBattleHeader result;
    const FBattleRound* round = registry.try_get<FBattleRound>(state.battle);
    result.round = round != nullptr ? round->value : 0;

    if (registry.all_of<FBattleVictory>(state.battle)) {
        result.phase = "승리 (전투 종료)";
    } else if (registry.all_of<FBattleDefeat>(state.battle)) {
        result.phase = "패배 (파티 전멸)";
    } else if (registry.all_of<FBattleOngoing>(state.battle)) {
        result.phase = "진행 중";
    }
    return result;
}

void appendGrid(std::vector<std::string>& lines, entt::registry& registry, const FBattleState& state) {
    const std::vector<FUnitGlyph> glyphs = collectGlyphs(registry);

    std::string header2 = "    ";
    for (int x = 0; x < state.gridWidth; ++x) {
        header2 += fmt::format("{} ", x);
    }
    lines.push_back(header2);

    for (int y = 0; y < state.gridHeight; ++y) {
        std::string row = fmt::format("{:>2} |", y);
        for (int x = 0; x < state.gridWidth; ++x) {
            row += fmt::format("{} ", glyphAt(glyphs, registry, x, y));
        }
        lines.push_back(row);
    }

    for (const FUnitGlyph& glyph : glyphs) {
        const FHealth* health = registry.try_get<FHealth>(glyph.entity);
        const FApPool* pool = registry.try_get<FApPool>(glyph.entity);
        const FGridPosition* cell = registry.try_get<FGridPosition>(glyph.entity);
        const FInitiative* initiative = registry.try_get<FInitiative>(glyph.entity);
        const bool downed = registry.all_of<FDowned>(glyph.entity);

        lines.push_back(fmt::format("  {} {}{}  HP {:.0f}/{:.0f}  AP 이동 {}/스킬 {}  ({}, {})  속도 {:.0f}",
                                    glyph.glyph,
                                    unitName(registry, glyph.entity),
                                    downed ? " [쓰러짐]" : "",
                                    health != nullptr ? health->current : 0.0,
                                    health != nullptr ? health->max : 0.0,
                                    pool != nullptr ? pool->move : 0,
                                    pool != nullptr ? pool->skill : 0,
                                    cell != nullptr ? cell->x : -1,
                                    cell != nullptr ? cell->y : -1,
                                    initiative != nullptr ? initiative->speed : 0.0));
    }
}

/// The predicted order (design §4 확정: 순서 예측 표시) — the unit acting now is marked so the player can compare
/// the prediction with what is happening.
void appendOrder(std::vector<std::string>& lines, entt::registry& registry, const FBattleState& state) {
    const entt::entity active = activeUnit(registry);
    std::string text = "예상 순서:";
    for (std::size_t index = state.cursor; index < state.order.size(); ++index) {
        const entt::entity unit = state.order[index];
        if (!registry.valid(unit)) {
            continue;
        }
        text += fmt::format(" {} {}", unit == active ? "▶" : "·", unitName(registry, unit));
    }
    lines.push_back(text);
}

void appendCommand(std::vector<std::string>& lines,
                   entt::registry& registry,
                   const FCommandSelection& selection) {
    const entt::entity unit = activeUnit(registry);
    if (unit == entt::null) {
        lines.push_back("명령: 활성 유닛 없음 (다음 턴 대기)");
        return;
    }

    lines.push_back(fmt::format("명령: {} (조작: WASD 이동, Q/E 스킬, Tab 대상, Enter 사용, Esc 턴 종료)",
                                unitName(registry, unit)));

    const FSkillSet* skills = registry.try_get<FSkillSet>(unit);
    const FContentRegistry* content = registry.ctx().find<FContentRegistry>();
    if (skills == nullptr || content == nullptr || skills->skillIds.empty()) {
        lines.push_back("  스킬 없음");
        return;
    }

    for (std::size_t index = 0; index < skills->skillIds.size(); ++index) {
        const FSkillContent* skill = content->skills.find(skills->skillIds[index]);
        lines.push_back(fmt::format("  {} {}  {} (AP {}, 사거리 {}, power {:.0f})",
                                    index == selection.skillIndex ? ">" : " ",
                                    index,
                                    skill != nullptr ? skill->displayName : skills->skillIds[index],
                                    skill != nullptr ? skill->apCost : 0,
                                    skill != nullptr ? skill->range : 0,
                                    skill != nullptr ? skill->power : 0.0));
    }

    const std::vector<entt::entity> opponents = livingOpponents(registry, unit);
    if (opponents.empty()) {
        lines.push_back("  대상 없음");
        return;
    }

    const FGridPosition* selfCell = registry.try_get<FGridPosition>(unit);
    for (std::size_t index = 0; index < opponents.size(); ++index) {
        const FGridPosition* cell = registry.try_get<FGridPosition>(opponents[index]);
        const FHealth* health = registry.try_get<FHealth>(opponents[index]);
        const int distance = (selfCell != nullptr && cell != nullptr) ? gridDistance(*selfCell, *cell) : -1;
        lines.push_back(fmt::format("  {} {}  {}  HP {:.0f}  거리 {}",
                                    index == selection.targetIndex ? ">" : " ",
                                    index,
                                    unitName(registry, opponents[index]),
                                    health != nullptr ? health->current : 0.0,
                                    distance));
    }
}

void appendLog(std::vector<std::string>& lines, entt::registry& registry) {
    const FBattleLog* log = registry.ctx().find<FBattleLog>();
    if (log == nullptr || log->lines.empty()) {
        lines.push_back("로그: (없음)");
        return;
    }

    const std::size_t tail = 14; // fallback: 표시 줄 수 — 프레젠테이션 상한이며 전투 규칙 수치가 아니다
    const std::size_t first = log->lines.size() > tail ? log->lines.size() - tail : 0;
    lines.push_back("로그 (최근):");
    for (std::size_t index = first; index < log->lines.size(); ++index) {
        lines.push_back(fmt::format("  {}", log->lines[index]));
    }
}

} // namespace

std::vector<std::string> FBattleTextView::snapshot(entt::registry& registry) {
    std::vector<std::string> lines;

    const FBattleState* state = registry.ctx().find<FBattleState>();
    if (state == nullptr || state->battle == entt::null || !registry.valid(state->battle)) {
        lines.push_back("전투 없음 — 다음 슬라인스에서 거점/던전 흐름이 붙는다 (design §2/§3 미구현)");
        return lines;
    }

    const FBattleHeader battleHeader = header(registry, *state);
    lines.push_back(fmt::format("=== 전투 (라운드 {}, {}) ===", battleHeader.round, battleHeader.phase));
    appendGrid(lines, registry, *state);
    appendOrder(lines, registry, *state);

    const FCommandSelection* selection = registry.ctx().find<FCommandSelection>();
    if (selection != nullptr) {
        appendCommand(lines, registry, *selection);
    }

    appendLog(lines, registry);
    return lines;
}

} // namespace game::gameplay

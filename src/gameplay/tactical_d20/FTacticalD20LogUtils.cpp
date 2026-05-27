#include "gameplay/tactical_d20/FTacticalD20LogUtils.h"

#include "ecs/components/FTacticalUnit.h"
#include "gameplay/tactical_d20/FTacticalD20CombatLog.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20EventLog.h"
#include "gameplay/tactical_d20/FTacticalD20StateLog.h"

#include <fmt/format.h>
#include <vector>

namespace game {
namespace {

int MaxStateLogLines(entt::registry& registry) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    return config ? config->logging.stateLogMaxLines : 32;
}

int MaxEventLogLines(entt::registry& registry) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    return config ? config->logging.eventLogMaxLines : 32;
}

int MaxCombatLogLines(entt::registry& registry) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    return config ? config->logging.combatLogMaxLines : 12;
}

void TrimLines(std::vector<std::string>& lines, int maxLines) {
    while (static_cast<int>(lines.size()) > maxLines) lines.erase(lines.begin());
}

std::string TargetLabel(entt::registry& registry, const FTacticalD20CommandDropValidatedEvent& event) {
    if (event.targetsTurnPanel) return "turn_panel";
    if (event.targetEntity != entt::null && registry.valid(event.targetEntity) && registry.all_of<FTacticalUnit>(event.targetEntity)) {
        return registry.get<FTacticalUnit>(event.targetEntity).id;
    }
    if (event.hasTargetTile) return fmt::format("tile({}, {})", event.targetTileX, event.targetTileY);
    return "none";
}

} // namespace

void AppendTacticalD20StateLog(entt::registry& registry, const std::string& line) {
    if (!registry.ctx().contains<FTacticalD20StateLog>()) registry.ctx().emplace<FTacticalD20StateLog>();
    auto& log = registry.ctx().get<FTacticalD20StateLog>();
    log.lines.push_back(line);
    TrimLines(log.lines, MaxStateLogLines(registry));
}

void AppendTacticalD20EventLog(entt::registry& registry, const std::string& line) {
    if (!registry.ctx().contains<FTacticalD20EventLog>()) registry.ctx().emplace<FTacticalD20EventLog>();
    auto& log = registry.ctx().get<FTacticalD20EventLog>();
    log.lines.push_back(line);
    TrimLines(log.lines, MaxEventLogLines(registry));
}

void AppendTacticalD20CombatLog(entt::registry& registry, const std::string& line) {
    if (!registry.ctx().contains<FTacticalD20CombatLog>()) registry.ctx().emplace<FTacticalD20CombatLog>();
    auto& log = registry.ctx().get<FTacticalD20CombatLog>();
    log.lines.push_back(line);
    TrimLines(log.lines, MaxCombatLogLines(registry));
}

void AppendTacticalD20CommandValidationLogs(entt::registry& registry, const FTacticalD20CommandDropValidatedEvent& event) {
    const auto result = event.valid ? std::string{"valid"} : fmt::format("invalid: {}", event.invalidReason);
    AppendTacticalD20EventLog(registry, fmt::format("[Event] CommandDropValidated command={} target={} result={}", event.commandId, TargetLabel(registry, event), result));
    AppendTacticalD20CombatLog(registry, fmt::format("{} command {}", event.commandId, result));
}

} // namespace game

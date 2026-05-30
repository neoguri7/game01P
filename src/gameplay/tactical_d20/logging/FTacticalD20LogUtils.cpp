#include "gameplay/tactical_d20/logging/FTacticalD20LogUtils.h"

#include "core/Logger.h"
#include "ecs/components/FTacticalUnit.h"
#include "gameplay/tactical_d20/FTacticalD20CombatLog.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20EventLog.h"
#include "gameplay/tactical_d20/FTacticalD20StateLog.h"

#include <fmt/format.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace game {
namespace {

constexpr int FallbackCombatLogMaxLines = 12;
constexpr int FallbackEventLogMaxLines = 32;
constexpr int FallbackStateLogMaxLines = 32;
constexpr int MinLogMaxLines = 1;

int SaneMaxLines(int configured, int fallback) {
    return std::max(configured > 0 ? configured : fallback, MinLogMaxLines);
}

int MaxStateLogLines(entt::registry& registry) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    return SaneMaxLines(config ? config->logging.stateLogMaxLines : FallbackStateLogMaxLines, FallbackStateLogMaxLines);
}

int MaxEventLogLines(entt::registry& registry) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    return SaneMaxLines(config ? config->logging.eventLogMaxLines : FallbackEventLogMaxLines, FallbackEventLogMaxLines);
}

int MaxCombatLogLines(entt::registry& registry) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    return SaneMaxLines(config ? config->logging.combatLogMaxLines : FallbackCombatLogMaxLines, FallbackCombatLogMaxLines);
}

void TrimLines(std::vector<std::string>& lines, int maxLines) {
    maxLines = std::max(maxLines, MinLogMaxLines);
    while (static_cast<int>(lines.size()) > maxLines) lines.erase(lines.begin());
}

bool ConsoleLogEnabled(entt::registry& registry) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    return !config || config->logging.consoleLogEnabled;
}

void MirrorConsole(entt::registry& registry, const char* category, const std::string& line) {
    if (!ConsoleLogEnabled(registry)) return;
    auto* logger = registry.ctx().find<FLogger>();
    if (logger && logger->logger) logger->logger->info("[TacticalD20:{}] {}", category, line);
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

std::string TacticalD20EntityLogKey(entt::registry& registry, entt::entity entity) {
    if (entity == entt::null) return "none";
    if (registry.valid(entity) && registry.all_of<FTacticalUnit>(entity)) return registry.get<FTacticalUnit>(entity).id;
    return fmt::format("entity_{}", static_cast<uint32_t>(entity));
}

void AppendTacticalD20StateLog(entt::registry& registry, const std::string& line) {
    if (!registry.ctx().contains<FTacticalD20StateLog>()) registry.ctx().emplace<FTacticalD20StateLog>();
    auto& log = registry.ctx().get<FTacticalD20StateLog>();
    log.lines.push_back(line);
    TrimLines(log.lines, MaxStateLogLines(registry));
    MirrorConsole(registry, "State", line);
}

void AppendTacticalD20EventLog(entt::registry& registry, const std::string& line) {
    if (!registry.ctx().contains<FTacticalD20EventLog>()) registry.ctx().emplace<FTacticalD20EventLog>();
    auto& log = registry.ctx().get<FTacticalD20EventLog>();
    log.lines.push_back(line);
    TrimLines(log.lines, MaxEventLogLines(registry));
    MirrorConsole(registry, "Event", line);
}

void AppendTacticalD20CombatLog(entt::registry& registry, const std::string& line) {
    if (!registry.ctx().contains<FTacticalD20CombatLog>()) registry.ctx().emplace<FTacticalD20CombatLog>();
    auto& log = registry.ctx().get<FTacticalD20CombatLog>();
    log.lines.push_back(line);
    TrimLines(log.lines, MaxCombatLogLines(registry));
    MirrorConsole(registry, "Combat", line);
}

void AppendTacticalD20CommandValidationLogs(entt::registry& registry, const FTacticalD20CommandDropValidatedEvent& event) {
    const auto result = event.valid ? std::string{"valid"} : fmt::format("invalid: {}", event.invalidReason);
    AppendTacticalD20EventLog(registry, fmt::format("[Event] CommandDropValidated command={} target={} result={}", event.commandId, TargetLabel(registry, event), result));
    AppendTacticalD20CombatLog(registry, fmt::format("{} command {}", event.commandId, result));
}

} // namespace game

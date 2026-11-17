#include "gameplay/dungeon/FRunLogSection.h"

#include <exception>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Logger.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/dungeon/FSaveSections.h"

namespace game {
namespace {

constexpr int kRunLogSectionVersion = 1;

nlohmann::json PackRunLog(entt::registry& registry) {
    const auto* log = registry.ctx().find<FRunLog>();
    if (log == nullptr || log->logId.empty()) return nlohmann::json::object();
    return {{"log_id", log->logId}, {"run_started_at", log->runStartedAt}};
}

/// Never throws: a corrupt section only loses the file identity (a fresh log starts on the first write);
/// the run itself must never fail to load because of its log.
void UnpackRunLog(entt::registry& registry, const nlohmann::json& section) {
    try {
        FRunLog restored;
        if (section.is_object()) {
            if (const auto id = section.find("log_id"); id != section.end() && id->is_string()) {
                restored.logId = id->get<std::string>();
            }
            if (const auto at = section.find("run_started_at"); at != section.end() && at->is_string()) {
                restored.runStartedAt = at->get<std::string>();
            }
        }
        if (restored.logId.empty()) {
            registry.ctx().erase<FRunLog>();
            return;
        }
        AttachRunLogToFile(registry, std::move(restored));
    } catch (const std::exception& error) {
        LOG_WARN("The saved run log could not be re-attached ({}); it starts a fresh file on the next write.", error.what());
        registry.ctx().erase<FRunLog>();
    } catch (...) {
        LOG_WARN("The saved run log could not be re-attached; it starts a fresh file on the next write.");
        registry.ctx().erase<FRunLog>();
    }
}

} // namespace

void RegisterRunLogSection(entt::registry& registry) {
    FSaveSectionSerializer serializer;
    serializer.id = kRunLogSectionId;
    serializer.version = kRunLogSectionVersion;
    serializer.pack = PackRunLog;
    serializer.unpack = UnpackRunLog;
    RegisterSaveSection(registry, std::move(serializer));
}

} // namespace game

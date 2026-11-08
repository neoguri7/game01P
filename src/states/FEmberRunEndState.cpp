#include "states/FEmberRunEndState.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <map>
#include <string>
#include <utility>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/EngineState.h"
#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FUiTheme.h"
#include "states/FEmberTitleState.h"
#include "states/FRunFlowStateMap.h"

namespace {

/// The room list scrolls once it is taller than this; the report, the fatal intent and the buttons never do.
constexpr float kRunEndRoomListMaxHeight = 220.f;

std::string Share(int part, int whole) {
    if (whole <= 0) return "0%";
    return std::to_string(static_cast<int>(std::lround(100.0 * static_cast<double>(part) / whole))) + "%";
}

} // namespace

FRunEndSummary BuildRunEndSummary(const game::FRunLog& log) {
    FRunEndSummary summary;
    const game::FRunReport report = game::FRunReportSummarize(log);
    summary.metrics.push_back("Run report");
    summary.metrics.push_back(std::format("Rooms: {}", report.rooms));
    summary.metrics.push_back(std::format("Average rounds per room: {:.1f}", report.avgRounds));
    summary.metrics.push_back(std::format(
        "Damage-free rooms: {} of {} ({})", report.damageFreeRooms, report.rooms,
        Share(report.damageFreeRooms, report.rooms)));
    summary.metrics.push_back(std::format(
        "Rooms with a KO: {} of {} ({})", report.koRooms, report.rooms, Share(report.koRooms, report.rooms)));
    summary.metrics.push_back(std::format("Intent whiff rate: {:.0f}%", report.whiffRate * 100.0));
    summary.metrics.push_back(std::format("Decisions per dungeon: {:.1f}", report.decisionsPerDungeon));

    // The room lines derive from the log itself: the report above only owns the aggregates.
    std::map<std::pair<int, int>, const nlohmann::json*> offers;
    std::map<int, int> campSpentByDepth;
    for (const auto& event : log.events) {
        if (!event.is_object()) continue;
        const auto type = event.find("t");
        if (type == event.end() || !type->is_string()) continue;
        const std::string& name = type->get_ref<const std::string&>();
        if (name == "offer") {
            const auto key = std::make_pair(event.value("depth", 0), event.value("room", 0));
            offers.try_emplace(key, &event);
        } else if (name == "camp") {
            campSpentByDepth[event.value("depth", 0)] += event.value("amount", 0);
        }
    }

    struct FRoomLine {
        int depth{0};
        std::size_t line{0};
    };
    std::vector<FRoomLine> roomLines;
    for (const auto& event : log.events) {
        if (!event.is_object()) continue;
        const auto type = event.find("t");
        if (type == event.end() || !type->is_string()) continue;
        if (type->get_ref<const std::string&>() != "room_end") continue;
        const int depth = event.value("depth", 0);
        const int room = event.value("room", 0);
        const int rounds = event.value("rounds", 0);
        const std::string result = event.value("result", std::string("win"));
        int hpLost = 0;
        if (const auto hp = event.find("hp_loss"); hp != event.end() && hp->is_array()) {
            for (const auto& entry : *hp) hpLost += entry.value("loss", 0);
        }
        std::string text = std::format("Depth {} room {} - {}, {} round{}, {} HP lost", depth, room, result,
                                       rounds, rounds == 1 ? "" : "s", hpLost);
        if (const auto offer = offers.find({depth, room}); offer != offers.end()) {
            if (offer->second->value("skipped", false)) {
                text += ", offer skipped";
            } else {
                text += ", picked " + offer->second->value("picked", std::string{});
            }
        }
        summary.rooms.push_back(std::move(text));
        roomLines.push_back(FRoomLine{depth, summary.rooms.size() - 1});
    }
    if (roomLines.empty()) summary.rooms.push_back("No rooms recorded.");
    // Camp spend belongs to the depth, not the room: show it on that depth's last room line.
    for (const auto& [depth, spent] : campSpentByDepth) {
        if (spent == 0) continue;
        for (auto it = roomLines.rbegin(); it != roomLines.rend(); ++it) {
            if (it->depth != depth) continue;
            summary.rooms[it->line] += std::format("; camp spent {} material{}", spent, spent == 1 ? "" : "s");
            break;
        }
    }

    if (log.fatalIntent.present) {
        summary.fatal.push_back(std::format(
            "Fatal intent: {} - declared {} at commit, {} at execution, {} damage ({}).",
            log.fatalIntent.skillId, log.fatalIntent.expectedAtCommit, log.fatalIntent.expectedAtExecution,
            log.fatalIntent.actualDamage, log.fatalIntent.hit ? "hit" : "missed"));
    } else {
        summary.fatal.push_back("No enemy intent was recorded.");
    }
    return summary;
}

std::vector<std::string> BuildRunEndSummaryLines(const game::FRunLog& log) {
    const FRunEndSummary summary = BuildRunEndSummary(log);
    std::vector<std::string> lines = summary.metrics;
    lines.push_back("");
    lines.push_back("Rooms");
    lines.insert(lines.end(), summary.rooms.begin(), summary.rooms.end());
    lines.push_back("");
    lines.insert(lines.end(), summary.fatal.begin(), summary.fatal.end());
    return lines;
}

FStateTransition FEmberRunEndState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (!Registry.ctx().contains<game::FDungeonRun>()) {
        if (auto* stale = Registry.ctx().find<game::FRunIntentRequest>()) Registry.ctx().erase<game::FRunIntentRequest>();
        LOG_ERROR("The run-end screen has no run in the context; returning to the title.");
        return {EStateChangeKind::Root, std::string(kEmberTitleId)};
    }
    auto* request = Registry.ctx().find<game::FRunIntentRequest>();
    if (request == nullptr) return {};
    const game::ERunIntent intent = request->intent;
    Registry.ctx().erase<game::FRunIntentRequest>();
    if (intent != game::ERunIntent::RetryAfterDefeat && intent != game::ERunIntent::LeaveRun) {
        LOG_ERROR("The run-end screen dropped an intent it does not own.");
        return {};
    }
    const auto result = game::ApplyRunIntent(Registry, intent);
    if (!result) {
        LOG_ERROR("The run-end screen could not apply its intent: {}", result.error());
        return {};
    }
    if (intent == game::ERunIntent::RetryAfterDefeat) {
        // A retry starts again from the checkpoint, not from depth 1.
        Registry.ctx().insert_or_assign<game::FRunStartRequest>(game::FRunStartRequest{false});
    }
    return game::ToStateTransition(*result, kEmberRunEndId);
}

void FEmberRunEndState::Render(entt::registry& Registry)
{
    const auto* run = Registry.ctx().find<game::FDungeonRun>();
    const auto& window = Registry.ctx().get<game::FEngineWindowState>();
    ImGui::SetNextWindowPos({window.width * .5f, window.height * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({680, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(
        {680, 0}, {680, static_cast<float>(window.height > 100 ? window.height - 80 : 200)});
    ImGui::OpenPopup("Expedition choice");
    if (ImGui::BeginPopupModal("Expedition choice", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        game::ui::DecorateCurrentWindow();
        game::DrawDungeonIcon(Registry, game::EDungeonSprite::Guardian);
        ImGui::SameLine();
        game::ui::HeadingText("EXPEDITION LOST", 26.f);
        if (run != nullptr) ImGui::TextWrapped("%s", run->message.c_str());
        ImGui::Separator();

        const game::FRunLog emptyLog;
        const auto* log = Registry.ctx().find<game::FRunLog>();
        const FRunEndSummary summary = BuildRunEndSummary(log != nullptr ? *log : emptyLog);

        // The metrics, the fatal intent and the buttons stay visible; a long run's room list scrolls.
        for (const auto& line : summary.metrics) {
            if (line.empty()) {
                ImGui::Spacing();
            } else {
                ImGui::TextUnformatted(line.c_str());
            }
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("Rooms");
        const float roomListHeight = std::min(
            static_cast<float>(summary.rooms.size()) * ImGui::GetTextLineHeightWithSpacing(),
            kRunEndRoomListMaxHeight);
        if (ImGui::BeginChild("run-end-rooms", {0, roomListHeight}, ImGuiChildFlags_None)) {
            for (const auto& line : summary.rooms) ImGui::TextUnformatted(line.c_str());
        }
        ImGui::EndChild();
        ImGui::Spacing();
        for (const auto& line : summary.fatal) ImGui::TextUnformatted(line.c_str());
        ImGui::Separator();
        if (game::ui::Button("New expedition from checkpoint", {-1, 44})) {
            game::PostRunIntent(Registry, game::ERunIntent::RetryAfterDefeat);
            ImGui::CloseCurrentPopup();
        }
        if (game::ui::Button("Return to camp", {-1, 32})) {
            game::PostRunIntent(Registry, game::ERunIntent::LeaveRun);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

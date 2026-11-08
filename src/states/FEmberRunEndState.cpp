#include "states/FEmberRunEndState.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/EngineState.h"
#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/meta/FMetaProgress.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FUiTheme.h"
#include "states/FEmberTitleState.h"
#include "states/FRunFlowStateMap.h"

namespace {

/// [WP-19b] em-based screen sizing (the WP-19a fixed 680 px width and 220 px room-list cap): the modal is
/// ~44 em wide (R6: not cramped at text_scale 1.25) and the single scrolling body caps at 30 em.
constexpr float kRunEndWidthEm = 44.f;
constexpr float kRunEndBodyMaxHeightEm = 30.f;
/// The two buttons below the body, in em (the old fixed 44 px / 32 px).
constexpr float kRunEndPrimaryButtonHeightEm = 2.8f;
constexpr float kRunEndSecondaryButtonHeightEm = 2.2f;
/// The footer block (separator, both buttons, paddings) reserved below the scrolling body.
constexpr float kRunEndFooterHeightEm = kRunEndPrimaryButtonHeightEm + kRunEndSecondaryButtonHeightEm + 1.5f;
/// Symmetric viewport edge around the popup (top and bottom alike: it is centered).
constexpr float kRunEndScreenMarginEm = 3.f;
/// Floor for absurdly small viewports so the modal never inverts.
constexpr float kRunEndMinPopupHeightPx = 200.f;

std::string Share(int part, int whole) {
    if (whole <= 0) return "0%";
    return std::to_string(static_cast<int>(std::lround(100.0 * static_cast<double>(part) / whole))) + "%";
}

/// "{count} {word}" with the word pluralized — the existing room line's `s` switch, wrapped once.
std::string CountedText(int count, std::string_view one, std::string_view many) {
    return std::to_string(count) + " " + std::string(count == 1 ? one : many);
}

/// Comma-joined ids in insertion order ("" for none).
std::string JoinIds(const std::vector<std::string>& ids) {
    std::string joined;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (index > 0) joined += ", ";
        joined += ids[index];
    }
    return joined;
}

/// The run's rune list as "none" / "1 rune (rune.fire)" / "2 runes (a, b)" (WP-19b ①②).
std::string RunesText(const std::vector<std::string>& runes) {
    if (runes.empty()) return "none";
    return CountedText(static_cast<int>(runes.size()), "rune", "runes") + " (" + JoinIds(runes) + ")";
}

/// The integer `key` of `event`, or nullopt when absent or not an integer (a mistyped key reads as absent).
std::optional<std::int64_t> IntegerKey(const nlohmann::json& event, const char* key) {
    const auto found = event.find(key);
    if (found == event.end()) return std::nullopt;
    if (!found->is_number_integer() && !found->is_number_unsigned()) return std::nullopt;
    return found->get<std::int64_t>();
}

/// One debrief section heading: a blank row, then the gold heading with its ornamental rules.
void RunEndSection(const char* title) {
    ImGui::Spacing();
    game::ui::SectionTitle(title);
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

    // [WP-19b] The decisive scenes as text (built only from the log — no registry, no listeners); a rule-3
    // scene carries the fatal intent's own facts, so the screen can drop its separate fatal line while it
    // shows them.
    for (const game::FRunScene& scene : game::BuildRunScenes(log)) {
        if (std::ranges::find(scene.rules, game::ERunSceneRule::FatalIntent) != scene.rules.end()) {
            summary.fatalInScene = true;
        }
        const std::vector<std::string> sceneLines = RunSceneLines(scene);
        summary.scenes.insert(summary.scenes.end(), sceneLines.begin(), sceneLines.end());
    }

    // [WP-19b] ④/⑤: the LAST run_end's cycle and the dev friction metric; the deepest room_end depth
    // names the cycle's depth. A missing or mistyped key drops only its own line.
    bool hasCycle = false;
    std::int64_t cycle = 0;
    std::optional<std::int64_t> msToFirstBattle;
    int deepestDepth = 0;
    for (const auto& event : log.events) {
        if (!event.is_object()) continue;
        const auto type = event.find("t");
        if (type == event.end() || !type->is_string()) continue;
        const std::string& name = type->get_ref<const std::string&>();
        if (name == "room_end") {
            if (const auto depth = IntegerKey(event, "depth"); depth.has_value() && *depth > deepestDepth) {
                deepestDepth = static_cast<int>(*depth);
            }
        } else if (name == "run_end") {
            const auto lastCycle = IntegerKey(event, "cycle");
            hasCycle = lastCycle.has_value();
            cycle = lastCycle.value_or(0);
            msToFirstBattle = IntegerKey(event, "ms_to_first_battle");
        }
    }
    if (hasCycle && cycle > 0) {
        summary.cycle.push_back(std::format("Cycle {} - deepest depth {}", cycle, deepestDepth));
    }
    if (msToFirstBattle.has_value()) {
        summary.friction.push_back(std::format("Time to first battle: {:.1f} s (dev metric)",
                                               static_cast<double>(*msToFirstBattle) / 1000.0));
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
    // [WP-19b] scenes / cycle / friction follow the pinned WP-19a text, each block one blank line apart.
    if (!summary.scenes.empty()) {
        lines.push_back("");
        lines.insert(lines.end(), summary.scenes.begin(), summary.scenes.end());
    }
    if (!summary.cycle.empty()) {
        lines.push_back("");
        lines.insert(lines.end(), summary.cycle.begin(), summary.cycle.end());
    }
    if (!summary.friction.empty()) {
        lines.push_back("");
        lines.insert(lines.end(), summary.friction.begin(), summary.friction.end());
    }
    return lines;
}

std::vector<std::string> RunSceneLines(const game::FRunScene& scene) {
    std::vector<std::string> lines;
    lines.reserve(scene.facts.size() + 1);
    lines.push_back(std::format("{} - Depth {} room {}", scene.title, scene.depth, scene.room));
    for (const game::FRunSceneFact& fact : scene.facts) {
        lines.push_back(std::format("  {}: {}", fact.label, fact.value));
    }
    return lines;
}

FNextRunDeltas NextRunDeltas(entt::registry& registry) {
    FNextRunDeltas deltas;
    const game::FMetaSave* save = game::FindMetaSave(registry);
    deltas.metaDisabled = game::MetaDisabled(registry) || save == nullptr;
    deltas.seals = game::MetaSeals(registry);
    for (const game::FUnlockOffer& offer : game::BuildUnlockOffers(registry)) {
        if (offer.status != game::EUnlockStatus::Purchasable) continue;
        deltas.unlockCandidates.push_back(
            std::format("{} ({})", offer.row.id, CountedText(offer.row.cost, "seal", "seals")));
    }
    deltas.ladderCheckpoint = game::MetaLadderCheckpoint(registry, 1);
    if (const auto* checkpoint = registry.ctx().find<game::FDungeonCheckpoint>(); checkpoint != nullptr) {
        deltas.checkpointCleared = checkpoint->cleared;
    }
    deltas.grandCount = game::MetaGrandCount(registry);
    if (save != nullptr) {
        // Locked roster rows (the preparation screen's "Not yet recruitable" entries) are not kept heroes.
        for (const game::FMetaRosterRow& row : save->roster) {
            if (!row.locked) ++deltas.rosterSize;
        }
        deltas.ownedUnlocks = static_cast<int>(save->unlocks.size());
    }
    return deltas;
}

std::vector<std::string> NextRunDeltaLines(const FNextRunDeltas& deltas) {
    // D-21: the start range comes from the checkpoint, not from meta — it shows in both modes.
    const std::string startRange = std::format("Start at any dungeon from 1 to {}", deltas.checkpointCleared + 1);
    if (deltas.metaDisabled) {
        return {startRange, "Unlocks and seals are shown when the unlock system is active."};
    }
    std::vector<std::string> lines;
    lines.push_back(std::format("Seals: {}", deltas.seals));
    lines.push_back(deltas.unlockCandidates.empty()
                        ? std::string("Unlock candidates: none affordable yet")
                        : "Unlock candidates: " + JoinIds(deltas.unlockCandidates));
    lines.push_back("Ladder 1 checkpoint: " + CountedText(deltas.ladderCheckpoint, "dungeon", "dungeons") +
                    " cleared");
    lines.push_back(startRange);
    lines.push_back("Achievements: " +
                    CountedText(deltas.grandCount, "grand breakthrough", "grand breakthroughs"));
    return lines;
}

FRunEndLedger BuildRunEndLedger(const game::FDungeonRun* run, const game::FRunLog& log,
                                const FNextRunDeltas& deltas) {
    FRunEndLedger ledger;
    // Log facts: the cleared dungeons and the camp spend (a held fact — the log carries no earned total).
    int clearedDungeons = 0;
    int campSpent = 0;
    for (const auto& event : log.events) {
        if (!event.is_object()) continue;
        const auto type = event.find("t");
        if (type == event.end() || !type->is_string()) continue;
        const std::string& name = type->get_ref<const std::string&>();
        if (name == "dungeon_clear") {
            ++clearedDungeons;
        } else if (name == "camp") {
            campSpent += event.value("amount", 0);
        }
    }
    ledger.gained.push_back(std::format("Dungeons cleared: {}", clearedDungeons));
    if (run != nullptr) {
        int levels = 0;
        int nodes = 0;
        for (const game::FRunMember& member : run->party) {
            levels += std::max(0, member.level - 1);
            nodes += static_cast<int>(member.allocatedNodes.size());
        }
        const std::string runes = RunesText(run->runes);
        // ① What the run gained — the logged/held facts only: the camp spend and the unspent balance. The
        // balance includes the D-7 start-support materials, so it is never called "earned".
        ledger.gained.push_back("Runes gained: " + runes);
        ledger.gained.push_back(std::format("Materials: {} spent at camp, {} unspent", campSpent, run->materials));
        ledger.gained.push_back(std::format("Levels gained: {}", levels));
        ledger.gained.push_back(std::format("Passive nodes gained: {}", nodes));
        // ② What the expedition loses: the run-scoped progress above — the rune count only; the id list
        // stays in ① (one full list is enough, the scenes must not sink below the fold).
        ledger.lost.push_back(std::format("Runes lost: {}", run->runes.size()));
        ledger.lost.push_back(std::format("Unspent materials lost: {}", run->materials));
        ledger.lost.push_back(std::format("Levels lost: {}", levels));
        ledger.lost.push_back(std::format("Passive nodes lost: {}", nodes));
    }
    // ② What stays: the checkpoint, the roster size and the owned unlocks — the numbers all come from the
    // deltas (this builder never reads the registry).
    ledger.kept.push_back("Checkpoint kept: " + CountedText(deltas.checkpointCleared, "dungeon", "dungeons") +
                          " cleared");
    if (!deltas.metaDisabled) {
        ledger.kept.push_back("Roster kept: " + CountedText(deltas.rosterSize, "hero", "heroes"));
        ledger.kept.push_back(std::format("Unlocks kept: {} owned", deltas.ownedUnlocks));
    }
    return ledger;
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
    const float em = ImGui::GetFontSize();
    const float width = kRunEndWidthEm * em;
    const float viewportHeight = static_cast<float>(window.height);
    const float screenMargin = kRunEndScreenMarginEm * em;
    ImGui::SetNextWindowPos({window.width * .5f, window.height * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({width, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(
        {width, 0}, {width, std::max(kRunEndMinPopupHeightPx, viewportHeight - screenMargin * 2.f)});
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
        const game::FRunLog& source = log != nullptr ? *log : emptyLog;
        const FRunEndSummary summary = BuildRunEndSummary(source);
        const FNextRunDeltas deltas = NextRunDeltas(Registry);
        const FRunEndLedger ledger = BuildRunEndLedger(run, source, deltas);
        const std::vector<std::string> deltaLines = NextRunDeltaLines(deltas);

        // The single scrolling body holds the whole debrief; the two buttons stay below it, outside the
        // scroll. The body is capped in em and by what the viewport leaves after the measured header and
        // the reserved footer, so the buttons never leave the screen.
        const float usedHeight = ImGui::GetCursorPosY();
        const float available = viewportHeight - screenMargin * 2.f - usedHeight - kRunEndFooterHeightEm * em;
        const float bodyHeight = std::min(kRunEndBodyMaxHeightEm * em, std::max(em, available));
        if (ImGui::BeginChild("run-end-body", {0.f, bodyHeight}, ImGuiChildFlags_None)) {
            // The screen's spine (R6/WP-19b): what you gained → what you lost / what stays → the decisive
            // moments and the fatal line → what changes next run (with the cycle). The WP-19a run report
            // and the rooms follow as reference, then the dimmed friction metric.
            RunEndSection("What you gained");
            for (const auto& line : ledger.gained) ImGui::TextWrapped("%s", line.c_str());

            RunEndSection("What you lost / what stays");
            for (const auto& line : ledger.lost) ImGui::TextWrapped("%s", line.c_str());
            for (const auto& line : ledger.kept) ImGui::TextWrapped("%s", line.c_str());

            if constexpr (kRunEndShowScenes) {
                RunEndSection("Decisive moments");
                if (summary.scenes.empty()) {
                    ImGui::TextDisabled("No decisive moment was recorded.");
                } else {
                    // A scene block: the "{title} - Depth d room r" heading (no two-space prefix, gold like
                    // a section title) then its "  {label}: {value}" facts wrapped (long facts no longer
                    // clip); one blank row between scenes.
                    bool firstScene = true;
                    for (const auto& line : summary.scenes) {
                        if (!line.starts_with("  ")) {
                            if (!firstScene) ImGui::Spacing();
                            firstScene = false;
                            ImGui::TextColored(game::ui::palette::kGoldV, "%s", line.c_str());
                        } else {
                            ImGui::TextWrapped("%s", line.c_str());
                        }
                    }
                }
            }

            // The fatal line yields to the rule-3 scene's own facts while the scenes section is shown; with
            // the rollback lever off it always comes back, so the fact never repeats on screen.
            if (!(kRunEndShowScenes && summary.fatalInScene)) {
                for (const auto& line : summary.fatal) ImGui::TextWrapped("%s", line.c_str());
            }

            RunEndSection("What changes next run");
            for (const auto& line : deltaLines) ImGui::TextWrapped("%s", line.c_str());

            for (const auto& line : summary.cycle) ImGui::TextUnformatted(line.c_str());

            // The run report, demoted to reference (impact first): the summary's own first metrics line
            // ("Run report") is replaced by this section title, so the text itself stays byte-identical.
            RunEndSection("Run report");
            for (std::size_t index = 1; index < summary.metrics.size(); ++index) {
                const std::string& line = summary.metrics[index];
                if (line.empty()) {
                    ImGui::Spacing();
                } else {
                    ImGui::TextUnformatted(line.c_str());
                }
            }

            for (const auto& line : summary.friction) ImGui::TextDisabled("%s", line.c_str());

            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Rooms", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const auto& line : summary.rooms) ImGui::TextUnformatted(line.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (game::ui::Button("New expedition from checkpoint", {-1.f, kRunEndPrimaryButtonHeightEm * em})) {
            game::PostRunIntent(Registry, game::ERunIntent::RetryAfterDefeat);
            ImGui::CloseCurrentPopup();
        }
        if (game::ui::Button("Return to title", {-1.f, kRunEndSecondaryButtonHeightEm * em})) {
            game::PostRunIntent(Registry, game::ERunIntent::LeaveRun);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

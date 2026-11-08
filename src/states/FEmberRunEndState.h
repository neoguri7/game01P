#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "FBaseState.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunLog.h"

/// Catalog id owned by this state (WP-02b). The runEnd modal is a real state: it draws the defeat view
/// and offers retry (back to prepare, from the checkpoint) or leave (to the title).
inline constexpr std::string_view kEmberRunEndId = game::kRunModalRunEnd;

/// The minimal run-end text (WP-19a E8), split so the screen can keep the metrics, the fatal intent and the
/// buttons visible while only the room list scrolls. `metrics` is the report block, `rooms` is one line per
/// room (the "Rooms" heading is the screen's), `fatal` is the one intent statement. WP-19b extends this.
struct FRunEndSummary {
    std::vector<std::string> metrics;
    std::vector<std::string> rooms;
    std::vector<std::string> fatal;
};

/// The minimal run-end text (WP-19a E8): the five report metrics, one line per room (with its offer pick
/// and that depth's camp spend), and the one fatal enemy intent as facts only. This is exactly
/// `BuildRunEndSummary` joined with blank lines, so text-only consumers and tests keep one canonical text.
/// Pure over the log so tests can check the text without ImGui.
[[nodiscard]] std::vector<std::string> BuildRunEndSummaryLines(const game::FRunLog& log);
[[nodiscard]] FRunEndSummary BuildRunEndSummary(const game::FRunLog& log);

/// Run-end screen. The view posts RetryAfterDefeat or LeaveRun; WP-19a replaced the stub presentation, not
/// the contract. It never pops: a run end is a screen, not a modal.
class FEmberRunEndState final : public FBaseState
{
public:
    FStateTransition Update(entt::registry& Registry, float DeltaTime) override;
    void Render(entt::registry& Registry) override;
    [[nodiscard]] std::string_view GetName() const noexcept override { return "RunEnd"; }
    [[nodiscard]] std::span<const std::string_view> RequiredSystems() const noexcept override { return {}; }
};

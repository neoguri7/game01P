// WP-13 status-rule tests: D-12 on_apply paralyze (decision at application, skipPending consumption),
// freeze staying turn_start, the speed_pct fold in RefreshCombatSheet ④, boot validation of mode/chance_cap
// and the AI's mode-aware control value.
//
// Unit-level rules run on hand-built content; decode/boot tests run through the real FAssetManager +
// FContentRegistry path. No fixture JSON is added: modified documents are written into temp roots whose
// first root shadows the frozen fixture (the manager falls back per file).

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FBaseCombatStats.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "gameplay/ai/FAiBlackboard.h"
#include "gameplay/ai/FAiQueries.h"
#include "gameplay/ai/FAiTuning.h"
#include "gameplay/ai/FAiWorld.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FStatusContent.h"

namespace {

using game::FActiveStatuses;
using game::FBattleRng;
using game::FStatusEffect;
using game::FStatusTickResult;
using game::gameplay::FContentRegistry;
using game::gameplay::FStatusContent;

// ---------------------------------------------------------------------------------------------
// Content / document helpers
// ---------------------------------------------------------------------------------------------

/// A unique throwaway asset root with a data/ subdirectory, removed when it goes out of scope.
struct FTempRoot {
    std::filesystem::path root;

    explicit FTempRoot(std::string_view label) {
        static int counter = 0;
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("game01p-wp13-" + std::string{label} + "-" + std::to_string(ticks) + "-" + std::to_string(counter++));
        std::error_code error;
        std::filesystem::remove_all(root, error);
        CHECK(std::filesystem::create_directories(root / "data"));
    }

    ~FTempRoot() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    FTempRoot(const FTempRoot&) = delete;
    FTempRoot& operator=(const FTempRoot&) = delete;
};

/// The frozen fixture's statuses document, so a modified copy keeps everything the cross-table checks need
/// (runes reference freeze/paralyze, skills reference burn/buff/debuff — dropping a row fails the boot).
[[nodiscard]] nlohmann::json FixtureStatuses() {
    std::ifstream file(FixtureDataPath("statuses.json"));
    CHECK(file.good());
    nlohmann::json document;
    file >> document;
    return document;
}

[[nodiscard]] nlohmann::json& StatusRow(nlohmann::json& document, std::string_view id) {
    for (nlohmann::json& row : document["statuses"]) {
        if (row["id"].get<std::string>() == id) {
            return row;
        }
    }
    throw std::runtime_error("fixture status row is missing: " + std::string{id});
}

/// The temp root first, then the frozen fixture and the shipped assets (per-file fallback).
[[nodiscard]] std::vector<std::string> RootsWith(const FTempRoot& temp) {
    std::vector<std::string> roots{temp.root.string()};
    const std::vector<std::string> base = TestAssetRoots();
    roots.insert(roots.end(), base.begin(), base.end());
    return roots;
}

void WriteStatuses(const FTempRoot& temp, const nlohmann::json& document) {
    std::ofstream file(temp.root / "data" / "statuses.json", std::ios::binary | std::ios::trunc);
    file << document.dump(2);
    file.flush();
    CHECK(file.good());
}

/// Boots the content tables against `roots`. Nullopt = a rejected document (the reason is logged by the
/// registry, like the boot would).
[[nodiscard]] std::optional<FContentRegistry> LoadContent(const std::vector<std::string>& roots) {
    const game::FAssetManager assets(roots);
    return FContentRegistry::load(assets);
}

[[nodiscard]] FContentRegistry FixtureContent() {
    std::optional<FContentRegistry> content = LoadContent(TestAssetRoots());
    CHECK(content.has_value());
    return std::move(*content);
}

// ---------------------------------------------------------------------------------------------
// Rule-level helpers
// ---------------------------------------------------------------------------------------------

/// A hand-built control status; the defaults are the D-12 ones (`chance_cap` 90, speed neutral).
[[nodiscard]] FStatusContent ControlStatus(std::string id, int magnitude, int duration, std::string mode,
                                           int chanceCap = game::gameplay::kDefaultChanceCap,
                                           int speedPct = game::gameplay::kStatusSpeedPctNeutral) {
    FStatusContent status;
    status.id = std::move(id);
    status.displayName = status.id;
    status.kind = game::gameplay::kStatusKindControl;
    status.magnitude = magnitude;
    status.durationTurns = duration;
    status.speedPct = speedPct;
    status.mode = std::move(mode);
    status.chanceCap = chanceCap;
    return status;
}

/// A battle RNG whose next `rollPercent()` equals `roll` (every residue appears within the first few
/// thousand xorshift32 states), so a test pins which side of a chance a roll lands on.
[[nodiscard]] FBattleRng RngWithFirstRoll(int roll) {
    for (std::uint32_t state = 1; state < 1000000u; ++state) {
        FBattleRng probe{state};
        if (probe.rollPercent() == roll) {
            return FBattleRng{state};
        }
    }
    throw std::runtime_error("no battle-RNG seed found for roll " + std::to_string(roll));
}

/// The state after exactly one draw — lets a test assert "consumed exactly one roll" without re-implementing
/// the xorshift.
[[nodiscard]] std::uint32_t StateAfterOneRoll(std::uint32_t state) {
    FBattleRng probe{state};
    (void)probe.rollPercent();
    return probe.state;
}

// ---------------------------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------------------------

/// One unit with a base speed and the loaded fixture content — enough for RefreshCombatSheet (no
/// FDungeonRun in the ctx, so ②/③ skip and only ④ runs).
struct FSheetFixture {
    std::optional<FContentRegistry> content;
    entt::registry registry;
    entt::entity unit{entt::null};

    explicit FSheetFixture(int baseSpeed) {
        content = LoadContent(TestAssetRoots());
        CHECK(content.has_value());
        unit = registry.create();
        game::FBaseCombatStats base;
        base.maxHealth = 30;
        base.speed = baseSpeed;
        registry.emplace<game::FBaseCombatStats>(unit, base);
        registry.emplace<game::FStats>(unit, game::FStats{});
        registry.emplace<game::FHealth>(unit, game::FHealth{30, 30});
        registry.emplace<game::FSpeed>(unit, baseSpeed);
        registry.emplace<game::FActiveStatuses>(unit);
    }

    void refresh() { game::RefreshCombatSheet(registry, unit, *content); }
    [[nodiscard]] int speed() const { return registry.get<game::FSpeed>(unit).value; }
};

/// The AI side of the tests: loaded content plus an empty registry/world — ValueOfStatus reads only the
/// content tables, the tuning and the target view handed to it.
struct FAiFixture {
    FContentRegistry content;
    entt::registry registry;
    game::ai::FAiBlackboard blackboard;
    game::ai::FBtMemory memory;
    game::ai::FAiTuning tuning;
    game::ai::FAiWorld world;

    explicit FAiFixture(std::vector<std::string> roots = TestAssetRoots()) {
        std::optional<FContentRegistry> loaded = LoadContent(roots);
        CHECK(loaded.has_value());
        content = std::move(*loaded);
    }

    [[nodiscard]] game::ai::FBtContext Context() {
        return {registry, content, tuning, world, blackboard, memory, entt::null, {}, false};
    }
};

// ---------------------------------------------------------------------------------------------
// D-12: paralyze decides at application, freeze never does
// ---------------------------------------------------------------------------------------------

/// ParalyzeDecidedOnApply: the success/failure is fixed at application; the owner's turn start consumes
/// the decided loss and does not roll again.
void ParalyzeDecidedOnApply() {
    const FStatusContent paralyze = ControlStatus("status.paralyze", 50, 2, std::string(game::gameplay::kStatusModeOnApply));
    game::FActiveStatuses active;
    FBattleRng rng = RngWithFirstRoll(30); // 30 < 50 → 부여 성공
    CHECK(game::TryApplyStatus(active, paralyze, 0, 0, rng));
    CHECK(active.effects.size() == 1);
    CHECK(active.effects[0].skipPending);

    const std::vector<const FStatusContent*> definitions{&paralyze};
    const std::uint32_t before = rng.state;
    const FStatusTickResult tick = game::TickStatuses(active, definitions, rng);
    CHECK(tick.skipTurn);                 // 부여 시 확정된 1턴 상실이 소비된다
    CHECK(!active.effects[0].skipPending);
    CHECK(rng.state == before);           // 턴 시작에 다시 굴리지 않는다

    // ApplyStatus is deterministic (preview/fixture contract): even for on_apply it rolls nothing and
    // leaves no pending loss — the rolling application is TryApplyStatus alone.
    game::FActiveStatuses deterministic;
    FBattleRng untouched = RngWithFirstRoll(0);
    const std::uint32_t untouchedBefore = untouched.state;
    game::ApplyStatus(deterministic, paralyze);
    CHECK(deterministic.effects.size() == 1);
    CHECK(!deterministic.effects[0].skipPending);
    CHECK(untouched.state == untouchedBefore);
}

/// FailedApplyLeavesNoStatus: a resisted application changes nothing and spends no further rolls.
void FailedApplyLeavesNoStatus() {
    const FStatusContent paralyze = ControlStatus("status.paralyze", 50, 2, std::string(game::gameplay::kStatusModeOnApply));
    game::FActiveStatuses active;
    FBattleRng rng = RngWithFirstRoll(70); // 70 >= 50 → 저항
    CHECK(!game::TryApplyStatus(active, paralyze, 0, 0, rng));
    CHECK(active.effects.empty());

    const std::vector<const FStatusContent*> definitions{&paralyze};
    const std::uint32_t afterApply = rng.state;
    const FStatusTickResult tick = game::TickStatuses(active, definitions, rng);
    CHECK(!tick.skipTurn);
    CHECK(active.effects.empty());
    CHECK(rng.state == afterApply);
}

/// PendingSkipIsSingle: one successful application costs exactly one turn; after the consumption the
/// remaining duration does not skip anything else.
void PendingSkipIsSingle() {
    const FStatusContent paralyze = ControlStatus("status.paralyze", 50, 2, std::string(game::gameplay::kStatusModeOnApply));
    game::FActiveStatuses active;
    FBattleRng rng = RngWithFirstRoll(30);
    CHECK(game::TryApplyStatus(active, paralyze, 0, 0, rng));

    const std::vector<const FStatusContent*> definitions{&paralyze};
    CHECK(game::TickStatuses(active, definitions, rng).skipTurn);
    CHECK(!active.effects[0].skipPending);
    CHECK(!game::TickStatuses(active, definitions, rng).skipTurn); // 소비 후에는 상실 없음
    CHECK(active.effects.empty());                                 // 지속 2턴 만료
}

/// ReapplyAfterConsumeRollsAgain: re-applying after the spend is a fresh application — one new roll and a
/// new pending loss.
void ReapplyAfterConsumeRollsAgain() {
    const FStatusContent paralyze = ControlStatus("status.paralyze", 50, 2, std::string(game::gameplay::kStatusModeOnApply));
    game::FActiveStatuses active;
    FBattleRng first = RngWithFirstRoll(30);
    CHECK(game::TryApplyStatus(active, paralyze, 0, 0, first));
    const std::vector<const FStatusContent*> definitions{&paralyze};
    CHECK(game::TickStatuses(active, definitions, first).skipTurn); // 소비

    FBattleRng second = RngWithFirstRoll(30);
    const std::uint32_t before = second.state;
    CHECK(game::TryApplyStatus(active, paralyze, 0, 0, second)); // 새 굴림
    CHECK(second.state == StateAfterOneRoll(before));            // 정확히 1회 소비
    CHECK(active.effects[0].skipPending);
    CHECK(active.effects[0].turnsRemaining == 2);                // 갱신도 함께(지속은 긴 쪽)
}

/// FreezeUnchangedByD12: freeze keeps turn_start, magnitude 0 = always, 2 lost turns and speed_pct 50.
void FreezeUnchangedByD12() {
    const FStatusContent freeze =
        ControlStatus("status.freeze", 0, 2, std::string(game::gameplay::kStatusModeTurnStart), 90, 50);
    game::FActiveStatuses active;
    FBattleRng rng = RngWithFirstRoll(0);
    const std::uint32_t before = rng.state;
    CHECK(game::TryApplyStatus(active, freeze, 0, 0, rng));
    CHECK(rng.state == before); // 적용은 롤하지 않는다(turn_start)
    CHECK(!active.effects[0].skipPending);
    CHECK(game::StatusSpeedValue(12, active, {&freeze}) == 6); // speed_pct 50 그대로

    const std::vector<const FStatusContent*> definitions{&freeze};
    CHECK(game::TickStatuses(active, definitions, rng).skipTurn); // magnitude 0 = 항상
    CHECK(game::TickStatuses(active, definitions, rng).skipTurn); // 2턴 상실 그대로
    CHECK(active.effects.empty());
    CHECK(rng.state == before);                                   // 100%는 굴림을 소비하지 않는다
}

/// FreezeTickChanceIsAlways: the tick chance of a magnitude-0 control is 100 (certain), whatever the cap.
void FreezeTickChanceIsAlways() {
    CHECK(game::ControlTickChance(0, 90) == 100);
    CHECK(game::ControlTickChance(0, 100) == 100);
    CHECK(game::ControlTickChance(50, 90) == 50); // 확률형은 저장 강도 그대로
}

/// ParalyzeApplyChanceZeroMeansNever: 0% application is "never", not "always" — the freeze 0=100
/// convention belongs to the tick side only.
void ParalyzeApplyChanceZeroMeansNever() {
    const FStatusContent paralyzed = ControlStatus("status.paralyze", 0, 2, std::string(game::gameplay::kStatusModeOnApply));
    CHECK(game::ControlApplyChance(paralyzed, 0) == 0);

    game::FActiveStatuses active;
    FBattleRng rng = RngWithFirstRoll(0);
    const std::uint32_t before = rng.state;
    CHECK(!game::TryApplyStatus(active, paralyzed, 0, 0, rng));
    CHECK(active.effects.empty());
    CHECK(rng.state == before); // 0%는 굴림도 없다
}

/// ParalyzeCapClamps: the apply chance clamps to chance_cap after the rune bonus (감전 +20%p stays ≤ 90).
void ParalyzeCapClamps() {
    const FStatusContent paralyze = ControlStatus("status.paralyze", 50, 2, std::string(game::gameplay::kStatusModeOnApply));
    CHECK(game::ControlApplyChance(paralyze, 20) == 70);
    CHECK(game::ControlApplyChance(paralyze, 40) == 90); // 상한 90
    const FStatusContent strong = ControlStatus("status.paralyze", 85, 2, std::string(game::gameplay::kStatusModeOnApply), 90);
    CHECK(game::ControlApplyChance(strong, 20) == 90);
    CHECK(game::ControlApplyChance(paralyze, -80) == 0); // 하한 0
}

/// ChanceCapClamps: the tick chance clamps to the cap too, but "magnitude 0 = always" wins over the cap.
void ChanceCapClamps() {
    CHECK(game::ControlTickChance(70, 40) == 40);
    CHECK(game::ControlTickChance(150, 90) == 90);
    CHECK(game::ControlTickChance(50, 90) == 50);
    CHECK(game::ControlTickChance(0, 40) == 100); // 빙결 규칙이 상한보다 우선한다
}

/// IsTurnSkippedSeesDecidedLoss: the decided-loss view (WP-11's consumer) sees a pending on_apply loss
/// and freeze, but never guesses a probabilistic turn_start roll.
void IsTurnSkippedSeesDecidedLoss() {
    const FStatusContent paralyze = ControlStatus("status.paralyze", 50, 2, std::string(game::gameplay::kStatusModeOnApply));
    const FStatusContent legacy = ControlStatus("status.paralyze", 50, 2, std::string(game::gameplay::kStatusModeTurnStart));
    const FStatusContent freeze = ControlStatus("status.freeze", 0, 2, std::string(game::gameplay::kStatusModeTurnStart));

    game::FActiveStatuses active;
    FBattleRng rng = RngWithFirstRoll(30);
    CHECK(game::TryApplyStatus(active, paralyze, 0, 0, rng));
    const std::vector<const FStatusContent*> definitions{&paralyze};
    CHECK(game::IsTurnSkipped(active, definitions));   // 부여 시 확정 — 표시 가능
    CHECK(game::TickStatuses(active, definitions, rng).skipTurn);
    CHECK(!game::IsTurnSkipped(active, definitions));  // 소비 후 미확정 상실 없음

    game::FActiveStatuses legacyActive;
    game::ApplyStatus(legacyActive, legacy);
    CHECK(!game::IsTurnSkipped(legacyActive, {&legacy})); // 확률형은 아직 미확정(의도 무효화 금지)

    game::FActiveStatuses frozen;
    game::ApplyStatus(frozen, freeze);
    CHECK(game::IsTurnSkipped(frozen, {&freeze})); // 빙결은 항상
}

// ---------------------------------------------------------------------------------------------
// RNG order
// ---------------------------------------------------------------------------------------------

/// ControlApplyRollsOnce: an application decision consumes the battle RNG exactly once (success or
/// failure), and the later consumption adds no draw.
void ControlApplyRollsOnce() {
    const FStatusContent paralyze = ControlStatus("status.paralyze", 50, 2, std::string(game::gameplay::kStatusModeOnApply));
    game::FActiveStatuses applied;
    FBattleRng success = RngWithFirstRoll(30);
    const std::uint32_t beforeSuccess = success.state;
    CHECK(game::TryApplyStatus(applied, paralyze, 0, 0, success));
    CHECK(success.state == StateAfterOneRoll(beforeSuccess));

    game::FActiveStatuses resisted;
    FBattleRng failure = RngWithFirstRoll(70);
    const std::uint32_t beforeFailure = failure.state;
    CHECK(!game::TryApplyStatus(resisted, paralyze, 0, 0, failure));
    CHECK(failure.state == StateAfterOneRoll(beforeFailure)); // 실패도 1회

    const std::uint32_t afterApply = success.state;
    const std::vector<const FStatusContent*> definitions{&paralyze};
    CHECK(game::TickStatuses(applied, definitions, success).skipTurn);
    CHECK(success.state == afterApply); // 소비는 굴림 없음
}

/// RngOrderIsPinned: the draw count per case — 1..99% apply = 1, 0%/100% = 0, dot/stat/freeze = 0,
/// legacy turn_start tick = 1.
void RngOrderIsPinned() {
    constexpr std::string_view onApply = game::gameplay::kStatusModeOnApply;
    constexpr std::string_view turnStart = game::gameplay::kStatusModeTurnStart;

    {
        // 100%: no draw, applied.
        const FStatusContent certain = ControlStatus("status.paralyze", 100, 2, std::string(onApply), 100);
        game::FActiveStatuses active;
        FBattleRng rng = RngWithFirstRoll(0);
        const std::uint32_t before = rng.state;
        CHECK(game::TryApplyStatus(active, certain, 0, 0, rng));
        CHECK(active.effects[0].skipPending);
        CHECK(rng.state == before);
    }
    {
        // dot / stat / freeze: no draw at apply time.
        FStatusContent burn;
        burn.id = "status.burn";
        burn.displayName = "Burn";
        burn.kind = game::gameplay::kStatusKindDot;
        burn.magnitude = 3;
        burn.durationTurns = 3;
        const FStatusContent freeze = ControlStatus("status.freeze", 0, 2, std::string(turnStart), 90, 50);

        game::FActiveStatuses active;
        FBattleRng rng = RngWithFirstRoll(0);
        const std::uint32_t before = rng.state;
        CHECK(game::TryApplyStatus(active, burn, 0, 0, rng));
        CHECK(game::TryApplyStatus(active, freeze, 0, 0, rng));
        CHECK(rng.state == before);

        const std::vector<const FStatusContent*> definitions{&burn, &freeze};
        const FStatusTickResult tick = game::TickStatuses(active, definitions, rng);
        CHECK(tick.damage == 3);
        CHECK(tick.skipTurn);
        CHECK(rng.state == before); // dot 피해/빙결 소비 모두 굴림 없음
    }
    {
        // Legacy turn_start control: the tick rolls exactly once.
        const FStatusContent legacy = ControlStatus("status.paralyze", 50, 2, std::string(turnStart));
        game::FActiveStatuses active;
        game::ApplyStatus(active, legacy); // 결정적 — 굴림 없음
        FBattleRng rng = RngWithFirstRoll(30);
        const std::uint32_t before = rng.state;
        const std::vector<const FStatusContent*> definitions{&legacy};
        CHECK(game::TickStatuses(active, definitions, rng).skipTurn); // 30 < 50
        CHECK(rng.state == StateAfterOneRoll(before));
    }
}

// ---------------------------------------------------------------------------------------------
// Decode / boot validation
// ---------------------------------------------------------------------------------------------

/// FixtureParalyzeIsOnApply / FreezeFixtureStaysTurnStart: the frozen fixture carries the D-12 rows.
void FixtureParalyzeIsOnApply() {
    const FContentRegistry content = FixtureContent();
    const FStatusContent* paralyze = content.statuses.find("status.paralyze");
    CHECK(paralyze != nullptr);
    CHECK(paralyze->mode == game::gameplay::kStatusModeOnApply);
    CHECK(paralyze->chanceCap == game::gameplay::kDefaultChanceCap);
    CHECK(paralyze->magnitude == 50);
    CHECK(paralyze->durationTurns == 2);
}

void FreezeFixtureStaysTurnStart() {
    const FContentRegistry content = FixtureContent();
    const FStatusContent* freeze = content.statuses.find("status.freeze");
    CHECK(freeze != nullptr);
    CHECK(freeze->mode == game::gameplay::kStatusModeTurnStart);
    CHECK(freeze->chanceCap == game::gameplay::kDefaultChanceCap);
    CHECK(freeze->magnitude == 0);
    CHECK(freeze->durationTurns == 2);
    CHECK(freeze->speedPct == 50);
}

/// StatusKeysMatchDecisionRecord: the shipped data boots and matches D-12 (mode/chance_cap on the two
/// controls). The loader rejects undeclared keys, so loading also proves `paralyze_roll`/`lost_turns`/
/// `apply_chance`/`rune_bonus_pp` are absent.
void StatusKeysMatchDecisionRecord() {
    const std::optional<FContentRegistry> content = LoadContent(ShipAssetRoots());
    CHECK(content.has_value());
    const FStatusContent* paralyze = content->statuses.find("status.paralyze");
    const FStatusContent* freeze = content->statuses.find("status.freeze");
    CHECK(paralyze != nullptr);
    CHECK(paralyze->mode == game::gameplay::kStatusModeOnApply);
    CHECK(paralyze->chanceCap == game::gameplay::kDefaultChanceCap);
    CHECK(freeze != nullptr);
    CHECK(freeze->mode == game::gameplay::kStatusModeTurnStart);
    CHECK(freeze->chanceCap == game::gameplay::kDefaultChanceCap);
    CHECK(freeze->speedPct == 50);
}

/// FreezeDefaultsToTurnStart: an omitted `mode` decodes to turn_start; an omitted `chance_cap` decodes to
/// 90 (the loader materializes missing optional numbers as 0 — the decoder maps that to the declared
/// default).
void FreezeDefaultsToTurnStart() {
    const FTempRoot temp{"freeze-default"};
    nlohmann::json document = FixtureStatuses();
    nlohmann::json& freeze = StatusRow(document, "status.freeze");
    freeze.erase("mode");       // 생략 → turn_start 기본
    freeze.erase("chance_cap"); // 생략 → 90 기본
    WriteStatuses(temp, document);

    const std::optional<FContentRegistry> content = LoadContent(RootsWith(temp));
    CHECK(content.has_value());
    const FStatusContent* decoded = content->statuses.find("status.freeze");
    CHECK(decoded != nullptr);
    CHECK(decoded->mode == game::gameplay::kStatusModeTurnStart);
    CHECK(decoded->chanceCap == game::gameplay::kDefaultChanceCap);
    CHECK(decoded->speedPct == 50);
    CHECK(decoded->magnitude == 0);
    CHECK(decoded->durationTurns == 2);
}

/// OmittedSpeedPctIsNeutral: a status without an authored speed_pct folds as ×1 — the dense 0 the loader
/// projects for the missing field must not become a ×0 speed collapse.
void OmittedSpeedPctIsNeutral() {
    const FContentRegistry content = FixtureContent();
    const FStatusContent* burn = content.statuses.find("status.burn");
    CHECK(burn != nullptr);
    CHECK(burn->speedPct == game::gameplay::kStatusSpeedPctNeutral);

    game::FActiveStatuses active;
    game::ApplyStatus(active, *burn);
    CHECK(game::StatusSpeedValue(12, active, {burn}) == 12);
}

/// UnknownStatusModeRejected: an unknown `mode` fails the boot (no silent fallback mid-fight).
void UnknownStatusModeRejected() {
    const FTempRoot temp{"bad-mode"};
    nlohmann::json document = FixtureStatuses();
    StatusRow(document, "status.paralyze")["mode"] = "sometimes";
    WriteStatuses(temp, document);
    CHECK(!LoadContent(RootsWith(temp)).has_value());
}

/// ChanceCapOutsideRangeRejected: `chance_cap` outside 0..100 fails the boot.
void ChanceCapOutsideRangeRejected() {
    const FTempRoot temp{"bad-cap"};
    nlohmann::json document = FixtureStatuses();
    StatusRow(document, "status.freeze")["chance_cap"] = 150;
    WriteStatuses(temp, document);
    CHECK(!LoadContent(RootsWith(temp)).has_value());
}

/// LegacyParalyzeValueKeepsGain: switching paralyze back to `mode: turn_start` (the documented 1-key
/// rollback) restores the old value shape — threat × tick chance × gain — and the tick rolls again.
void LegacyParalyzeValueKeepsGain() {
    const FTempRoot temp{"legacy-rollback"};
    nlohmann::json document = FixtureStatuses();
    StatusRow(document, "status.paralyze")["mode"] = "turn_start";
    WriteStatuses(temp, document);

    FAiFixture fixture{RootsWith(temp)};
    const FStatusContent* legacy = fixture.content.statuses.find("status.paralyze");
    CHECK(legacy != nullptr);
    CHECK(legacy->mode == game::gameplay::kStatusModeTurnStart);
    CHECK(!game::IsOnApplyControl(*legacy));

    game::ai::FUnitView target;
    target.hp = target.hpMax = 30;
    target.threat = 10;
    auto ctx = fixture.Context();
    CHECK(game::ai::ValueOfStatus(ctx, {"status.paralyze", 0, 0}, target, false) == 10); // 10 × 50% × gain 2

    // 레거시 틱: 확률형은 자기 턴 시작에 굴린다.
    game::FActiveStatuses active;
    game::ApplyStatus(active, *legacy);
    FBattleRng rng = RngWithFirstRoll(30);
    CHECK(game::TickStatuses(active, game::CollectStatuses(active, fixture.content), rng).skipTurn);
}

// ---------------------------------------------------------------------------------------------
// speed_pct consumption (RefreshCombatSheet ④)
// ---------------------------------------------------------------------------------------------

/// FreezeSpeedAppliesNextRound: the sheet fold reflects an applied freeze on the next refresh, holds while
/// frozen, and restores the base after expiry. (The resolver/tick call sites of RefreshCombatSheet are
/// WP-06; this fixes the sheet calculation itself.)
void FreezeSpeedAppliesNextRound() {
    FSheetFixture fixture(12);
    const FStatusContent* freeze = fixture.content->statuses.find("status.freeze");
    CHECK(freeze != nullptr);
    auto& active = fixture.registry.get<FActiveStatuses>(fixture.unit);
    FBattleRng rng = RngWithFirstRoll(0);
    CHECK(game::TryApplyStatus(active, *freeze, 0, 0, rng));

    fixture.refresh();
    CHECK(fixture.speed() == 6); // 12 × 50%

    const std::vector<const FStatusContent*> definitions = game::CollectStatuses(active, *fixture.content);
    CHECK(game::TickStatuses(active, definitions, rng).skipTurn);
    fixture.refresh();
    CHECK(fixture.speed() == 6); // 다음 라운드(소비 후)에도 유지

    CHECK(game::TickStatuses(active, definitions, rng).skipTurn); // 만료 틱
    fixture.refresh();
    CHECK(fixture.speed() == 12); // 만료 → 배수 제거
}

/// SpeedMultipliersStack: one factor per status id, multiplied (two 50% statuses → 25%).
void SpeedMultipliersStack() {
    const FStatusContent slow =
        ControlStatus("status.slow", 0, 2, std::string(game::gameplay::kStatusModeTurnStart), 90, 50);
    const FStatusContent chill =
        ControlStatus("status.chill", 0, 2, std::string(game::gameplay::kStatusModeTurnStart), 90, 50);
    game::FActiveStatuses active;
    active.effects.push_back(FStatusEffect{"status.slow", 2, 0, false});
    active.effects.push_back(FStatusEffect{"status.chill", 2, 0, false});
    CHECK(game::StatusSpeedValue(12, active, {&slow, &chill}) == 3);

    // 중첩 = 갱신: re-applying keeps one entry per id — the factor is not squared.
    game::ApplyStatus(active, slow);
    CHECK(active.effects.size() == 2);
    CHECK(game::StatusSpeedValue(12, active, {&slow, &chill}) == 3);
}

/// SpeedFloorIsOne: a fold below 1 clamps to 1.
void SpeedFloorIsOne() {
    const FStatusContent crawl =
        ControlStatus("status.crawl", 0, 2, std::string(game::gameplay::kStatusModeTurnStart), 90, 10);
    game::FActiveStatuses active;
    active.effects.push_back(FStatusEffect{"status.crawl", 2, 0, false});
    CHECK(game::StatusSpeedValue(3, active, {&crawl}) == 1); // round(0.3) = 0 → 하한 1
}

/// ExpiryRestoresSpeed: while the status lives the factor applies; after expiry the fold is the identity.
void ExpiryRestoresSpeed() {
    const FStatusContent freeze =
        ControlStatus("status.freeze", 0, 2, std::string(game::gameplay::kStatusModeTurnStart), 90, 50);
    game::FActiveStatuses active;
    game::ApplyStatus(active, freeze);
    const std::vector<const FStatusContent*> definitions{&freeze};
    CHECK(game::StatusSpeedValue(10, active, definitions) == 5);

    FBattleRng rng = RngWithFirstRoll(0);
    (void)game::TickStatuses(active, definitions, rng); // 2 → 1
    CHECK(game::StatusSpeedValue(10, active, definitions) == 5);
    (void)game::TickStatuses(active, definitions, rng); // 만료
    CHECK(active.effects.empty());
    CHECK(game::StatusSpeedValue(10, active, definitions) == 10);
}

// ---------------------------------------------------------------------------------------------
// AI values
// ---------------------------------------------------------------------------------------------

/// ParalyzeValueHalvesUnderOnApply: on_apply is worth chance × one lost turn — no gain factor (the old
/// formula counted both turns, so the same data halves).
void ParalyzeValueHalvesUnderOnApply() {
    FAiFixture fixture;
    const FStatusContent* paralyze = fixture.content.statuses.find("status.paralyze");
    CHECK(paralyze != nullptr);
    game::ai::FUnitView target;
    target.hp = target.hpMax = 20;
    target.threat = 10;
    auto ctx = fixture.Context();
    CHECK(game::ai::ValueOfStatus(ctx, {"status.paralyze", 0, 0}, target, false) == 5); // 10 × 50% (구: 10)
    // 보정(감전 룬)이 들어가도 같은 함수가 값을 낸다.
    CHECK(game::ai::ValueOfStatus(ctx, {"status.paralyze", 0, 20}, target, false) == 7); // 10 × 70%
}

/// FreezeValueUnchanged: turn_start control keeps the old formula — threat × tick chance × gain.
void FreezeValueUnchanged() {
    FAiFixture fixture;
    game::ai::FUnitView target;
    target.hp = target.hpMax = 30;
    target.threat = 10;
    auto ctx = fixture.Context();
    CHECK(game::ai::ValueOfStatus(ctx, {"status.freeze", 0, 0}, target, false) == 20); // 10 × 100% × 2턴
}

/// PendingParalyzeHasNoValue: re-applying while the decided loss is unconsumed adds nothing; after the
/// spend the same application is worth its chance again.
void PendingParalyzeHasNoValue() {
    FAiFixture fixture;
    game::ai::FUnitView target;
    target.hp = target.hpMax = 20;
    target.threat = 10;
    target.effects.push_back(FStatusEffect{"status.paralyze", 1, 50, true});
    auto ctx = fixture.Context();
    CHECK(game::ai::ValueOfStatus(ctx, {"status.paralyze", 0, 0}, target, false) == 0);

    target.effects[0].skipPending = false; // 소비 후 — 새 굴림은 새 상실
    CHECK(game::ai::ValueOfStatus(ctx, {"status.paralyze", 0, 0}, target, false) == 5);
}

/// ControlChanceIsSingleSourced: the AI value, the application roll and the tick/display agreement all
/// derive from ControlApplyChance / ControlTickChance.
void ControlChanceIsSingleSourced() {
    FAiFixture fixture;
    const FStatusContent* paralyze = fixture.content.statuses.find("status.paralyze");
    const FStatusContent* freeze = fixture.content.statuses.find("status.freeze");
    CHECK(paralyze != nullptr && freeze != nullptr);
    CHECK(game::IsOnApplyControl(*paralyze));
    CHECK(!game::IsOnApplyControl(*freeze));

    game::ai::FUnitView target;
    target.hp = target.hpMax = 30;
    target.threat = 10;
    auto ctx = fixture.Context();
    for (const int delta : {0, 20, 40}) {
        const int expected =
            target.threat * game::ControlApplyChance(*paralyze, delta) / 100 * game::kControlLostTurns;
        CHECK(game::ai::ValueOfStatus(ctx, {"status.paralyze", 0, delta}, target, false) == expected);
    }

    game::FActiveStatuses frozen;
    game::ApplyStatus(frozen, *freeze);
    const std::vector<const FStatusContent*> definitions{freeze};
    CHECK(game::ControlTickChance(0, freeze->chanceCap) == 100);
    CHECK(game::IsTurnSkipped(frozen, definitions)); // 표시도 같은 함수
    FBattleRng rng = RngWithFirstRoll(0);
    const std::uint32_t before = rng.state;
    CHECK(game::TickStatuses(frozen, definitions, rng).skipTurn); // 굴림도 같은 값(100) → 소비 0회
    CHECK(rng.state == before);
}

} // namespace

int main() {
    // Save safety (the effect_hooks contract): ctest exports no GAME01P_SAVE_DIR for feature tests, so
    // outside build-test.ps1 a plain run would target the user's real save under %APPDATA%. Commit an
    // override before any dungeon service can touch disk. (_putenv_s/setenv: same pattern as
    // effect_hooks_tests.cpp.)
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "status_rules_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("status rules", {
        {"ParalyzeDecidedOnApply", ParalyzeDecidedOnApply},
        {"FailedApplyLeavesNoStatus", FailedApplyLeavesNoStatus},
        {"PendingSkipIsSingle", PendingSkipIsSingle},
        {"ReapplyAfterConsumeRollsAgain", ReapplyAfterConsumeRollsAgain},
        {"FreezeUnchangedByD12", FreezeUnchangedByD12},
        {"FreezeTickChanceIsAlways", FreezeTickChanceIsAlways},
        {"ParalyzeApplyChanceZeroMeansNever", ParalyzeApplyChanceZeroMeansNever},
        {"ParalyzeCapClamps", ParalyzeCapClamps},
        {"ChanceCapClamps", ChanceCapClamps},
        {"IsTurnSkippedSeesDecidedLoss", IsTurnSkippedSeesDecidedLoss},
        {"ControlApplyRollsOnce", ControlApplyRollsOnce},
        {"RngOrderIsPinned", RngOrderIsPinned},
        {"FixtureParalyzeIsOnApply", FixtureParalyzeIsOnApply},
        {"FreezeFixtureStaysTurnStart", FreezeFixtureStaysTurnStart},
        {"StatusKeysMatchDecisionRecord", StatusKeysMatchDecisionRecord},
        {"FreezeDefaultsToTurnStart", FreezeDefaultsToTurnStart},
        {"OmittedSpeedPctIsNeutral", OmittedSpeedPctIsNeutral},
        {"UnknownStatusModeRejected", UnknownStatusModeRejected},
        {"ChanceCapOutsideRangeRejected", ChanceCapOutsideRangeRejected},
        {"LegacyParalyzeValueKeepsGain", LegacyParalyzeValueKeepsGain},
        {"FreezeSpeedAppliesNextRound", FreezeSpeedAppliesNextRound},
        {"SpeedMultipliersStack", SpeedMultipliersStack},
        {"SpeedFloorIsOne", SpeedFloorIsOne},
        {"ExpiryRestoresSpeed", ExpiryRestoresSpeed},
        {"ParalyzeValueHalvesUnderOnApply", ParalyzeValueHalvesUnderOnApply},
        {"FreezeValueUnchanged", FreezeValueUnchanged},
        {"PendingParalyzeHasNoValue", PendingParalyzeHasNoValue},
        {"ControlChanceIsSingleSourced", ControlChanceIsSingleSourced},
    });
}

#include "gameplay/tactical_d20/FTacticalD20ConfigLoader.h"

#include "core/Logger.h"
#include "gameplay/tactical_d20/FTacticalD20ConfigDefaults.h"
#include "gameplay/tactical_d20/FTacticalD20ConfigReader.h"
#include "gameplay/tactical_d20/FTacticalD20Rules.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace game {
namespace reader = tactical_d20_config_reader;
namespace {

constexpr int MinAbilityScore = 8;
constexpr int MaxAbilityScore = 21;
constexpr int FallbackGridWidth = 8;
constexpr int FallbackGridHeight = 6;
constexpr int FallbackTileFeet = 5;

std::string ReadTextFile(std::string_view path) {
    std::ifstream file{std::string(path)};
    if (!file) return {};

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

int ClampAbilityScore(int score, const std::string& path, FTacticalD20Config& config) {
    const int clamped = std::clamp(score, MinAbilityScore, MaxAbilityScore);
    if (clamped != score) {
        config.warnings.push_back(path + " ability score " + std::to_string(score) + " clamped to " + std::to_string(clamped));
    }
    return clamped;
}

int ReadPositiveInt(std::string_view text, std::string_view key, int fallback, FTacticalD20Config& config) {
    const int value = reader::ReadInt(text, key, fallback);
    if (value > 0) return value;

    config.warnings.push_back(std::string(key) + " must be positive; using fallback " + std::to_string(fallback));
    return fallback;
}

FAbilityScores ParseAbilities(std::string_view unitBody, const FAbilityScores& fallback, const std::string& unitId, FTacticalD20Config& config) {
    const auto body = reader::FindObjectBody(unitBody, "abilities");
    FAbilityScores abilities = fallback;
    if (!body.empty()) {
        abilities.strength = reader::ReadInt(body, "strength", abilities.strength);
        abilities.dexterity = reader::ReadInt(body, "dexterity", abilities.dexterity);
        abilities.constitution = reader::ReadInt(body, "constitution", abilities.constitution);
        abilities.intelligence = reader::ReadInt(body, "intelligence", abilities.intelligence);
        abilities.wisdom = reader::ReadInt(body, "wisdom", abilities.wisdom);
        abilities.charisma = reader::ReadInt(body, "charisma", abilities.charisma);
    }

    abilities.strength = ClampAbilityScore(abilities.strength, unitId + ".strength", config);
    abilities.dexterity = ClampAbilityScore(abilities.dexterity, unitId + ".dexterity", config);
    abilities.constitution = ClampAbilityScore(abilities.constitution, unitId + ".constitution", config);
    abilities.intelligence = ClampAbilityScore(abilities.intelligence, unitId + ".intelligence", config);
    abilities.wisdom = ClampAbilityScore(abilities.wisdom, unitId + ".wisdom", config);
    abilities.charisma = ClampAbilityScore(abilities.charisma, unitId + ".charisma", config);
    return abilities;
}

FTacticalD20UnitConfig ParseUnit(std::string_view body, const FTacticalD20UnitConfig& fallback, FTacticalD20Config& config) {
    FTacticalD20UnitConfig unit = fallback;
    unit.id = reader::ReadString(body, "id", unit.id);
    unit.team = reader::ReadString(body, "team", unit.team);
    unit.displayName = reader::ReadString(body, "displayName", unit.displayName);
    unit.maxHp = reader::ReadInt(body, "maxHp", unit.maxHp);
    unit.armorClass = reader::ReadInt(body, "armorClass", unit.armorClass);
    unit.speedFeet = reader::ReadInt(body, "speedFeet", unit.speedFeet);

    if (const auto tile = reader::FindObjectBody(body, "startTile"); !tile.empty()) {
        unit.startTileX = reader::ReadInt(tile, "x", unit.startTileX);
        unit.startTileY = reader::ReadInt(tile, "y", unit.startTileY);
    }

    const auto proficiencies = reader::FindObjectBody(body, "proficiencies");
    unit.weaponProficiencies = reader::ReadStringArray(proficiencies, "weapons", unit.weaponProficiencies);
    unit.savingThrowProficiencies = reader::ReadStringArray(proficiencies, "savingThrows", unit.savingThrowProficiencies);
    unit.skillProficiencies = reader::ReadStringArray(proficiencies, "skills", unit.skillProficiencies);
    unit.actions = reader::ReadStringArray(body, "actions", unit.actions);
    unit.abilities = ParseAbilities(body, unit.abilities, unit.id, config);
    return unit;
}

void ParseUnits(std::string_view text, FTacticalD20Config& config) {
    const auto bodies = reader::SplitObjectBodies(reader::FindArrayBody(text, "units"));
    if (bodies.empty()) return;

    auto fallbackUnits = config.units;
    config.units.clear();
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const auto& fallback = fallbackUnits[std::min(i, fallbackUnits.size() - 1)];
        config.units.push_back(ParseUnit(bodies[i], fallback, config));
    }
}

void ParseWeapons(std::string_view text, FTacticalD20Config& config) {
    const auto bodies = reader::SplitObjectBodies(reader::FindArrayBody(text, "weapons"));
    if (bodies.empty()) return;

    auto fallbackWeapons = config.weapons;
    config.weapons.clear();
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        auto weapon = fallbackWeapons[std::min(i, fallbackWeapons.size() - 1)];
        weapon.id = reader::ReadString(bodies[i], "id", weapon.id);
        weapon.displayName = reader::ReadString(bodies[i], "displayName", weapon.displayName);
        weapon.attackType = reader::ReadString(bodies[i], "attackType", weapon.attackType);
        weapon.ability = reader::ReadString(bodies[i], "ability", weapon.ability);
        weapon.rangeTiles = reader::ReadInt(bodies[i], "rangeTiles", weapon.rangeTiles);
        const bool hasDamageDice = bodies[i].find("\"damageDice\"") != std::string_view::npos;
        weapon.damageDice = reader::ReadString(bodies[i], "damageDice", weapon.damageDice);
        weapon.damageAbility = reader::ReadString(bodies[i], "damageAbility", weapon.damageAbility);
        if (!hasDamageDice) {
            config.warnings.push_back("weapon " + weapon.id + " damageDice missing; using fallback " + weapon.damageDice);
        } else if (!reader::IsValidDiceString(weapon.damageDice)) {
            config.warnings.push_back("weapon " + weapon.id + " damageDice invalid; using fallback " + fallbackWeapons[0].damageDice);
            weapon.damageDice = fallbackWeapons[0].damageDice;
        }
        config.weapons.push_back(std::move(weapon));
    }
}

void ParseDemoAndInitiative(std::string_view text, FTacticalD20Config& config) {
    if (const auto demo = reader::FindObjectBody(text, "demo"); !demo.empty()) {
        config.gridWidth = ReadPositiveInt(demo, "gridWidth", FallbackGridWidth, config);
        config.gridHeight = ReadPositiveInt(demo, "gridHeight", FallbackGridHeight, config);
        config.tileFeet = ReadPositiveInt(demo, "tileFeet", FallbackTileFeet, config);
        const int configuredProficiencyBonus = reader::ReadInt(demo, "proficiencyBonus", config.proficiencyBonus);
        config.proficiencyBonus = TacticalD20PrototypeProficiencyBonus(configuredProficiencyBonus);
        if (configuredProficiencyBonus != config.proficiencyBonus) {
            config.warnings.push_back("demo.proficiencyBonus is fixed at +2 for this prototype; ignoring configured value "
                + std::to_string(configuredProficiencyBonus));
        }
        config.showEngineTelemetry = reader::ReadBool(demo, "showEngineTelemetry", config.showEngineTelemetry);
        config.showCombatLog = reader::ReadBool(demo, "showCombatLog", config.showCombatLog);
        config.showEventLog = reader::ReadBool(demo, "showEventLog", config.showEventLog);
        config.showStateLog = reader::ReadBool(demo, "showStateLog", config.showStateLog);
    }
    if (const auto initiative = reader::FindObjectBody(text, "initiative"); !initiative.empty()) {
        config.initiativeTieBreaker = reader::ReadString(initiative, "tieBreaker", config.initiativeTieBreaker);
    }
}

void ParseActionsConditionsAndBoard(std::string_view text, FTacticalD20Config& config) {
    if (const auto actions = reader::FindObjectBody(text, "actions"); !actions.empty()) {
        config.actions.dashExtraMovementMultiplier = reader::ReadInt(reader::FindObjectBody(actions, "dash"), "extraMovementMultiplier", config.actions.dashExtraMovementMultiplier);
        config.actions.dodgeIncomingAttackMode = reader::ReadString(reader::FindObjectBody(actions, "dodge"), "incomingAttackMode", config.actions.dodgeIncomingAttackMode);
        config.actions.dodgeExpiresAtStartOfNextTurn = reader::ReadBool(reader::FindObjectBody(actions, "dodge"), "expiresAtStartOfNextTurn", config.actions.dodgeExpiresAtStartOfNextTurn);
        config.actions.waitEndsTurn = reader::ReadBool(reader::FindObjectBody(actions, "wait"), "endsTurn", config.actions.waitEndsTurn);
    }
    if (const auto conditions = reader::FindObjectBody(text, "conditions"); !conditions.empty()) {
        const auto burning = reader::FindObjectBody(conditions, "burning");
        const bool hasBurningDice = burning.find("\"startOfTurnDamageDice\"") != std::string_view::npos;
        auto dice = reader::ReadString(burning, "startOfTurnDamageDice", config.conditions.burningDamageDice);
        if (!hasBurningDice) config.warnings.push_back("conditions.burning.startOfTurnDamageDice missing; using fallback " + config.conditions.burningDamageDice);
        else if (reader::IsValidDiceString(dice)) config.conditions.burningDamageDice = dice;
        else config.warnings.push_back("conditions.burning.startOfTurnDamageDice invalid; using fallback " + config.conditions.burningDamageDice);
        config.conditions.burningDurationRounds = reader::ReadInt(burning, "durationRounds", config.conditions.burningDurationRounds);

        const auto poisoned = reader::FindObjectBody(conditions, "poisoned");
        config.conditions.poisonedAttackRollMode = reader::ReadString(poisoned, "attackRollMode", config.conditions.poisonedAttackRollMode);
        config.conditions.poisonedAbilityCheckMode = reader::ReadString(poisoned, "abilityCheckMode", config.conditions.poisonedAbilityCheckMode);

        const auto stunned = reader::FindObjectBody(conditions, "stunned");
        config.conditions.stunnedSkipMove = reader::ReadBool(stunned, "skipMove", config.conditions.stunnedSkipMove);
        config.conditions.stunnedSkipAction = reader::ReadBool(stunned, "skipAction", config.conditions.stunnedSkipAction);
        config.conditions.stunnedDurationTurns = reader::ReadInt(stunned, "durationTurns", config.conditions.stunnedDurationTurns);
    }
    if (const auto board = reader::FindObjectBody(text, "board"); !board.empty()) {
        if (auto walls = reader::ParseTiles(reader::FindArrayBody(board, "walls")); !walls.empty()) config.walls = std::move(walls);
        if (auto cover = reader::ParseTiles(reader::FindArrayBody(board, "coverTiles")); !cover.empty()) config.coverTiles = std::move(cover);
        config.coverAcBonus = reader::ReadInt(board, "coverAcBonus", config.coverAcBonus);
    }
}

void ParseEnemyAiAndLogging(std::string_view text, FTacticalD20Config& config) {
    if (const auto ai = reader::FindObjectBody(text, "enemyAi"); !ai.empty()) {
        config.enemyAi.type = reader::ReadString(ai, "type", config.enemyAi.type);
        config.enemyAi.attackIfInRange = reader::ReadBool(ai, "attackIfInRange", config.enemyAi.attackIfInRange);
        config.enemyAi.moveTowardTargetIfOutOfRange = reader::ReadBool(ai, "moveTowardTargetIfOutOfRange", config.enemyAi.moveTowardTargetIfOutOfRange);
        config.enemyAi.noAttackAfterMoveInFirstPrototype = reader::ReadBool(ai, "noAttackAfterMoveInFirstPrototype", config.enemyAi.noAttackAfterMoveInFirstPrototype);
        config.enemyAi.enemyThinkDelaySeconds = reader::ReadFloat(ai, "enemyThinkDelaySeconds", config.enemyAi.enemyThinkDelaySeconds);
    }
    if (const auto logging = reader::FindObjectBody(text, "logging"); !logging.empty()) {
        config.logging.combatLogMaxLines = reader::ReadInt(logging, "combatLogMaxLines", config.logging.combatLogMaxLines);
        config.logging.eventLogMaxLines = reader::ReadInt(logging, "eventLogMaxLines", config.logging.eventLogMaxLines);
        config.logging.stateLogMaxLines = reader::ReadInt(logging, "stateLogMaxLines", config.logging.stateLogMaxLines);
        config.logging.consoleLogEnabled = reader::ReadBool(logging, "consoleLogEnabled", config.logging.consoleLogEnabled);
        config.logging.imguiLogEnabled = reader::ReadBool(logging, "imguiLogEnabled", config.logging.imguiLogEnabled);
        config.logging.tracyZonesEnabled = reader::ReadBool(logging, "tracyZonesEnabled", config.logging.tracyZonesEnabled);
    }
}

void ParseConfigText(std::string_view text, FTacticalD20Config& config) {
    ParseDemoAndInitiative(text, config);
    ParseUnits(text, config);
    ParseWeapons(text, config);
    ParseActionsConditionsAndBoard(text, config);
    ParseEnemyAiAndLogging(text, config);
}

} // namespace

FTacticalD20Config FTacticalD20ConfigLoader::load(std::string_view path) {
    auto config = MakeTacticalD20FallbackConfig();
    const auto text = ReadTextFile(path);
    if (text.empty()) {
        config.warnings.push_back("config missing or empty: " + std::string(path) + "; using fallback defaults");
    } else {
        ParseConfigText(text, config);
    }
    return config;
}

void FTacticalD20ConfigLoader::Initialize(entt::registry& registry, std::string_view path) {
    if (registry.ctx().contains<FTacticalD20Config>()) return;

    auto config = load(path);
    for (const auto& warning : config.warnings) {
        LOG_WARN("[TacticalD20Config] {}", warning);
    }
    registry.ctx().emplace<FTacticalD20Config>(std::move(config));
}

} // namespace game

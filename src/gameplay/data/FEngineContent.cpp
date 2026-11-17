#include "gameplay/data/FEngineContent.h"

namespace game::gameplay {

FEngineContent decodeEngine(const FContentRow& row) {
    FEngineContent engine;
    engine.id = row.text(kEngineFieldId);
    engine.name = row.text(kEngineFieldName);
    engine.requiresRunes = row.textArray(kEngineFieldRequiresRunes);
    if (const FContentRow* trigger = row.object(kEngineFieldTrigger)) {
        engine.triggerKind = trigger->text(kEngineTriggerFieldKind);
        engine.triggerStatusId = trigger->text(kEngineTriggerFieldStatusId);
        engine.minMagnitude = trigger->integer(kEngineTriggerFieldMinMagnitude, 0);
    }
    engine.log = row.text(kEngineFieldLog);
    engine.text = row.text(kEngineFieldText);
    if (const FContentRow* effect = row.object(kEngineFieldEffect)) {
        engine.effectKind = effect->text(kEngineEffectFieldKind);
        engine.effectPercent = effect->integer(kEngineEffectFieldPercent, 0);
        engine.effectTurns = effect->integer(kEngineEffectFieldTurns, 0);
    }
    return engine;
}

} // namespace game::gameplay

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
    return engine;
}

} // namespace game::gameplay

#pragma once
#include "states/GameStateMachine.h"    // V7: ecs layer must not know states/
#include "states/FHubState.h"
namespace game::ecs { struct ZzStateLeak { int flag{0}; }; }

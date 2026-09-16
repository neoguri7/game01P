#pragma once

// V27 [R32] silent system: an ISystem with no observation point at all — no ZoneScopedN span, no LOG_*.
// Nothing in a live build can answer "is this system running, and how long does it take?" (S15/S16 telemetry
// and forensic logs; S18 Tracy spans are part of the code and compile to nothing when disabled).
#include "ecs/systems/ISystem.h"

namespace game::ecs {
	struct ZzSilentSystem : public ISystem {
		const char* name() const override { return "ZzSilent"; }
		void update(entt::registry& registry, float dt) override {
			(void)registry;
			(void)dt;
		}
	};
}

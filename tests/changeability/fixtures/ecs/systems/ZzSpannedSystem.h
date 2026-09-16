#pragma once

// V27 negative control [R32]: a system that has NO LOG_* but DOES have a Tracy span is observed, so the
// silent-system detector must not flag it (S18: the span is the observation point, not the log line).
#include <tracy/Tracy.hpp>
#include "ecs/systems/ISystem.h"

namespace game::ecs {
	struct ZzSpannedSystem : public ISystem {
		const char* name() const override { return "ZzSpanned"; }
		void update(entt::registry& registry, float dt) override {
			ZoneScopedN("ZzSpanned");
			(void)registry;
			(void)dt;
		}
	};
}

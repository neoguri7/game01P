#pragma once

// V26 [R31] logging deleted from some builds: two LOG_* calls sit inside a preprocessor block, so the
// shipping/dedicated build loses the only forensic trace of the damage path (S15 ryg instruments the source
// itself; S16 Valve forensic debugging of release builds; S17 UE_LOG keeps the call in code and filters by
// category/verbosity). The third LOG_* call below is OUTSIDE the block and must NOT be reported.
#include <cstdint>

namespace game::core {
	struct FZzGuarded {
		int health{100};

		void Damage(int amount) {
#if !defined(GAME_SHIPPING)
			LOG_INFO("ZzGuarded: damage {} (health {})", amount, health);
#endif
			health -= amount;
#ifdef GAME_VERBOSE_COMBAT
			LOG_DEBUG("ZzGuarded: health after damage {}", health);
#endif
		}

		void Heal() {
			LOG_INFO("ZzGuarded: heal from {}", health);
			health += 10;
		}
	};
}

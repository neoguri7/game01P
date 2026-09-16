#pragma once

// V24 [R24] debug/visual toggles scattered as per-entity fields instead of one registered debug surface
// (DOOM 3 shape: cvars + console commands as the only uniform tuning surface, neo/framework/CVarSystem.h:217,
// neo/framework/CmdSystem.h:80). One debug/config service owns these.
struct FZzToggles {
	bool showDebugGrid;
	bool debugDrawEnabled;
	bool drawColliderOutlines;
};

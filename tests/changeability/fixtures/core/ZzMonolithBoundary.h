#pragma once

// V29 [R29] versionless boundary struct carrying raw interface pointers (DOOM 3 gameImport_t shape:
// neo/game/Game.h:326 GAME_API_VERSION, :328 gameImport_t with 14 raw pointers, re-injected into globals at
// neo/game/Game_local.cpp:98). A boundary, if one exists, carries a version and no owning raw pointers.
struct ZzRenderer;
struct ZzSound;
struct ZzFileSystem;

struct ZzEngineImport {
	ZzRenderer * renderer;
	ZzSound * sound;
	ZzFileSystem * files;
};

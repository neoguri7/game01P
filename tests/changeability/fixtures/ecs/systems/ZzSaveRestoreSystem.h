#pragma once

// V28 [R28] a hand-written per-feature Save/Restore pair (DOOM 3 shape: Save/Restore function pointers baked
// into idTypeInfo at neo/game/gamesys/Class.h:110, neo/game/gamesys/SaveGame.h:48-69, Entity.cpp:628). State
// belongs to components; snapshots enumerate components and carry a schema version.
struct ZzSaveGame;

struct ZzSaveRestoreSystem {
	void Save( ZzSaveGame & out );
	void Restore( ZzSaveGame & in );
};

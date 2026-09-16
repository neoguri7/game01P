#pragma once

// V30 [R30] fork-by-copy of a layer/module for a variant (DOOM 3 shape: neo/d3xp/ is a near-verbatim copy of
// neo/game/ — 140 files vs 136 — both exporting the same GetGameAPI; expansion differences belong in data
// overlays and capability composition, never in a copied tree).
//
// The two files ecs/ZzVariantCopy.h and debug/ZzVariantCopy.h are deliberate near-duplicates: the R30 check
// flags duplicate basenames only when BOTH files are >= 40 lines and >= 60% of the shorter file's unique
// lines also appear in the longer one, so a 3-line forwarding shim is not flagged.
struct ZzVariantCopyConfig {
	int maxEntities;
	int maxProjectiles;
	float spawnRadius;
	float recycleDistance;
	bool keepCorpses;
	bool drawDebugOverlay;
};

inline ZzVariantCopyConfig ZzVariantCopyDefaults() {
	ZzVariantCopyConfig cfg{};
	cfg.maxEntities = 1024;
	cfg.maxProjectiles = 256;
	cfg.spawnRadius = 12.0f;
	cfg.recycleDistance = 40.0f;
	cfg.keepCorpses = false;
	cfg.drawDebugOverlay = false;
	return cfg;
}

inline bool ZzVariantCopyValid( const ZzVariantCopyConfig & cfg ) {
	if ( cfg.maxEntities <= 0 ) {
		return false;
	}
	if ( cfg.maxProjectiles < 0 ) {
		return false;
	}
	if ( cfg.spawnRadius <= 0.0f ) {
		return false;
	}
	if ( cfg.recycleDistance < cfg.spawnRadius ) {
		return false;
	}
	return true;
}

inline float ZzVariantCopyRecycleSq( const ZzVariantCopyConfig & cfg ) {
	return cfg.recycleDistance * cfg.recycleDistance;
}

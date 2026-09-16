#pragma once

// V21 [R21] self-registering type machinery via a declaration macro (DOOM 3 CLASS_DECLARATION shape:
// neo/game/gamesys/Class.h:110 + Class.cpp:65 static-constructor registration). The modern form is an
// explicit register_all() in one place.
#define ZZ_CLASS_DECLARATION( super, cls ) \
	static cls::ZzTypeInfo cls##Type

struct ZzTypeInfo {
	const char * name;
};

struct ZzEntity {
	static ZzTypeInfo ZzEntityType;
};
static ZzTypeInfo ZzEntityType;

#pragma once

// V27 [R27] name-based dispatch plus void* argument packing (DOOM 3 idEvent shape: neo/game/gamesys/Event.cpp:135
// contiguous eventnum, Class.h:56 idEventArg packed into int, Event.cpp:532 ProcessEventArgPtr). Typed event
// structs through FEventBus are the contract.
struct ZzBus {
	void publish( const char * name, int value );
};

struct ZzStringEventSystem {
	void Fire( const char * name, void * payload );

	static void PublishAll( ZzBus & bus ) {
		bus.publish( "damage", 1 );
	}
};

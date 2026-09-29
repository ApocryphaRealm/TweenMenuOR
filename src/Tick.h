#pragma once

#include <Xinput.h>   // types only - nothing is linked or loaded

// ============================================================================================================
// A game-thread tick. OBSE64 has no task or main-loop interface, so - as Improved Wheel Menu does - the game's own
// XINPUT1_3!XInputGetState import slot is chained (the previous target is kept and called, so either plugin can
// install first) and every read on the game's main thread calls the callback. Nothing about the pad is changed.
// Never loads an XInput DLL (gate oblivion-plugin-never-loads-xinput). Engine objects are only ever touched from
// this callback (logic library 7662: a plugin thread's StaticFindObject sat beside a UObjectArray start-up crash).
//
// Known gap: UE reads a controller every frame only while one is connected; a keyboard-only player gets reads
// only when UE looks for a newly connected pad. To be closed before release (PLAN.md).
// ============================================================================================================

namespace tick
{
	// a_state: the controller state the game is about to receive (nullptr when no pad is connected); the callback may
	// change it (the open menu hands the game an empty pad - and moves dwPacketNumber, or the game ignores the change)
	using Callback = void (*)(XINPUT_STATE* a_state);
	bool Install(Callback a_callback);   // at OBSE's post-load, on the game's main thread
	std::uint64_t Reads();
}

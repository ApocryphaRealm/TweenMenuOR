#pragma once

#include <Xinput.h>   // types only - nothing is linked or loaded

// ============================================================================================================
// Two game-thread hooks, both by chaining an import slot of the game's executable (the previous target is kept and
// called, so other plugins chaining the same slot work in either order). OBSE64 has no task or main-loop interface.
//
//   * USER32!PeekMessageW - Unreal's message pump calls it every frame on the game's main thread, controller or no
//     controller: the FRAME tick (at most once every 2 ms; the pump calls it once per queued message).
//     Found 2026-09-29: with the controller asleep the game stops calling XInputGetState, and a tick taken from it
//     (the first builds) never ran - no menu for a keyboard player.
//   * XINPUT1_3!XInputGetState - the controller state the game is about to receive: the open menu reads it and hands
//     the game an empty pad (moving dwPacketNumber, or the game ignores the change).
//
// Never loads an XInput DLL (gate oblivion-plugin-never-loads-xinput). Engine objects are only ever touched from these
// callbacks (logic library 7662).
// ============================================================================================================

namespace tick
{
	using FrameCallback = void (*)();
	using PadCallback = void (*)(XINPUT_STATE* a_state);   // nullptr when no pad is connected
	bool Install(FrameCallback a_frame, PadCallback a_pad);   // at OBSE's post-load, on the game's main thread
	std::uint64_t Reads();
}

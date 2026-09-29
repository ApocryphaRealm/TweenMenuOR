#pragma once

// ============================================================================================================
// M1 (PLAN.md): the tween menu's own input action, linked to Wait's key binding.
//
// Wait is IA_Game_Default_OpenRestMenu in IMC_Game_Default, on Select (pad) and T (keyboard) by default. The owner,
// 2026-09-29: "wherever the wait button is bound, the tween menu follows" - "it will just use the key binding for wait,
// not the function". So at the first tick with everything loaded (game thread), and again whenever gameplay resumes
// and every 60 ticks:
//   * create IA_TweenMenu_Open once (rooted),
//   * move every key Wait has in IMC_Game_Default to it (Wait keeps its unbound entries, so the tween menu's Wait
//     option still opens it) and drop any key Wait no longer has,
//   * put Start on System (bStartOpensSystem),
//   * ask Enhanced Input to rebuild its mappings when anything changed.
// Each tick in gameplay the keys bound to our action are checked with the player controller's IsInputKeyDown.
// ============================================================================================================

namespace input
{
	void Tick();             // game thread, every tick
	bool CreateEarly();      // at OBSE's post-load: our input actions, if the engine can make objects yet
	bool TakePressed();      // the tween key was pressed since the last call (gameplay only)

	// the menu's layout, fixed for 1.0 - one entry per function, side 0 up, 1 right, 2 down, 3 left
	struct LayoutEntry
	{
		std::string    id;
		std::wstring   label;
		const wchar_t* action;
		int            side;
	};
	std::vector<LayoutEntry> Layout();
	bool TweenKeyPressedOnPad(WORD a_pressed);   // the tween key is a controller button and it is among a_pressed (XInput bits)

	struct Status
	{
		bool        actionCreated = false;
		bool        mapped = false;
		int         waitKeysMoved = 0;
		int         rowsAdded = 0;
		std::string boundKeys;
		std::uint64_t presses = 0;
		std::string problem;
	};
	Status GetStatus();
}

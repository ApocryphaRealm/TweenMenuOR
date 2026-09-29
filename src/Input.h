#pragma once

// ============================================================================================================
// M1 (PLAN.md): the tween menu's own input action and its row on the game's Controls page.
//
// Settled 2026-09-29 (probes): the Controls page is data - DT_Modern_Settings_GamepadRebind_Data and
// ..._KeyboardRebind_Data (VModernSettingRebindData : DataTable, row ModernRebindSettingTableRow), whose
// RebindSettings array holds the rows in order (headers Type 0, actions Type 2 with RebindData {InputAction,
// MappingContext, defaults}). Wait is IA_Game_Default_OpenRestMenu in IMC_Game_Default on Select (pad) and T
// (keyboard).
//
// At the first tick with everything loaded (game thread):
//   * create IA_TweenMenu_Open (an InputAction; kept alive by the mapping context and the rows that point at it),
//   * map it in IMC_Game_Default to the INI's pad and keyboard keys, and take those keys off Wait while Wait is still
//     on them (a player who rebound Wait keeps theirs) - the owner, 2026-09-29: "Wait becomes an option",
//   * put a "Tween Menu" row on both Controls pages under the Menu header (before OpenCharacter),
//   * ask Enhanced Input to rebuild its mappings.
// Each tick in gameplay the keys bound to our action (QueryKeysMappedToAction - so a rebind on the Controls page is
// followed) are checked with the player controller's WasInputKeyJustPressed.
// ============================================================================================================

namespace input
{
	void Tick();             // game thread, every tick
	bool CreateEarly();      // at OBSE's post-load: our input actions, if the engine can make objects yet
	bool TakePressed();      // the tween key was pressed since the last call (gameplay only)

	// M6: the menu's layout as the Controls page's "Tween Menu Layout" rows have it - one entry per function, side =
	// the D-pad direction bound to its row (0 up, 1 right, 2 down, 3 left; -1 = not in the menu). Kept in the INI.
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

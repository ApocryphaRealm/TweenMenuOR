#pragma once

#include <Xinput.h>

// ============================================================================================================
// M2 (PLAN.md): the menu. The owner, 2026-09-29: "just use the boxes that appear in the controls page ... a brown
// colored box that will have the name of the function ... no background ... a bunch of boxes with names in four
// directions".
//
// Built at runtime from the game's own pieces: a UserWidget whose tree is one CanvasPanel (no background), and per
// option a WBP_OriginalImageTile_C carrying the Controls row's own brush (T_Setting_ListFrame_Defauft; the selected
// one T_Setting_ListFrame_Hover - both read from WBP_Modern_Settings_GamepadRebindWidget's template tiles) with a
// WBP_AltarTextBlock_C label styled as that row's RebindLabel. Classic layout (Tween Menu Overhaul): four
// categories, one per direction; the first direction pressed picks one, pressing along it steps through its options.
// A opens the option (after the menu has closed, its input action is fired the way a key press would - the game's
// own menu opens, with the game's own rules), B or the tween key closes. While the menu is open the controller is
// the menu's: the game gets an empty pad, and after it closes the buttons stay swallowed until they are let go.
// ============================================================================================================

namespace menu
{
	void Toggle();                          // the tween key
	bool IsOpen();
	void OnPad(XINPUT_STATE* a_state);      // every controller read on the game thread (nullptr: no pad)
	void Tick();                            // every tick: fires a chosen option once the menu has gone
	std::string Status();
}

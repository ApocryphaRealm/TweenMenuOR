#pragma once

// The owner, 2026-09-29: System "remembers whatever subsection of the system tab it was on, which I don't like. I just
// want it to always go to its far left tab." Each time the settings menu (WBP_Modern_SettingsMenu_C) activates - from
// Start, the tween menu's System option or anywhere else - the settings view model's page index is set to 0 (Save &
// Load) before the menu's own activation runs. [Keys] bSystemOpensFirstPage.
namespace systempage
{
	void Tick();   // game thread: watches the settings menu once its class has loaded
}

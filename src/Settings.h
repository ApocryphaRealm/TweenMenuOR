#pragma once

// TweenMenu.ini beside the plugin. Read once at load; the compiled defaults are the shipped INI's values (rule 16).

namespace settings
{
	struct Values
	{
		int         logLevel = 2;                          // [Log] uLogLevel: 0 trace .. 4 error (shipped at info)
		std::string gamepadKey = "Gamepad_Special_Left";   // [Keys] sGamepadKey: the tween menu's controller button (Select / View)
		std::string keyboardKey = "T";                     // [Keys] sKeyboardKey: its keyboard key (Wait's T - Wait is a tween menu option)
		bool        startOpensSystem = true;               // [Keys] bStartOpensSystem: Start opens System instead of the Character page
		bool        systemOpensFirstPage = true;           // [Keys] bSystemOpensFirstPage: System always opens on its far-left page (Save & Load)
	};

	void Load();
	const Values& Get();
	std::filesystem::path PluginFolder();   // ...\OblivionRemastered\Binaries\Win64\OBSE\Plugins
}

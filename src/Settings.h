#pragma once

// TweenMenu.ini beside the plugin. Read once at load; the compiled defaults are the shipped INI's values (rule 16).

namespace settings
{
	struct Values
	{
		int         logLevel = 2;                       // [Log] uLogLevel: 0 trace .. 4 error (shipped at info)
		std::string layout = "Alternative";             // [Layout] sLayout: Alternative (one row) | Classic (four categories)
		std::string gamepadKey = "Gamepad_Special_Left";   // [Keys] sGamepadKey: the default controller button (Select / View)
		bool        startOpensSystem = true;
		bool        systemOpensFirstPage = true;      // [Keys] bSystemOpensFirstPage: System always opens on its far-left page (Save & Load)          // [Keys] bStartOpensSystem: Start opens System (Save & Load) instead of the Character page
		std::string keyboardKey = "T";                  // [Keys] sKeyboardKey: the default keyboard key (Wait's T - Wait becomes an option)
	};

	void Load();
	// a rebind on the game's Controls page is kept here: the game re-applies its saved rebinds before our action
	// exists, so our own INI carries the tween key across restarts (found 2026-09-29: D-pad Up came back as Select)
	void SaveKeys(const std::string& a_gamepadKey, const std::string& a_keyboardKey);

	// any [section] key=value of TweenMenu.ini (the tween layout rows keep their bindings here, as SaveKeys does)
	std::string ReadIni(const char* a_section, const std::string& a_key, const std::string& a_default);
	void        WriteIni(const char* a_section, const std::string& a_key, const std::string& a_value);
	const Values& Get();
	std::filesystem::path PluginFolder();   // ...\OblivionRemastered\Binaries\Win64\OBSE\Plugins
}

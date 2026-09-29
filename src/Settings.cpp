#include "Settings.h"

namespace settings
{
	namespace
	{
		Values g_values;

		std::filesystem::path ThisModule()
		{
			HMODULE self = nullptr;
			wchar_t buf[MAX_PATH]{};
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&ThisModule), &self) &&
				GetModuleFileNameW(self, buf, MAX_PATH)) {
				return std::filesystem::path(buf);
			}
			return {};
		}

		int ReadInt(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, int a_default, int a_min, int a_max)
		{
			const int v = static_cast<int>(GetPrivateProfileIntW(a_section, a_key, a_default, a_ini.c_str()));
			return std::clamp(v, a_min, a_max);
		}

		std::string ReadString(const std::filesystem::path& a_ini, const wchar_t* a_section, const wchar_t* a_key, const std::string& a_default)
		{
			wchar_t buf[256]{};
			const std::wstring def(a_default.begin(), a_default.end());
			GetPrivateProfileStringW(a_section, a_key, def.c_str(), buf, 256, a_ini.c_str());
			std::string out;
			for (const wchar_t* p = buf; *p; ++p) {
				out += static_cast<char>(*p < 128 ? *p : '?');
			}
			return out;
		}
	}

	std::filesystem::path PluginFolder()
	{
		return ThisModule().parent_path();
	}

	void Load()
	{
		const auto ini = PluginFolder() / L"TweenMenu.ini";
		Values v;
		if (std::filesystem::exists(ini)) {
			v.logLevel = ReadInt(ini, L"Log", L"uLogLevel", v.logLevel, 0, 4);
			v.gamepadKey = ReadString(ini, L"Keys", L"sGamepadKey", v.gamepadKey);
			v.keyboardKey = ReadString(ini, L"Keys", L"sKeyboardKey", v.keyboardKey);
			v.systemOpensFirstPage = ReadInt(ini, L"Keys", L"bSystemOpensFirstPage", v.systemOpensFirstPage ? 1 : 0, 0, 1) != 0;
			v.startOpensSystem = ReadInt(ini, L"Keys", L"bStartOpensSystem", v.startOpensSystem ? 1 : 0, 0, 1) != 0;
			g_values = v;
			logger::info("settings: {} read (uLogLevel={}, sGamepadKey={}, sKeyboardKey={}, bStartOpensSystem={}, bSystemOpensFirstPage={})",
				ini.string(), v.logLevel, v.gamepadKey, v.keyboardKey, v.startOpensSystem, v.systemOpensFirstPage);
		} else {
			g_values = v;
			logger::warn("settings: {} not found - compiled defaults in use", ini.string());
		}
	}

	const Values& Get()
	{
		return g_values;
	}
}

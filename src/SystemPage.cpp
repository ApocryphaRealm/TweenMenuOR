#include "SystemPage.h"

#include "Settings.h"
#include "Ue.h"

namespace systempage
{
	namespace
	{
		constexpr const wchar_t* kMenuClass = L"/Game/UI/Modern/MenuLayer/Settings/WBP_Modern_SettingsMenu.WBP_Modern_SettingsMenu_C";
		bool           g_watching = false;
		UE::UFunction* g_fnActivated = nullptr;

		void FirstPage()
		{
			auto* vm = ue::FirstOf(ue::Class(L"/Script/Altar.VSettingsMenuViewModel"));
			if (!vm) {
				logger::warn("system page: no settings view model - left on its remembered page");
				return;
			}
			ue::Call c(vm, L"SetPageIndex");
			if (!c) {
				return;
			}
			void* first = nullptr;
			for (const auto& [name, off] : reflect::Fields(reinterpret_cast<UE::UStruct*>(vm->FindFunction(UE::FName(L"SetPageIndex", UE::EFindName::Find))))) {
				first = c.At(name);
				break;
			}
			if (first) {
				*static_cast<std::int32_t*>(first) = 0;
				c.Run();
				logger::info("system page: opening on its first page (Save & Load)");
			}
		}

		void OnMenuEvent(UE::UObject*, UE::UFunction* a_fn, void*)
		{
			if (!g_fnActivated && pe::FunctionName(a_fn) == "BP_OnActivated") {
				g_fnActivated = a_fn;
			}
			if (a_fn == g_fnActivated) {
				FirstPage();   // before the menu's own activation reads the page index
			}
		}
	}

	void Tick()
	{
		if (g_watching || !settings::Get().systemOpensFirstPage) {
			return;
		}
		static std::uint64_t n = 0;
		if (++n % 30 != 0) {
			return;
		}
		if (auto* cls = ue::Class(kMenuClass)) {
			g_watching = pe::Watch(cls, &OnMenuEvent);
			if (g_watching) {
				logger::info("system page: watching the System menu - it will always open on its far-left page");
			}
		}
	}
}

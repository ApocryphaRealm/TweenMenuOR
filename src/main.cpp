// Tween Menu for Oblivion Remastered - plan: 4. plans\tween-menu-oblivion\PLAN.md.
#include "Input.h"
#include "Menu.h"
#include "Settings.h"
#include "SystemPage.h"
#include "Tick.h"
#include "Tool.h"

namespace
{
	// every frame (from the message pump - controller or not)
	void OnFrame()
	{
		tool::Pump();
		input::Tick();
		if (input::TakePressed()) {
			menu::Toggle();
		}
		menu::Keys();
		menu::Tick();
		systempage::Tick();
		static bool toolRegistered = false;
		static std::uint64_t n = 0;
		if (!toolRegistered && ++n % 60 == 0) {
			toolRegistered = tool::Register();
		}
	}

	// every controller read: while the menu is open the pad is the menu's
	void OnPad(XINPUT_STATE* a_state)
	{
		menu::OnPad(a_state);
	}

	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		if (a_msg->type != OBSE::MessagingInterface::kPostLoad) {
			return;
		}
		tick::Install(&OnFrame, &OnPad);
	}
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	settings::Load();
	{
		const auto level = static_cast<spdlog::level::level_enum>(std::clamp(settings::Get().logLevel, 0, 4));
		logger::set_level(level, level);
	}
	logger::info("Tween Menu {} loaded (Oblivion Remastered)", TWM_VERSION);
	if (auto* messaging = OBSE::GetMessagingInterface(); !messaging || !messaging->RegisterListener(&OnMessage)) {
		logger::error("OBSE messaging unavailable - the tween menu will not start");
	}
	return true;
}

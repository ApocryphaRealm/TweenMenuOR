// Tween Menu for Oblivion Remastered - plan: 4. plans\tween-menu-oblivion\PLAN.md.
#include "Input.h"
#include "Menu.h"
#include "Settings.h"
#include "Tick.h"
#include "Tool.h"

namespace
{
	void OnTick(XINPUT_STATE* a_state)
	{
		tool::Pump();
		menu::OnPad(a_state);   // first: while the menu is open the pad is the menu's
		input::Tick();
		if (input::TakePressed()) {
			menu::Toggle();
		}
		menu::Tick();
		static bool toolRegistered = false;
		static std::uint64_t n = 0;
		if (!toolRegistered && ++n % 60 == 0) {
			toolRegistered = tool::Register();
		}
	}

	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != OBSE::MessagingInterface::kPostLoad) {
			return;
		}
		tick::Install(&OnTick);
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

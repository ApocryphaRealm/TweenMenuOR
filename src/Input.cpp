#include "Input.h"

#include <Xinput.h>   // constants only

#include "PEHook.h"
#include "Reflect.h"
#include "Settings.h"
#include "Strings.h"
#include "Ue.h"

namespace input
{
	namespace
	{
		constexpr const wchar_t* kIMC = L"/Game/Dev/Input/GamePlay/InputMappingContexts/IMC_Game_Default.IMC_Game_Default";
		constexpr const wchar_t* kPadTable = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/DT_Modern_Settings_GamepadRebind_Data.DT_Modern_Settings_GamepadRebind_Data";
		constexpr const wchar_t* kKeyTable = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Keyboard/DT_Modern_Settings_KeyboardRebind_Data.DT_Modern_Settings_KeyboardRebind_Data";
		constexpr const wchar_t* kKeyTableFr = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Keyboard/DT_Modern_Settings_KeyboardRebind_Fr_Data.DT_Modern_Settings_KeyboardRebind_Fr_Data";
		constexpr const char*    kWaitAction = "IA_Game_Default_OpenRestMenu";
		constexpr const char*    kFirstMenuAction = "IA_Game_Default_OpenStatsMenu";   // the row ours goes before
		constexpr const wchar_t* kOurAction = L"IA_TweenMenu_Open";

		struct RawArray
		{
			std::uint8_t* data;
			std::int32_t  num;
			std::int32_t  max;
		};

		Status             g_status;
		bool               g_done = false;
		UE::UObject*       g_action = nullptr;
		UE::UObject*       g_imc = nullptr;
		std::vector<UE::FName> g_keys;   // bound to our action now
		std::uint64_t      g_tickCount = 0;
		bool               g_pressed = false;

		std::string NameOf(UE::UObject* a_o)
		{
			return a_o ? pe::Utf8(a_o->GetFName().ToString()) : std::string("null");
		}

		template <class T = UE::UObject>
		T* Find(const wchar_t* a_path)
		{
			return static_cast<T*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, a_path));
		}

		UE::UStruct* Struct(const wchar_t* a_path)
		{
			return UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, a_path);
		}

		// A reflected call: parameters by name, laid out from the UFunction's own properties.
		class Call
		{
		public:
			Call(UE::UObject* a_obj, const wchar_t* a_fn) :
				m_obj(a_obj),
				m_fn(a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr)
			{
				if (m_fn) {
					m_params.assign(static_cast<std::size_t>(reinterpret_cast<UE::UStruct*>(m_fn)->propertiesSize), 0);
				}
			}
			explicit operator bool() const { return m_fn != nullptr; }
			void* At(std::string_view a_name)
			{
				const auto off = reflect::Offset(reinterpret_cast<UE::UStruct*>(m_fn), a_name);
				return off >= 0 ? m_params.data() + off : nullptr;
			}
			template <class T>
			void Set(std::string_view a_name, const T& a_value)
			{
				if (void* p = At(a_name)) {
					std::memcpy(p, &a_value, sizeof(T));
				}
			}
			void Run() { m_obj->ProcessEvent(m_fn, m_params.data()); }

		private:
			UE::UObject*              m_obj;
			UE::UFunction*            m_fn;
			std::vector<std::uint8_t> m_params;
		};

		void SetKey(void* a_at, const std::string& a_keyName)
		{
			// FKey: FName + TSharedPtr<FKeyDetails> (resolved lazily by the engine); written as a fresh key
			std::memset(a_at, 0, sizeof(UE::FKey));
			new (a_at) UE::FKey(UE::FName(std::wstring(a_keyName.begin(), a_keyName.end()).c_str()));
		}

		// ---- the mapping context ----

		struct MappingLayout
		{
			std::int32_t size = 0, action = -1, key = -1;
		};

		MappingLayout Mappings()
		{
			MappingLayout m;
			if (auto* st = Struct(L"/Script/EnhancedInput.EnhancedActionKeyMapping")) {
				m.size = st->propertiesSize;
				m.action = reflect::Offset(st, "Action");
				m.key = reflect::Offset(st, "Key");
			}
			return m;
		}

		RawArray* MappingArray(UE::UObject* a_imc)
		{
			return reflect::At<RawArray>(a_imc, reflect::Offset(a_imc->GetClass(), "Mappings"));
		}

		bool IsMapped(UE::UObject* a_imc, UE::UObject* a_action, const std::string& a_key)
		{
			const auto m = Mappings();
			auto* arr = MappingArray(a_imc);
			if (!arr || m.size <= 0 || m.action < 0 || m.key < 0) {
				return false;
			}
			for (std::int32_t i = 0; i < arr->num; ++i) {
				std::uint8_t* e = arr->data + static_cast<std::ptrdiff_t>(i) * m.size;
				auto* act = *reinterpret_cast<UE::UObject**>(e + m.action);
				const auto& key = *reinterpret_cast<const UE::FName*>(e + m.key);
				if (act == a_action && (a_key.empty() || pe::Utf8(key.ToString()) == a_key)) {
					return true;
				}
			}
			return false;
		}

		UE::UObject* FindMappedAction(UE::UObject* a_imc, const std::string& a_name)
		{
			const auto m = Mappings();
			auto* arr = MappingArray(a_imc);
			for (std::int32_t i = 0; arr && m.size > 0 && i < arr->num; ++i) {
				auto* act = *reinterpret_cast<UE::UObject**>(arr->data + static_cast<std::ptrdiff_t>(i) * m.size + m.action);
				if (act && NameOf(act) == a_name) {
					return act;
				}
			}
			return nullptr;
		}

		bool MapKey(UE::UObject* a_imc, UE::UObject* a_action, const std::string& a_key)
		{
			Call c(a_imc, L"MapKey");
			void* key = c ? c.At("ToKey") : nullptr;
			if (!key) {
				return false;
			}
			c.Set("Action", a_action);
			SetKey(key, a_key);
			c.Run();
			static_cast<UE::FKey*>(key)->~FKey();
			return IsMapped(a_imc, a_action, a_key);
		}

		bool UnmapKey(UE::UObject* a_imc, UE::UObject* a_action, const std::string& a_key)
		{
			Call c(a_imc, L"UnmapKey");
			void* key = c ? c.At("Key") : nullptr;
			if (!key) {
				return false;
			}
			c.Set("Action", a_action);
			SetKey(key, a_key);
			c.Run();
			static_cast<UE::FKey*>(key)->~FKey();
			return !IsMapped(a_imc, a_action, a_key);
		}

		// ---- the Controls page rows ----

		// ---- the tween menu's functions and their sides ----
		// Fixed for 1.0 (the owner, 2026-09-29: "keep my most current layout ... magic on left, inventory on right, with
		// the character menu and quest above and the map and wait menu below"; rebinding from the Controls page
		// postponed). Each opens through the game's own input action.
		struct Function
		{
			const char*    id;
			const char*    label;    // English; the shown text is TR("TWM_<id>", label)
			const wchar_t* action;   // the game's own input action that opens it
			int            side;     // default: 0 up, 1 right, 2 down, 3 left, -1 not in the menu
		};

		const std::array<Function, 6> kFunctions{ {
			{ "Character", "Character", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenStatsMenu.IA_Game_Default_OpenStatsMenu", 0 },
			{ "Quests", "Quests", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenQuestMenu.IA_Game_Default_OpenQuestMenu", 0 },
			{ "Inventory", "Inventory", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenInventoryMenu.IA_Game_Default_OpenInventoryMenu", 1 },
			{ "Magic", "Magic", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenMagicMenu.IA_Game_Default_OpenMagicMenu", 3 },
			{ "Map", "Map", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenMapMenu.IA_Game_Default_OpenMapMenu", 2 },
			{ "Wait", "Wait", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenRestMenu.IA_Game_Default_OpenRestMenu", 2 },
		} };
		// The owner, 2026-09-29: the Start button (right, Menu) opens System - "the system part of the menu where you go
		// to save and load saves" - instead of the tabbed menu's Character page. Whatever IMC_Game_Default has on Start
		// gives it up; the game's own OpenOptionsMenu action takes it, and the Controller page's OpenSystem row gets
		// Start as its default (so its Reset gives Start back).
		void StartOpensSystem()
		{
			constexpr const char* kStart = "Gamepad_Special_Right";
			auto* system = FindMappedAction(g_imc, "IA_Game_Default_OpenOptionsMenu");
			if (!system) {
				logger::warn("input: OpenOptionsMenu is not in {} - Start left as it is", NameOf(g_imc));
				return;
			}
			if (IsMapped(g_imc, system, kStart)) {
				return;
			}
			const auto m = Mappings();
			auto* arr = MappingArray(g_imc);
			std::vector<UE::UObject*> onStart;
			for (std::int32_t i = 0; arr && m.size > 0 && i < arr->num; ++i) {
				std::uint8_t* e = arr->data + static_cast<std::ptrdiff_t>(i) * m.size;
				if (pe::Utf8(reinterpret_cast<const UE::FName*>(e + m.key)->ToString()) == kStart) {
					onStart.push_back(*reinterpret_cast<UE::UObject**>(e + m.action));
				}
			}
			for (auto* a : onStart) {
				if (a && UnmapKey(g_imc, a, kStart)) {
					logger::info("input: {} no longer on Start", NameOf(a));
				}
			}
			logger::info("input: Start now opens System (Save & Load) - {} ({})", NameOf(system), MapKey(g_imc, system, kStart) ? "ok" : "FAILED");
			// the OpenSystem row's default, so the Controls page's Reset returns Start
			auto* table = Find(kPadTable);
			auto* rowStruct = Struct(L"/Script/Altar.ModernRebindSettingTableRow");
			auto* dataStruct = Struct(L"/Script/Altar.ModernRebindData");
			if (!table || !rowStruct || !dataStruct) {
				return;
			}
			auto* rows = reflect::At<RawArray>(table, reflect::Offset(table->GetClass(), "RebindSettings"));
			const auto size = rowStruct->propertiesSize;
			const auto oData = reflect::Offset(rowStruct, "RebindData");
			const auto oAction = reflect::Offset(dataStruct, "InputAction");
			const auto oPad = reflect::Offset(dataStruct, "DefaultPrimaryGamepadKey");
			for (std::int32_t i = 0; rows && oData >= 0 && oAction >= 0 && oPad >= 0 && i < rows->num; ++i) {
				std::uint8_t* row = rows->data + static_cast<std::ptrdiff_t>(i) * size;
				if (*reinterpret_cast<UE::UObject**>(row + oData + oAction) == system) {
					*reinterpret_cast<UE::FName*>(row + oData + oPad) = UE::FName(L"Gamepad_Special_Right");
				}
			}
		}

		void RebuildMappings()
		{
			auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem");
			int n = 0;
			for (auto* sub : reflect::Instances(cls)) {
				// Both parameters set: zeroed, RebuildType is EInputMappingRebuildType::None (0) and the request does nothing -
				// until 2026-09-29 every rebuild here was a no-op, and the first Select after loading a save still opened
				// Wait beside the tween menu (the player's compiled mappings were only rebuilt when a game menu next
				// pushed a context). Options = FModifyContextOptions' default: bIgnoreAllPressedKeysUntilRelease (bit 0).
				Call c(sub, L"RequestRebuildControlMappings");
				if (c) {
					c.Set("Options", std::uint8_t{ 1 });
					c.Set("RebuildType", std::uint8_t{ 1 });   // EInputMappingRebuildType::Rebuild
					c.Run();
					++n;
				}
			}
			logger::info("input: Enhanced Input asked to rebuild its mappings ({} player subsystem{})", n, n == 1 ? "" : "s");
		}

		// Our input action, rooted at creation (MarkAsRootSet): in a shipped game Standalone does not keep an object from
		// the garbage collector, and an action made before anything referenced it was freed within seconds (the crash of
		// 2026-09-29 03:58, FName::ToString on a freed action).
		bool CreateActions()
		{
			if (g_action) {
				return true;
			}
			auto* actionClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/EnhancedInput.InputAction");
			if (!actionClass) {
				return false;
			}
			const auto flags = static_cast<UE::EObjectFlags>(static_cast<std::int32_t>(UE::EObjectFlags::Public) | static_cast<std::int32_t>(UE::EObjectFlags::Standalone) | static_cast<std::int32_t>(UE::EObjectFlags::MarkAsRootSet));
			g_action = UE::NewObject<UE::UObject>(UE::GetTransientPackage(), actionClass, UE::FName(kOurAction), flags);
			return g_action != nullptr;
		}

		// the keys an action has in IMC_Game_Default now ("None" entries - an action kept in the context unbound - left out)
		std::vector<std::string> KeysOf(UE::UObject* a_action)
		{
			std::vector<std::string> keys;
			const auto m = Mappings();
			auto* arr = a_action ? MappingArray(g_imc) : nullptr;
			for (std::int32_t i = 0; arr && m.size > 0 && m.action >= 0 && m.key >= 0 && i < arr->num; ++i) {
				std::uint8_t* e = arr->data + static_cast<std::ptrdiff_t>(i) * m.size;
				if (*reinterpret_cast<UE::UObject**>(e + m.action) != a_action) {
					continue;
				}
				const auto name = pe::Utf8(reinterpret_cast<const UE::FName*>(e + m.key)->ToString());
				if (name != "None" && std::find(keys.begin(), keys.end(), name) == keys.end()) {
					keys.push_back(name);
				}
			}
			return keys;
		}

		// The linked mapping (the owner, 2026-09-29: "wherever the wait button is bound, the tween menu follows" - "it will
		// just use the key binding for wait, not the function, so that the tween menu takes over the wait menu
		// functionality"). Every key the game gives Wait in IMC_Game_Default - from the saved Controls, after a rebind and
		// Apply on the Controls page, or when the game re-applies its saved map after a menu (seen 2026-09-29, logic library
		// 7701) - moves to our action, and our action gives up any key Wait no longer has. Wait's entries stay in the
		// context with no key, so the tween menu's own Wait option still opens it. Nothing the game saves is touched: the
		// Controls page's Wait row IS the tween menu's button. Start is put on System the same way. Returns whether the
		// context changed (Enhanced Input is then asked to rebuild).
		bool Sync()
		{
			bool changed = false;
			auto* wait = FindMappedAction(g_imc, kWaitAction);
			const auto waitKeys = KeysOf(wait);
			if (!waitKeys.empty()) {
				for (const auto& key : KeysOf(g_action)) {
					if (std::find(waitKeys.begin(), waitKeys.end(), key) == waitKeys.end() && UnmapKey(g_imc, g_action, key)) {
						logger::info("input: the tween menu is off {} (Wait is no longer bound there)", key);
						changed = true;
					}
				}
				for (const auto& key : waitKeys) {
					const bool off = UnmapKey(g_imc, wait, key);
					const bool on = IsMapped(g_imc, g_action, key) || MapKey(g_imc, g_action, key);
					++g_status.waitKeysMoved;
					logger::info("input: Wait's {} opens the tween menu now (Wait off it: {}, tween menu on it: {})", key, off ? "ok" : "FAILED", on ? "ok" : "FAILED");
					changed = true;
				}
			}
			if (settings::Get().startOpensSystem) {
				auto* system = FindMappedAction(g_imc, "IA_Game_Default_OpenOptionsMenu");
				if (system && !IsMapped(g_imc, system, "Gamepad_Special_Right")) {
					StartOpensSystem();
					changed = true;
				}
			}
			const auto keys = KeysOf(g_action);
			std::string text;
			g_keys.clear();
			for (const auto& key : keys) {
				text += (text.empty() ? "" : ", ") + key;
				g_keys.emplace_back(std::wstring(key.begin(), key.end()).c_str());
			}
			g_status.mapped = !keys.empty();
			if (text != g_status.boundKeys) {
				logger::info("input: the tween menu is bound to {} (Wait's keys on the Controls page)", text.empty() ? "nothing" : text);
				g_status.boundKeys = text;
			}
			if (changed) {
				RebuildMappings();
			}
			return changed;
		}

		void Setup()
		{
			reflect::SelfCheck();   // proves the property layout once (Improved Wheel Menu's rows::Tick did this there)
			g_imc = Find(kIMC);
			auto* padTable = Find(kPadTable);
			auto* actionClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/EnhancedInput.InputAction");
			if (!g_imc || !padTable || !actionClass || !reflect::Ok()) {
				return;   // not loaded yet: again next tick
			}
			g_done = true;
			CreateActions();
			g_status.actionCreated = g_action != nullptr;
			if (!g_action) {
				g_status.problem = "the input action could not be created";
				logger::error("input: {}", g_status.problem);
				return;
			}
			logger::info("input: {} created", NameOf(g_action));
			Sync();
		}

		UE::UObject* PlayerController()
		{
			static UE::UClass* base = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Engine.PlayerController");
			static UE::UObject* cached = nullptr;
			if (cached && reflect::IsLive(cached)) {
				return cached;
			}
			cached = nullptr;
			auto* arr = UE::FUObjectArray::GetSingleton();
			if (!arr || !base) {
				return nullptr;
			}
			arr->LockInternalArray();
			const std::int32_t n = arr->GetObjectArrayNum();
			for (std::int32_t i = 0; i < n && !cached; ++i) {
				auto* item = arr->IndexToObject(i);
				auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
				auto* cls = o ? o->GetClass() : nullptr;
				if (cls && cls->IsChildOf(base) && o != cls->GetDefaultObject(false)) {
					cached = o;
				}
			}
			arr->UnlockInternalArray();
			return cached;
		}

		bool InGameplay()
		{
			auto* ui = RE::InterfaceManager::GetInstance(false, false);
			return ui && ui->menuMode == 1;
		}
	}

	bool CreateEarly()
	{
		return CreateActions();
	}

	void Tick()
	{
		++g_tickCount;
		if (!g_action) {
			CreateActions();
		}
		if (!g_done) {
			if (g_tickCount % 30 == 0) {
				Setup();
			}
			return;
		}
		static bool s_wasInGameplay = false;
		const bool inGameplay = InGameplay();
		const bool backInGameplay = inGameplay && !s_wasInGameplay;
		s_wasInGameplay = inGameplay;
		if (!g_action || !inGameplay) {
			return;
		}
		// followed the moment a menu hands back gameplay (a menu's close can re-apply the saved bindings) and every 60 ticks
		if (backInGameplay || g_tickCount % 60 == 0) {
			Sync();
		}
		auto* pc = PlayerController();
		if (!pc) {
			return;
		}
		// edges of IsInputKeyDown (the frame tick can run more than once a frame; "just pressed" would fire twice)
		static std::vector<std::pair<std::string, bool>> s_down;
		for (const auto& key : g_keys) {
			const std::string name = pe::Utf8(key.ToString());
			const bool down = ue::KeyDown(pc, key);
			auto it = std::find_if(s_down.begin(), s_down.end(), [&](const auto& e) { return e.first == name; });
			if (it == s_down.end()) {
				s_down.emplace_back(name, down);
				continue;
			}
			const bool was = it->second;
			it->second = down;
			if (down && !was) {
				g_pressed = true;
				++g_status.presses;
				logger::info("input: tween menu key {} pressed", name);
			}
		}
	}

	bool TweenKeyPressedOnPad(WORD a_pressed)
	{
		static const std::pair<const char*, WORD> kPad[] = {
			{ "Gamepad_Special_Left", XINPUT_GAMEPAD_BACK }, { "Gamepad_Special_Right", XINPUT_GAMEPAD_START },
			{ "Gamepad_FaceButton_Bottom", XINPUT_GAMEPAD_A }, { "Gamepad_FaceButton_Right", XINPUT_GAMEPAD_B },
			{ "Gamepad_FaceButton_Left", XINPUT_GAMEPAD_X }, { "Gamepad_FaceButton_Top", XINPUT_GAMEPAD_Y },
			{ "Gamepad_LeftShoulder", XINPUT_GAMEPAD_LEFT_SHOULDER }, { "Gamepad_RightShoulder", XINPUT_GAMEPAD_RIGHT_SHOULDER },
			{ "Gamepad_LeftThumbstick", XINPUT_GAMEPAD_LEFT_THUMB }, { "Gamepad_RightThumbstick", XINPUT_GAMEPAD_RIGHT_THUMB },
			{ "Gamepad_DPad_Up", XINPUT_GAMEPAD_DPAD_UP }, { "Gamepad_DPad_Down", XINPUT_GAMEPAD_DPAD_DOWN },
			{ "Gamepad_DPad_Left", XINPUT_GAMEPAD_DPAD_LEFT }, { "Gamepad_DPad_Right", XINPUT_GAMEPAD_DPAD_RIGHT },
		};
		for (const auto& key : g_keys) {
			const std::string name = pe::Utf8(key.ToString());
			for (const auto& [n, bit] : kPad) {
				if (name == n && (a_pressed & bit)) {
					return true;
				}
			}
		}
		return false;
	}

	std::vector<LayoutEntry> Layout()
	{
		std::vector<LayoutEntry> out;
		// each label written out for translation-coverage.py (rule 66): TR("TWM_Character") TR("TWM_Quests")
		// TR("TWM_Inventory") TR("TWM_Magic") TR("TWM_Map") TR("TWM_Wait")
		for (const auto& f : kFunctions) {
			const std::string key = std::string("TWM_") + f.id;
			const std::string text = TR(key.c_str(), f.label);
			const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring label(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
			if (n > 0) {
				MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), label.data(), n);
			}
			out.push_back({ f.id, std::move(label), f.action, f.side });
		}
		return out;
	}

	bool TakePressed()
	{
		const bool p = g_pressed;
		g_pressed = false;
		return p;
	}

	Status GetStatus()
	{
		return g_status;
	}
}

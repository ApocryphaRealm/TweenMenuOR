#include "Input.h"

#include <Xinput.h>   // constants only

#include "PEHook.h"
#include "Reflect.h"
#include "Settings.h"

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

		int AddRow(const wchar_t* a_tablePath, const char* a_what)
		{
			auto* table = Find(a_tablePath);
			auto* rowStruct = Struct(L"/Script/Altar.ModernRebindSettingTableRow");
			auto* dataStruct = Struct(L"/Script/Altar.ModernRebindData");
			if (!table || !rowStruct || !dataStruct) {
				if (table) {
					logger::warn("input: the row structs are not loaded - no {} row", a_what);
				}
				return 0;
			}
			auto* arr = reflect::At<RawArray>(table, reflect::Offset(table->GetClass(), "RebindSettings"));
			const std::int32_t size = rowStruct->propertiesSize;
			const auto oLabel = reflect::Offset(rowStruct, "Label");
			const auto oType = reflect::Offset(rowStruct, "Type");
			const auto oData = reflect::Offset(rowStruct, "RebindData");
			const auto oAction = reflect::Offset(dataStruct, "InputAction");
			const auto oContext = reflect::Offset(dataStruct, "MappingContext");
			const auto oCategory = reflect::Offset(dataStruct, "DefaultCategory");
			const auto oPad = reflect::Offset(dataStruct, "DefaultPrimaryGamepadKey");
			const auto oKey1 = reflect::Offset(dataStruct, "DefaultPrimaryKeyboardKey");
			const auto oKey2 = reflect::Offset(dataStruct, "DefaultSecondaryKeyboardKey");
			if (!arr || size <= 0 || oLabel < 0 || oType < 0 || oData < 0 || oAction < 0 || oContext < 0 || oCategory < 0 || oPad < 0 || oKey1 < 0 ||
				oKey2 < 0) {
				logger::error("input: the {} table's row layout is not as read (size {}, Label {}, Type {}, RebindData {}) - no row", a_what, size,
					oLabel, oType, oData);
				return 0;
			}
			// where: before the Menu section's first row (OpenCharacter); already there: nothing to do
			std::int32_t at = arr->num;
			std::int32_t model = -1;
			for (std::int32_t i = 0; i < arr->num; ++i) {
				std::uint8_t* row = arr->data + static_cast<std::ptrdiff_t>(i) * size;
				auto* act = *reinterpret_cast<UE::UObject**>(row + oData + oAction);
				if (act == g_action) {
					return 0;
				}
				if (act && NameOf(act) == kFirstMenuAction) {
					at = i;
					model = i;
				}
				if (model < 0 && act) {
					model = i;
				}
			}
			if (model < 0) {
				logger::error("input: the {} table has no action row to model ours on", a_what);
				return 0;
			}
			std::vector<std::uint8_t> ours(static_cast<std::size_t>(size), 0);
			const std::uint8_t* like = arr->data + static_cast<std::ptrdiff_t>(model) * size;
			new (ours.data() + oLabel) UE::FText(UE::FText::AsCultureInvariant(UE::FString(L"Tween Menu")));
			std::memcpy(ours.data() + oType, like + oType, 1);   // an action row, as the model
			std::uint8_t* data = ours.data() + oData;
			*reinterpret_cast<UE::UObject**>(data + oAction) = g_action;
			*reinterpret_cast<UE::UObject**>(data + oContext) = g_imc;
			std::memcpy(data + oCategory, like + oData + oCategory, 1);
			SetKey(data + oPad, settings::Get().gamepadKey);
			SetKey(data + oKey1, settings::Get().keyboardKey);
			SetKey(data + oKey2, "None");

			// the array grows by one, ours at `at` (elements are moved bitwise - a relocation, never a copy)
			auto* grown = static_cast<std::uint8_t*>(UE::FMemory::Malloc(static_cast<std::size_t>(arr->num + 1) * size, 16));
			if (!grown) {
				return 0;
			}
			std::memcpy(grown, arr->data, static_cast<std::size_t>(at) * size);
			std::memcpy(grown + static_cast<std::ptrdiff_t>(at) * size, ours.data(), static_cast<std::size_t>(size));
			std::memcpy(grown + static_cast<std::ptrdiff_t>(at + 1) * size, arr->data + static_cast<std::ptrdiff_t>(at) * size,
				static_cast<std::size_t>(arr->num - at) * size);
			UE::FMemory::Free(arr->data);
			arr->data = grown;
			arr->num += 1;
			arr->max = arr->num;
			logger::info("input: \"Tween Menu\" row added to the {} Controls page at row {} of {}", a_what, at + 1, arr->num);
			return 1;
		}

		void RebuildMappings()
		{
			auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem");
			int n = 0;
			for (auto* sub : reflect::Instances(cls)) {
				Call c(sub, L"RequestRebuildControlMappings");
				if (c) {
					c.Run();
					++n;
				}
			}
			logger::info("input: Enhanced Input asked to rebuild its mappings ({} player subsystem{})", n, n == 1 ? "" : "s");
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
			const auto& s = settings::Get();

			g_action = UE::NewObject<UE::UObject>(UE::GetTransientPackage(), actionClass, UE::FName(kOurAction),
				static_cast<UE::EObjectFlags>(static_cast<std::int32_t>(UE::EObjectFlags::Public) | static_cast<std::int32_t>(UE::EObjectFlags::Standalone)));
			g_status.actionCreated = g_action != nullptr;
			if (!g_action) {
				g_status.problem = "the input action could not be created";
				logger::error("input: {}", g_status.problem);
				return;
			}
			logger::info("input: {} created", NameOf(g_action));

			// Wait gives up its default keys (only while it still has them); ours takes them
			if (auto* wait = FindMappedAction(g_imc, kWaitAction)) {
				for (const auto& key : { s.gamepadKey, s.keyboardKey }) {
					if (IsMapped(g_imc, wait, key)) {
						if (UnmapKey(g_imc, wait, key)) {
							++g_status.waitKeysMoved;
							logger::info("input: Wait no longer on {} (it becomes a tween menu option)", key);
						} else {
							logger::warn("input: Wait could not be taken off {}", key);
						}
					}
				}
			}
			const bool pad = MapKey(g_imc, g_action, s.gamepadKey);
			const bool kb = MapKey(g_imc, g_action, s.keyboardKey);
			g_status.mapped = pad && kb;
			logger::info("input: {} mapped in {} to {} ({}) and {} ({})", NameOf(g_action), NameOf(g_imc), s.gamepadKey, pad ? "ok" : "FAILED",
				s.keyboardKey, kb ? "ok" : "FAILED");

			g_status.rowsAdded += AddRow(kPadTable, "Controller");
			g_status.rowsAdded += AddRow(kKeyTable, "Keyboard");
			if (Find(kKeyTableFr)) {
				g_status.rowsAdded += AddRow(kKeyTableFr, "Keyboard (AZERTY)");
			}
			RebuildMappings();
		}

		// the keys bound to our action now (followed so a rebind on the Controls page takes effect)
		void RefreshKeys()
		{
			auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem");
			for (auto* sub : reflect::Instances(cls)) {
				Call c(sub, L"QueryKeysMappedToAction");
				if (!c) {
					continue;
				}
				c.Set("Action", g_action);
				c.Run();
				auto* ret = static_cast<RawArray*>(c.At("ReturnValue"));
				if (!ret) {
					continue;
				}
				std::vector<UE::FName> keys;
				std::string text;
				for (std::int32_t i = 0; i < ret->num; ++i) {
					const auto& name = *reinterpret_cast<const UE::FName*>(ret->data + static_cast<std::ptrdiff_t>(i) * sizeof(UE::FKey));
					keys.push_back(name);
					text += (text.empty() ? "" : ", ") + pe::Utf8(name.ToString());
					reinterpret_cast<UE::FKey*>(ret->data + static_cast<std::ptrdiff_t>(i) * sizeof(UE::FKey))->~FKey();
				}
				if (ret->data) {
					UE::FMemory::Free(ret->data);   // the engine's array: its keys destroyed above, then the storage
				}
				if (text != g_status.boundKeys) {
					logger::info("input: the tween menu is bound to {}", text.empty() ? "nothing" : text);
					g_status.boundKeys = text;
					// a rebind on the Controls page: keep it in our INI (the game's own save cannot restore our action)
					std::string pad, kb;
					for (const auto& k : keys) {
						const std::string n = pe::Utf8(k.ToString());
						std::string& slot = n.starts_with("Gamepad_") ? pad : kb;
						if (slot.empty()) {
							slot = n;
						}
					}
					const auto& s = settings::Get();
					if ((!pad.empty() && pad != s.gamepadKey) || (!kb.empty() && kb != s.keyboardKey)) {
						settings::SaveKeys(pad.empty() ? s.gamepadKey : pad, kb.empty() ? s.keyboardKey : kb);
					}
				}
				g_keys = std::move(keys);
				return;
			}
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

	void Tick()
	{
		++g_tickCount;
		if (!g_done) {
			if (g_tickCount % 30 == 0) {
				Setup();
			}
			return;
		}
		if (!g_action || !InGameplay()) {
			return;
		}
		if (g_keys.empty() || g_tickCount % 60 == 0) {
			RefreshKeys();
		}
		auto* pc = PlayerController();
		if (!pc) {
			return;
		}
		for (const auto& key : g_keys) {
			Call c(pc, L"WasInputKeyJustPressed");
			void* k = c ? c.At("Key") : nullptr;
			if (!k) {
				return;
			}
			new (k) UE::FKey(key);
			c.Run();
			static_cast<UE::FKey*>(k)->~FKey();   // the engine may attach its key details: released every call
			const bool* pressed = static_cast<const bool*>(c.At("ReturnValue"));
			if (pressed && *pressed) {
				g_pressed = true;
				++g_status.presses;
				logger::info("input: tween menu key {} pressed", pe::Utf8(key.ToString()));
				break;
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

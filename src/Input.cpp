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

		struct RowSpec
		{
			std::wstring label;
			bool         header = false;
			UE::UObject* action = nullptr;
			UE::UObject* context = nullptr;
			std::string  padKey = "None";
			std::string  kbKey = "None";
			int          category = -1;   // -1: as the model row
		};

		enum class Where
		{
			kBeforeMenuSection,   // before the Menu section's first row (OpenCharacter): the Tween Menu key's row
			kAfterMenuSection,    // after the Menu section's last row: the Tween Menu layout section
		};

		int InsertRows(const wchar_t* a_tablePath, const char* a_what, const std::vector<RowSpec>& a_rows, Where a_where)
		{
			auto* table = Find(a_tablePath);
			auto* rowStruct = Struct(L"/Script/Altar.ModernRebindSettingTableRow");
			auto* dataStruct = Struct(L"/Script/Altar.ModernRebindData");
			if (!table || !rowStruct || !dataStruct || a_rows.empty()) {
				if (table) {
					logger::warn("input: the row structs are not loaded - no {} rows", a_what);
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
				logger::error("input: the {} table's row layout is not as read (size {}, Label {}, Type {}, RebindData {}) - no rows", a_what, size,
					oLabel, oType, oData);
				return 0;
			}
			const auto actionOf = [&](std::int32_t i) { return *reinterpret_cast<UE::UObject**>(arr->data + static_cast<std::ptrdiff_t>(i) * size + oData + oAction); };
			const auto typeOf = [&](std::int32_t i) { return arr->data[static_cast<std::ptrdiff_t>(i) * size + oType]; };

			UE::UObject* probe = nullptr;
			for (const auto& r : a_rows) {
				if (r.action) {
					probe = r.action;
					break;
				}
			}
			std::int32_t menuFirst = -1, actionModel = -1, headerModel = -1;
			for (std::int32_t i = 0; i < arr->num; ++i) {
				auto* act = actionOf(i);
				if (probe && act == probe) {
					return 0;   // already there
				}
				if (act && menuFirst < 0 && NameOf(act) == kFirstMenuAction) {
					menuFirst = i;
				}
				if (act && actionModel < 0) {
					actionModel = i;
				}
				if (!act && headerModel < 0) {
					headerModel = i;
				}
			}
			if (actionModel < 0 || headerModel < 0) {
				logger::error("input: the {} table has no rows to model ours on", a_what);
				return 0;
			}
			std::int32_t at = arr->num;
			if (menuFirst >= 0) {
				if (a_where == Where::kBeforeMenuSection) {
					at = menuFirst;
				} else {
					at = menuFirst;
					while (at < arr->num && actionOf(at)) {
						++at;   // to the next header (or the end): after the Menu section's last row
					}
				}
			}
			const std::int32_t n = static_cast<std::int32_t>(a_rows.size());
			std::vector<std::uint8_t> ours(static_cast<std::size_t>(size) * n, 0);
			for (std::int32_t k = 0; k < n; ++k) {
				const auto& spec = a_rows[k];
				std::uint8_t* row = ours.data() + static_cast<std::ptrdiff_t>(k) * size;
				const std::uint8_t* like = arr->data + static_cast<std::ptrdiff_t>(spec.header ? headerModel : actionModel) * size;
				new (row + oLabel) UE::FText(UE::FText::AsCultureInvariant(UE::FString(spec.label.c_str())));
				row[oType] = like[oType];
				std::uint8_t* data = row + oData;
				if (spec.category >= 0) {
					data[oCategory] = static_cast<std::uint8_t>(spec.category);
				} else {
					std::memcpy(data + oCategory, like + oData + oCategory, 1);
				}
				*reinterpret_cast<UE::UObject**>(data + oAction) = spec.action;
				*reinterpret_cast<UE::UObject**>(data + oContext) = spec.context;
				SetKey(data + oPad, spec.padKey);
				SetKey(data + oKey1, spec.kbKey);
				SetKey(data + oKey2, "None");
			}
			// the array grows by n, ours at `at` (elements are moved bitwise - a relocation, never a copy)
			auto* grown = static_cast<std::uint8_t*>(UE::FMemory::Malloc(static_cast<std::size_t>(arr->num + n) * size, 16));
			if (!grown) {
				return 0;
			}
			std::memcpy(grown, arr->data, static_cast<std::size_t>(at) * size);
			std::memcpy(grown + static_cast<std::ptrdiff_t>(at) * size, ours.data(), static_cast<std::size_t>(n) * size);
			std::memcpy(grown + static_cast<std::ptrdiff_t>(at + n) * size, arr->data + static_cast<std::ptrdiff_t>(at) * size,
				static_cast<std::size_t>(arr->num - at) * size);
			UE::FMemory::Free(arr->data);
			arr->data = grown;
			arr->num += n;
			arr->max = arr->num;
			logger::info("input: {} row{} added to the {} Controls page at row {} of {}", n, n == 1 ? "" : "s", a_what, at + 1, arr->num);
			return n;
		}

		// ---- the tween layout (M6): a row per function, the direction bound to it = its side ----

		struct Function
		{
			const char*    id;
			const wchar_t* label;
			const wchar_t* action;   // the game's own input action that opens it
			int            side;     // default: 0 up, 1 right, 2 down, 3 left, -1 not in the menu
		};

		const std::array<Function, 10> kFunctions{ {
			{ "Character", L"Character", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenStatsMenu.IA_Game_Default_OpenStatsMenu", 0 },
			{ "Quests", L"Quests", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenQuestMenu.IA_Game_Default_OpenQuestMenu", 0 },
			{ "Inventory", L"Inventory", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenInventoryMenu.IA_Game_Default_OpenInventoryMenu", 1 },
			{ "Magic", L"Magic", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenMagicMenu.IA_Game_Default_OpenMagicMenu", 1 },
			{ "Map", L"Map", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenMapMenu.IA_Game_Default_OpenMapMenu", 2 },
			{ "Wait", L"Wait", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenRestMenu.IA_Game_Default_OpenRestMenu", 2 },
			{ "System", L"System", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenOptionsMenu.IA_Game_Default_OpenOptionsMenu", 3 },
			{ "Help", L"Help", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_OpenHelpMenu.IA_Game_Default_OpenHelpMenu", 3 },
			{ "QuickSave", L"Quick Save", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_QuickSave.IA_Game_Default_QuickSave", -1 },
			{ "QuickLoad", L"Quick Load", L"/Game/Dev/Input/GamePlay/InputActions/Default/IA_Game_Default_QuickLoad.IA_Game_Default_QuickLoad", -1 },
		} };
		constexpr std::array<const char*, 4> kPadSide{ "Gamepad_DPad_Up", "Gamepad_DPad_Right", "Gamepad_DPad_Down", "Gamepad_DPad_Left" };
		constexpr std::array<const char*, 4> kKeySide{ "Up", "Right", "Down", "Left" };

		UE::UObject* g_layoutImc = nullptr;   // our own mapping context - never applied to the player
		std::array<UE::UObject*, kFunctions.size()> g_slotActions{};

		std::vector<std::string> KeysOf(UE::UObject* a_imc, UE::UObject* a_action)
		{
			std::vector<std::string> out;
			const auto m = Mappings();
			auto* arr = a_imc ? MappingArray(a_imc) : nullptr;
			for (std::int32_t i = 0; arr && m.size > 0 && i < arr->num; ++i) {
				std::uint8_t* e = arr->data + static_cast<std::ptrdiff_t>(i) * m.size;
				if (*reinterpret_cast<UE::UObject**>(e + m.action) == a_action) {
					out.push_back(pe::Utf8(reinterpret_cast<const UE::FName*>(e + m.key)->ToString()));
				}
			}
			return out;
		}

		// The Controls page shows a row's keys from the player's ACTIVE mapping contexts (found 2026-09-29: the layout
		// rows showed empty, red-bordered, while their context was not applied). So the layout context is applied -
		// at the lowest priority, its actions consuming nothing and bound to nothing, so it never changes play.
		void ApplyLayoutContext()
		{
			auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem");
			for (auto* sub : reflect::Instances(cls)) {
				Call has(sub, L"HasMappingContext");
				has.Set("MappingContext", g_layoutImc);
				has.Run();
				const auto* present = static_cast<const bool*>(has.At("ReturnValue"));
				if (!present || *present) {
					continue;
				}
				Call add(sub, L"AddMappingContext");
				add.Set("MappingContext", g_layoutImc);
				add.Set<std::int32_t>("Priority", -1000);
				add.Run();
				logger::info("input: the tween layout context applied (lowest priority - it only lets the Controls page show its keys)");
			}
		}

		void SetupLayout(UE::UClass* a_actionClass)
		{
			auto* imcClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/EnhancedInput.InputMappingContext");
			const auto flags = static_cast<UE::EObjectFlags>(static_cast<std::int32_t>(UE::EObjectFlags::Public) | static_cast<std::int32_t>(UE::EObjectFlags::Standalone));
			g_layoutImc = imcClass ? UE::NewObject<UE::UObject>(UE::GetTransientPackage(), imcClass, UE::FName(L"IMC_TweenMenu_Layout"), flags) : nullptr;
			if (!g_layoutImc) {
				logger::error("input: the tween layout's mapping context could not be created - no layout rows");
				return;
			}
			std::vector<RowSpec> pad{ { L"Tween Menu Layout", true } }, kb{ { L"Tween Menu Layout", true } };
			for (std::size_t i = 0; i < kFunctions.size(); ++i) {
				const auto& f = kFunctions[i];
				const std::string name = std::string("IA_TweenMenu_Slot_") + f.id;
				g_slotActions[i] = UE::NewObject<UE::UObject>(UE::GetTransientPackage(), a_actionClass,
					UE::FName(std::wstring(name.begin(), name.end()).c_str()), flags);
				if (!g_slotActions[i]) {
					continue;
				}
				const std::string padDefault = f.side >= 0 ? kPadSide[f.side] : "None";
				const std::string kbDefault = f.side >= 0 ? kKeySide[f.side] : "None";
				const std::string padKey = settings::ReadIni("TweenLayoutPad", f.id, padDefault);
				const std::string kbKey = settings::ReadIni("TweenLayoutKeyboard", f.id, kbDefault);
				// never takes a key from anything else (the context is also applied at the lowest priority)
				if (const auto c = reflect::Offset(a_actionClass, "bConsumeInput"); c >= 0) {
					*reflect::At<bool>(g_slotActions[i], c) = false;
				}
				if (padKey != "None") {
					MapKey(g_layoutImc, g_slotActions[i], padKey);
				}
				if (kbKey != "None") {
					MapKey(g_layoutImc, g_slotActions[i], kbKey);
				}
				const std::wstring label = std::wstring(L"Tween: ") + f.label;
				// Its own rebind category per row: the Controls page refuses a key already used IN THE SAME CATEGORY
				// (the Keyboard page's UI rows, category 1, share Q and E with gameplay, category 0). A category of its
				// own lets any number of functions share one direction, and never touches a real binding - the owner,
				// 2026-09-29: "these are separate D-pad functions ... we're just using them as a way to distinguish
				// which direction of the menu it goes on".
				const int category = 10 + static_cast<int>(i);
				pad.push_back({ label, false, g_slotActions[i], g_layoutImc, padDefault, "None", category });
				kb.push_back({ label, false, g_slotActions[i], g_layoutImc, "None", kbDefault, category });
			}
			g_status.rowsAdded += InsertRows(kPadTable, "Controller", pad, Where::kAfterMenuSection);
			g_status.rowsAdded += InsertRows(kKeyTable, "Keyboard", kb, Where::kAfterMenuSection);
			if (Find(kKeyTableFr)) {
				g_status.rowsAdded += InsertRows(kKeyTableFr, "Keyboard (AZERTY)", kb, Where::kAfterMenuSection);
			}
		}

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
			if (s.startOpensSystem) {
				StartOpensSystem();
			}
			const bool pad = MapKey(g_imc, g_action, s.gamepadKey);
			const bool kb = MapKey(g_imc, g_action, s.keyboardKey);
			g_status.mapped = pad && kb;
			logger::info("input: {} mapped in {} to {} ({}) and {} ({})", NameOf(g_action), NameOf(g_imc), s.gamepadKey, pad ? "ok" : "FAILED",
				s.keyboardKey, kb ? "ok" : "FAILED");

			const std::vector<RowSpec> padRow{ { L"Tween Menu", false, g_action, g_imc, s.gamepadKey, "None" } };
			const std::vector<RowSpec> kbRow{ { L"Tween Menu", false, g_action, g_imc, "None", s.keyboardKey } };
			g_status.rowsAdded += InsertRows(kPadTable, "Controller", padRow, Where::kBeforeMenuSection);
			g_status.rowsAdded += InsertRows(kKeyTable, "Keyboard", kbRow, Where::kBeforeMenuSection);
			if (Find(kKeyTableFr)) {
				g_status.rowsAdded += InsertRows(kKeyTableFr, "Keyboard (AZERTY)", kbRow, Where::kBeforeMenuSection);
			}
			SetupLayout(actionClass);
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
		if (g_layoutImc && g_tickCount % 120 == 0) {
			ApplyLayoutContext();   // again after a load or anything that clears the player's contexts
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

	std::vector<LayoutEntry> Layout()
	{
		std::vector<LayoutEntry> out;
		for (std::size_t i = 0; i < kFunctions.size(); ++i) {
			const auto& f = kFunctions[i];
			if (!g_slotActions[i]) {
				continue;
			}
			std::string pad = "None", kb = "None";
			for (const auto& k : KeysOf(g_layoutImc, g_slotActions[i])) {
				std::string& slot = k.starts_with("Gamepad_") ? pad : kb;
				if (slot == "None") {
					slot = k;
				}
			}
			// kept across restarts (the game cannot restore a runtime action's rebind)
			if (settings::ReadIni("TweenLayoutPad", f.id, "?") != pad) {
				settings::WriteIni("TweenLayoutPad", f.id, pad);
			}
			if (settings::ReadIni("TweenLayoutKeyboard", f.id, "?") != kb) {
				settings::WriteIni("TweenLayoutKeyboard", f.id, kb);
			}
			int side = -1;
			for (int d = 0; d < 4 && side < 0; ++d) {
				if (pad == kPadSide[d]) {
					side = d;
				}
			}
			for (int d = 0; d < 4 && side < 0; ++d) {
				if (pad == "None" && kb == kKeySide[d]) {
					side = d;   // no controller binding: the keyboard row decides
				}
			}
			out.push_back({ f.id, f.label, f.action, side });
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

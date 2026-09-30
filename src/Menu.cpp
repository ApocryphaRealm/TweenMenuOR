#include "Menu.h"

#include "Input.h"
#include "Strings.h"
#include "Ue.h"

namespace menu
{
	namespace
	{
		constexpr const wchar_t* kRowClass = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/WBP_Modern_Settings_GamepadRebindWidget.WBP_Modern_Settings_GamepadRebindWidget_C";
		constexpr const wchar_t* kBoxTemplate = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/WBP_Modern_Settings_GamepadRebindWidget.WBP_Modern_Settings_GamepadRebindWidget_C:WidgetTree.RebindBackground";
		constexpr const wchar_t* kFocusTemplate = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/WBP_Modern_Settings_GamepadRebindWidget.WBP_Modern_Settings_GamepadRebindWidget_C:WidgetTree.FocusBackground";
		constexpr const wchar_t* kLabelTemplate = L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/WBP_Modern_Settings_GamepadRebindWidget.WBP_Modern_Settings_GamepadRebindWidget_C:WidgetTree.RebindLabel";
		constexpr const wchar_t* kTileClass = L"/Game/UI/Original/Prefabs/WBP_OriginalImageTile.WBP_OriginalImageTile_C";
		constexpr const wchar_t* kTextClass = L"/Game/UI/Modern/Prefabs/WBP_AltarTextBlock.WBP_AltarTextBlock_C";

		constexpr double kBoxW = 360.0, kBoxH = 52.0, kStep = 60.0, kReach = 96.0, kSide = 400.0;

		enum Dir { kUp, kRight, kDown, kLeft, kDirs };

		// One box per function (all of them, built once); each open places them from the Controls page's "Tween Menu
		// Layout" rows (M6, the owner 2026-09-29: the D-pad direction bound to a function's row is its side).
		struct Entry
		{
			std::string    id;
			std::wstring   text;
			const wchar_t* action = nullptr;   // the game's own input action that opens it
			UE::UObject*   box = nullptr;
			UE::UObject*   label = nullptr;
			UE::UObject*   boxSlot = nullptr;
			UE::UObject*   labelSlot = nullptr;
		};

		UE::UObject* g_root = nullptr;   // the UserWidget on the viewport
		std::vector<Entry> g_all;                          // every function's box
		std::array<std::vector<Entry*>, kDirs> g_entries;  // this open's layout: the boxes on each side, in order
		bool  g_open = false;
		int   g_cat = -1, g_idx = 0;
		WORD  g_prevButtons = 0;
		int   g_stickDir = -1;            // the left stick's direction last read (-1 = centred)
		bool  g_drain = false;            // after closing: swallow the pad until every button is let go
		DWORD g_packetBump = 0;
		const wchar_t* g_pendingAction = nullptr;
		int   g_pendingDelay = 0;
		std::string g_problem;

		struct Colors
		{
			std::array<float, 4> focus{ 0.072f, 0.038f, 0.028f, 1.0f };     // the Controls row's FocusColor (dark text on the lit box)
			std::array<float, 4> unfocus{ 1.0f, 0.939f, 0.847f, 1.0f };     // UnfocusColor (light text on the brown box)
		} g_colors;

		UE::UObject* PlayerController()
		{
			static UE::UObject* cached = nullptr;
			if (!cached || !reflect::IsLive(cached)) {
				cached = ue::FirstOf(ue::Class(L"/Script/Engine.PlayerController"));
			}
			return cached;
		}

		// the first parameter of a function, by position (Blueprint functions name theirs freely)
		bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const void* a_bytes, std::size_t a_size)
		{
			auto* fn = a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr;
			if (!fn) {
				return false;
			}
			auto* st = reinterpret_cast<UE::UStruct*>(fn);
			const auto fields = reflect::Fields(st);
			if (fields.empty()) {
				return false;
			}
			std::vector<std::uint8_t> params(static_cast<std::size_t>(st->propertiesSize), 0);
			std::memcpy(params.data() + fields.front().second, a_bytes, std::min<std::size_t>(a_size, params.size() - fields.front().second));
			a_obj->ProcessEvent(fn, params.data());
			return true;
		}

		UE::UObject* Create(const wchar_t* a_classPath)
		{
			static auto* lib = ue::Class(L"/Script/UMG.WidgetBlueprintLibrary");
			auto* cls = ue::Class(a_classPath);
			auto* pc = PlayerController();
			if (!lib || !cls || !pc || ue::Dying(pc)) {
				return nullptr;
			}
			ue::Call c(lib->GetDefaultObject(false), L"Create");
			c.Set("WorldContextObject", pc);
			c.Set("WidgetType", cls);
			c.Set("OwningPlayer", pc);
			if (!c.RunGuarded()) {
				return nullptr;
			}
			return c.Get<UE::UObject*>("ReturnValue");
		}

		// copies a reflected property's bytes from a template object (brush, font - no reference-counted members)
		void CopyProperty(UE::UObject* a_to, UE::UObject* a_from, std::string_view a_name)
		{
			const auto to = reflect::Offset(a_to->GetClass(), a_name);
			const auto from = reflect::Offset(a_from->GetClass(), a_name);
			const auto size = reflect::Size(a_from->GetClass(), a_name);
			if (to >= 0 && from >= 0 && size > 0 && size == reflect::Size(a_to->GetClass(), a_name)) {
				std::memcpy(reinterpret_cast<std::uint8_t*>(a_to) + to, reinterpret_cast<std::uint8_t*>(a_from) + from, static_cast<std::size_t>(size));
			}
		}

		void SetBoxLit(Entry& a_e, bool a_lit)
		{
			static auto* box = ue::Find(kBoxTemplate);
			static auto* focus = ue::Find(kFocusTemplate);
			auto* from = a_lit ? focus : box;
			const auto off = from ? reflect::Offset(from->GetClass(), "Brush") : -1;
			const auto size = from ? reflect::Size(from->GetClass(), "Brush") : -1;
			if (off >= 0 && size > 0) {
				CallFirst(a_e.box, L"SetBrush", reinterpret_cast<std::uint8_t*>(from) + off, static_cast<std::size_t>(size));   // FSlateBrush
			}
			// the label's colour: a slate colour (linear colour + "use specified") - the BP's SetColor takes it
			struct { std::array<float, 4> c; std::uint8_t rule; std::uint8_t pad[7]; } color{ a_lit ? g_colors.focus : g_colors.unfocus, 0, {} };
			CallFirst(a_e.label, L"SetColor", &color, sizeof(color));
		}

		void Refresh()
		{
			for (int d = 0; d < kDirs; ++d) {
				for (int i = 0; i < static_cast<int>(g_entries[d].size()); ++i) {
					SetBoxLit(*g_entries[d][i], d == g_cat && i == g_idx);
				}
			}
		}

		// where option i of direction d stands, from the screen's centre (Slate units; alignment centre)
		std::pair<double, double> Place(int a_d, int a_i, int a_n)
		{
			switch (a_d) {
			case kUp: return { 0.0, -(kReach + a_i * kStep) };
			case kDown: return { 0.0, kReach + a_i * kStep };
			case kLeft: return { -kSide, (a_i - (a_n - 1) / 2.0) * kStep };
			default: return { kSide, (a_i - (a_n - 1) / 2.0) * kStep };
			}
		}

		UE::UObject* AddToCanvas(UE::UObject* a_canvas, UE::UObject* a_child, double a_x, double a_y, double a_w, double a_h, bool a_autoSize)
		{
			ue::Call add(a_canvas, L"AddChildToCanvas");
			add.Set("Content", a_child);
			add.Run();
			auto* slot = add.Get<UE::UObject*>("ReturnValue");
			if (!slot) {
				return nullptr;
			}
			const double anchors[4] = { 0.5, 0.5, 0.5, 0.5 };
			CallFirst(slot, L"SetAnchors", anchors, sizeof(anchors));
			const double align[2] = { 0.5, 0.5 };
			CallFirst(slot, L"SetAlignment", align, sizeof(align));
			const double pos[2] = { a_x, a_y };
			CallFirst(slot, L"SetPosition", pos, sizeof(pos));
			if (a_autoSize) {
				const bool yes = true;
				CallFirst(slot, L"SetAutoSize", &yes, sizeof(yes));
			} else {
				const double size[2] = { a_w, a_h };
				CallFirst(slot, L"SetSize", size, sizeof(size));
			}
			return slot;
		}

		// Loads a Blueprint class by its path when nothing has loaded it yet (the Controls page's row widget is loaded
		// only once that page has been opened): KismetSystemLibrary::LoadClassAsset_Blocking with a soft class path
		// (FSoftObjectPtr: weak pointer 8 bytes, then FSoftObjectPath = package FName, asset FName, sub-path FString).
		UE::UClass* LoadClass(const wchar_t* a_package, const wchar_t* a_asset)
		{
			static auto* lib = ue::Class(L"/Script/Engine.KismetSystemLibrary");
			ue::Call c(lib ? lib->GetDefaultObject(false) : nullptr, L"LoadClassAsset_Blocking");
			auto* soft = static_cast<std::uint8_t*>(c ? c.At("AssetClass") : nullptr);
			if (!soft) {
				return nullptr;
			}
			new (soft + 0x08) UE::FName(a_package);
			new (soft + 0x10) UE::FName(a_asset);
			c.Run();
			auto* cls = c.Get<UE::UClass*>("ReturnValue");
			logger::info("menu: {} loaded by path ({})", pe::Utf8(UE::FString(a_asset)), cls ? "ok" : "FAILED");
			return cls;
		}

		bool Build()
		{
			auto* rowClass = ue::Class(kRowClass);
			if (!rowClass) {
				rowClass = LoadClass(L"/Game/UI/Modern/MenuLayer/Settings/Rebind/Gamepad/WBP_Modern_Settings_GamepadRebindWidget",
					L"WBP_Modern_Settings_GamepadRebindWidget_C");
			}
			auto* labelTemplate = ue::Find(kLabelTemplate);
			if (!rowClass || !labelTemplate || !ue::Find(kBoxTemplate) || !ue::Find(kFocusTemplate)) {
				g_problem = "the Controls page's row widget is not loaded yet (open System > Controller once)";
				return false;
			}
			// the row's own text colours
			if (auto* cdo = rowClass->GetDefaultObject(false)) {
				if (const auto f = reflect::Offset(rowClass, "FocusColor"); f >= 0) {
					std::memcpy(g_colors.focus.data(), reinterpret_cast<std::uint8_t*>(cdo) + f, 16);
				}
				if (const auto u = reflect::Offset(rowClass, "UnfocusColor"); u >= 0) {
					std::memcpy(g_colors.unfocus.data(), reinterpret_cast<std::uint8_t*>(cdo) + u, 16);
				}
			}
			g_root = Create(L"/Script/UMG.UserWidget");
			auto* treeClass = ue::Class(L"/Script/UMG.WidgetTree");
			auto* canvasClass = ue::Class(L"/Script/UMG.CanvasPanel");
			if (!g_root || !treeClass || !canvasClass) {
				g_problem = "the menu's root widget could not be created";
				return false;
			}
			auto** tree = reflect::At<UE::UObject*>(g_root, reflect::Offset(g_root->GetClass(), "WidgetTree"));
			if (tree && !*tree) {
				*tree = UE::NewObject<UE::UObject>(g_root, treeClass, UE::FName(L"TweenMenuTree"));
			}
			if (!tree || !*tree) {
				g_problem = "the menu has no widget tree";
				return false;
			}
			auto* canvas = UE::NewObject<UE::UObject>(*tree, canvasClass, UE::FName(L"TweenMenuCanvas"));
			auto** rootWidget = reflect::At<UE::UObject*>(*tree, reflect::Offset(treeClass, "RootWidget"));
			if (!canvas || !rootWidget) {
				g_problem = "the menu's canvas could not be made";
				return false;
			}
			*rootWidget = canvas;

			int made = 0;
			for (const auto& f : input::Layout()) {
				Entry e;
				e.id = f.id;
				e.text = f.label;
				e.action = f.action;
				e.box = Create(kTileClass);
				e.label = Create(kTextClass);
				if (!e.box || !e.label) {
					continue;
				}
				// the label: the Controls row label's own style, then the function's name
				CopyProperty(e.label, labelTemplate, "FontInfo");
				CopyProperty(e.label, labelTemplate, "Justification");
				CopyProperty(e.label, labelTemplate, "FontSizeChannel");
				if (const auto t = reflect::Offset(e.label->GetClass(), "Text"); t >= 0) {
					auto* text = reflect::At<UE::FText>(e.label, t);
					text->~FText();   // FText cannot be assigned: the default text goes, ours is made in its place
					new (text) UE::FText(UE::FText::AsCultureInvariant(UE::FString(e.text.c_str())));
				}
				e.boxSlot = AddToCanvas(canvas, e.box, 0, 0, kBoxW, kBoxH, false);
				e.labelSlot = AddToCanvas(canvas, e.label, 0, 0, 0, 0, true);
				g_all.push_back(e);
				++made;
			}
			logger::info("menu: built from the game's widgets - {} option boxes on a canvas, no background", made);
			return made > 0;
		}

		// The menu is built once and stays on the viewport, shown and hidden: taken off it, nothing holds the widgets
		// and the garbage collector frees them - the crash of 2026-09-29 03:19 (Toggle on a freed root). A level change
		// can still take viewport widgets away, so each open checks they are alive and on the viewport.
		bool Alive()
		{
			if (!g_root || !reflect::IsLive(g_root)) {
				return false;
			}
			for (const auto& e : g_all) {
				if (!reflect::IsLive(e.box) || !reflect::IsLive(e.label)) {
					return false;
				}
			}
			return !g_all.empty();
		}

		void Show(UE::UObject* a_widget, bool a_visible)
		{
			ue::Call c(a_widget, L"SetVisibility");
			c.Set("InVisibility", static_cast<std::uint8_t>(a_visible ? 3 : 1));
			c.Run();
		}

		void Arrange()
		{
			for (auto& side : g_entries) {
				side.clear();
			}
			const auto layout = input::Layout();
			std::string summary;
			for (auto& e : g_all) {
				int side = -1;
				for (const auto& f : layout) {
					if (f.id == e.id) {
						side = f.side;
					}
				}
				if (side >= 0) {
					g_entries[side].push_back(&e);
				}
				Show(e.box, side >= 0);
				Show(e.label, side >= 0);
			}
			static constexpr const char* kSideName[kDirs] = { "up", "right", "down", "left" };
			for (int d = 0; d < kDirs; ++d) {
				const int n = static_cast<int>(g_entries[d].size());
				for (int i = 0; i < n; ++i) {
					const auto [x, y] = Place(d, i, n);
					const double pos[2] = { x, y };
					CallFirst(g_entries[d][i]->boxSlot, L"SetPosition", pos, sizeof(pos));
					CallFirst(g_entries[d][i]->labelSlot, L"SetPosition", pos, sizeof(pos));
					summary += std::format("{}{} {}", summary.empty() ? "" : ", ", g_entries[d][i]->id, kSideName[d]);
				}
			}
			logger::info("menu: laid out from the Controls rows - {}", summary.empty() ? "nothing bound to a direction" : summary);
		}

		void SetVisible(bool a_visible)
		{
			const std::uint8_t vis = a_visible ? 3 : 1;   // ESlateVisibility: HitTestInvisible (drawn, never takes the mouse) / Collapsed
			ue::Call c(g_root, L"SetVisibility");
			c.Set("InVisibility", vis);
			c.Run();
		}

		std::string g_builtLanguage;

		void Open()
		{
			strings::Refresh();
			if (g_root && Alive() && strings::Language() != g_builtLanguage) {
				logger::info("menu: the game's language changed ({} -> {}) - building it again", g_builtLanguage, strings::Language());
				ue::Call rm(g_root, L"RemoveFromParent");
				rm.Run();
				g_root = nullptr;
				g_all.clear();
				for (auto& dir : g_entries) {
					dir.clear();
				}
			}
			if (g_root && !Alive()) {
				logger::info("menu: its widgets are gone (a level change?) - building it again");
				g_root = nullptr;
				g_all.clear();
				for (auto& dir : g_entries) {
					dir.clear();
				}
			}
			if (!g_root) {
				if (!Build()) {
					logger::warn("menu: cannot open - {}", g_problem);
					return;
				}
				g_builtLanguage = strings::Language();
			}
			ue::Call in(g_root, L"IsInViewport");
			in.Run();
			if (!in.Get<bool>("ReturnValue")) {
				ue::Call add(g_root, L"AddToViewport");
				add.Set<std::int32_t>("ZOrder", 50);
				add.Run();
			}
			Arrange();
			SetVisible(true);
			g_open = true;
			g_cat = -1;
			g_idx = 0;
			Refresh();
			logger::info("menu: open");
		}

		void Close(const wchar_t* a_then)
		{
			if (Alive()) {
				SetVisible(false);
			}
			g_open = false;
			g_drain = true;
			g_pendingAction = a_then;
			g_pendingDelay = 2;   // a couple of ticks: the menu is gone before the game's own menu opens
			logger::info("menu: closed{}", a_then ? " - opening the chosen option" : "");
		}

		void Fire(const wchar_t* a_actionPath)
		{
			auto* action = ue::Find(a_actionPath);
			auto* cls = ue::Class(L"/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem");
			auto* sub = ue::FirstOf(cls);
			if (!action || !sub) {
				logger::warn("menu: the option's action {} could not be fired", action ? ue::NameOf(action) : "(not found)");
				return;
			}
			ue::Call c(sub, L"InjectInputForAction");
			c.Set("Action", action);
			if (void* raw = c.At("RawValue")) {
				const double one = 1.0;   // FInputActionValue: FVector value (doubles), then its value type (0 = bool)
				std::memcpy(raw, &one, sizeof(one));
			}
			c.Run();
			logger::info("menu: {} fired", ue::NameOf(action));
		}

		void Step(int a_dir)
		{
			const auto has = [](int d) { return !g_entries[d].empty(); };
			if (g_cat < 0) {
				if (has(a_dir)) {
					g_cat = a_dir;
					g_idx = 0;
				}
			} else if (a_dir == kLeft || a_dir == kRight) {
				if (g_cat == kLeft || g_cat == kRight) {
					if (a_dir != g_cat && has(a_dir)) {
						g_cat = a_dir;
						g_idx = 0;
					}
				} else if (has(a_dir)) {
					g_cat = a_dir;
					g_idx = 0;
				}
			} else if (g_cat == kLeft || g_cat == kRight) {
				// in a side column Up/Down step through it; past its first box Up goes to the top set, past its last
				// Down goes to the bottom set (the owner, 2026-09-29: from the sides the top and bottom were unreachable)
				const int n = static_cast<int>(g_entries[g_cat].size());
				const int next = g_idx + (a_dir == kDown ? 1 : -1);
				if (next < 0 && has(kUp)) {
					g_cat = kUp;
					g_idx = 0;
				} else if (next >= n && has(kDown)) {
					g_cat = kDown;
					g_idx = 0;
				} else {
					g_idx = std::clamp(next, 0, n - 1);
				}
			} else {
				// in the Up or Down column: along it steps outward, against it steps back (past the first: none)
				const int n = static_cast<int>(g_entries[g_cat].size());
				if (a_dir == g_cat) {
					g_idx = std::min(g_idx + 1, n - 1);
				} else if (g_idx > 0) {
					--g_idx;
				} else {
					g_cat = -1;
				}
			}
			Refresh();
		}
	}

	void Toggle()
	{
		if (g_open) {
			Close(nullptr);
		} else {
			Open();
		}
	}

	bool IsOpen()
	{
		return g_open;
	}

	// open, or just closed: the pad still drained, or a chosen option still waiting to open (TweenMenu_IsOpen)
	bool Busy()
	{
		return g_open || g_drain || g_pendingAction != nullptr;
	}

	void OnPad(XINPUT_STATE* a_state)
	{
		if (!a_state) {
			return;
		}
		auto& pad = a_state->Gamepad;
		const WORD raw = pad.wButtons;
		const WORD pressed = raw & ~g_prevButtons;
		g_prevButtons = raw;
		// the left stick as a D-pad: past half-way in one direction is one press; back to the centre before the next
		int stick = -1;
		const int lx = pad.sThumbLX, ly = pad.sThumbLY;
		constexpr int kPush = 16000, kRest = 8000;
		if (std::abs(lx) > kPush || std::abs(ly) > kPush) {
			stick = std::abs(lx) > std::abs(ly) ? (lx > 0 ? kRight : kLeft) : (ly > 0 ? kUp : kDown);
		} else if (std::abs(lx) > kRest || std::abs(ly) > kRest) {
			stick = g_stickDir;   // between the two: no change (no chatter at the edge)
		}
		const bool stickPressed = stick >= 0 && stick != g_stickDir;
		g_stickDir = stick;
		if (g_open) {
			if (stickPressed) { Step(stick); }
			if (pressed & XINPUT_GAMEPAD_DPAD_UP) { Step(kUp); }
			if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) { Step(kDown); }
			if (pressed & XINPUT_GAMEPAD_DPAD_LEFT) { Step(kLeft); }
			if (pressed & XINPUT_GAMEPAD_DPAD_RIGHT) { Step(kRight); }
			if (pressed & XINPUT_GAMEPAD_A) {
				if (g_cat >= 0 && g_idx < static_cast<int>(g_entries[g_cat].size())) {
					logger::info("menu: {} chosen", g_entries[g_cat][g_idx]->id);
					Close(g_entries[g_cat][g_idx]->action);
				}
			} else if (pressed & XINPUT_GAMEPAD_B) {
				Close(nullptr);
			} else if (input::TweenKeyPressedOnPad(pressed)) {
				Close(nullptr);
			}
		}
		if (g_open || g_drain) {
			if (!g_open && raw == 0 && pad.bLeftTrigger < 30 && pad.bRightTrigger < 30) {
				g_drain = false;   // everything let go: the game has the pad again
			}
			std::memset(&pad, 0, sizeof(pad));
			a_state->dwPacketNumber += ++g_packetBump;
		}
	}

	// The keyboard while the menu is open: the arrow keys, Enter and Backspace never reach the player controller (the
	// game's UI layer takes them as navigation - found 2026-09-29: IsInputKeyDown saw T but no arrow), so they are read
	// straight from Windows, and only while the game's window is the one in front.
	void Keys()
	{
		static const std::pair<int, int> kKeys[] = {
			{ VK_UP, kUp }, { VK_RIGHT, kRight }, { VK_DOWN, kDown }, { VK_LEFT, kLeft }, { VK_RETURN, 10 }, { VK_BACK, 11 },
		};
		static bool s_was[std::size(kKeys)]{};
		DWORD pid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &pid);
		const bool front = pid == GetCurrentProcessId();
		for (std::size_t i = 0; i < std::size(kKeys); ++i) {
			const bool down = front && (GetAsyncKeyState(kKeys[i].first) & 0x8000) != 0;
			const bool pressed = down && !s_was[i];
			s_was[i] = down;
			if (!pressed || !g_open) {
				continue;
			}
			const int what = kKeys[i].second;
			if (what < kDirs) {
				Step(what);
			} else if (what == 10) {
				if (g_cat >= 0 && g_idx < static_cast<int>(g_entries[g_cat].size())) {
					logger::info("menu: {} chosen (keyboard)", g_entries[g_cat][g_idx]->id);
					Close(g_entries[g_cat][g_idx]->action);
				}
			} else {
				Close(nullptr);
			}
		}
	}

	void Tick()
	{
		if (g_pendingAction && --g_pendingDelay <= 0) {
			Fire(g_pendingAction);
			g_pendingAction = nullptr;
		}
	}

	std::string Status()
	{
		return std::format("{}; {}", g_open ? "open" : "closed", g_problem.empty() ? "ok" : g_problem);
	}
}

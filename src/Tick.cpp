#include "Tick.h"

namespace tick
{
	namespace
	{
		using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
		using PeekMessageW_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);

		XInputGetState_t           g_prevXInput = nullptr;
		PeekMessageW_t             g_prevPeek = nullptr;
		std::atomic<FrameCallback> g_frame{ nullptr };
		std::atomic<PadCallback>   g_pad{ nullptr };
		DWORD                      g_gameThread = 0;
		std::atomic<std::uint64_t> g_reads{ 0 };
		ULONGLONG                  g_lastFrame = 0;
		bool                       g_inFrame = false;

		DWORD WINAPI ChainedXInput(DWORD a_user, XINPUT_STATE* a_state)
		{
			const DWORD rc = g_prevXInput ? g_prevXInput(a_user, a_state) : ERROR_DEVICE_NOT_CONNECTED;
			if (a_user == 0 && GetCurrentThreadId() == g_gameThread) {
				if (auto cb = g_pad.load(std::memory_order_acquire)) {
					cb(rc == ERROR_SUCCESS ? a_state : nullptr);
				}
			}
			return rc;
		}

		BOOL WINAPI ChainedPeek(LPMSG a_msg, HWND a_wnd, UINT a_min, UINT a_max, UINT a_remove)
		{
			const BOOL r = g_prevPeek ? g_prevPeek(a_msg, a_wnd, a_min, a_max, a_remove) : FALSE;
			if (GetCurrentThreadId() == g_gameThread && !g_inFrame) {
				const ULONGLONG now = GetTickCount64();
				if (now - g_lastFrame >= 2) {
					g_lastFrame = now;
					g_inFrame = true;   // a callback that pumps messages itself must not re-enter
					g_reads.fetch_add(1, std::memory_order_relaxed);
					if (auto cb = g_frame.load(std::memory_order_acquire)) {
						cb();
					}
					g_inFrame = false;
				}
			}
			return r;
		}

		// Repoints the game executable's import of a_dll!a_name (or ordinal a_ordinal) at a_replacement; the old target
		// goes to *a_previous.
		bool Chain(const char* a_dll, const char* a_name, WORD a_ordinal, void* a_replacement, void** a_previous)
		{
			auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
			const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!dir.VirtualAddress) {
				return false;
			}
			for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
				const char* dll = reinterpret_cast<const char*>(base + desc->Name);
				if (_strnicmp(dll, a_dll, std::strlen(a_dll)) != 0) {
					continue;
				}
				auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
				auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
				for (; names->u1.AddressOfData; ++names, ++slots) {
					const bool match = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)
					                       ? (a_ordinal && IMAGE_ORDINAL64(names->u1.Ordinal) == a_ordinal)
					                       : std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, a_name) == 0;
					if (!match) {
						continue;
					}
					auto* slot = reinterpret_cast<void**>(&slots->u1.Function);
					DWORD old = 0;
					if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
						return false;
					}
					*a_previous = *slot;
					*slot = a_replacement;
					VirtualProtect(slot, sizeof(void*), old, &old);
					logger::info("tick: chained after the game's {} {} import (previously {})", dll, a_name, *a_previous);
					return true;
				}
			}
			return false;
		}
	}

	bool Install(FrameCallback a_frame, PadCallback a_pad)
	{
		g_frame.store(a_frame, std::memory_order_release);
		g_pad.store(a_pad, std::memory_order_release);
		if (g_prevPeek || g_prevXInput) {
			return true;
		}
		g_gameThread = GetCurrentThreadId();
		const bool peek = Chain("user32", "PeekMessageW", 0, reinterpret_cast<void*>(&ChainedPeek), reinterpret_cast<void**>(&g_prevPeek));
		const bool pad = Chain("xinput", "XInputGetState", 2, reinterpret_cast<void*>(&ChainedXInput), reinterpret_cast<void**>(&g_prevXInput));
		if (!peek) {
			logger::error("tick: the game imports no PeekMessageW - no frame tick, the tween menu cannot work");
		}
		if (!pad) {
			logger::warn("tick: the game imports no XInputGetState - the menu cannot take the controller");
		}
		return peek;
	}

	std::uint64_t Reads()
	{
		return g_reads.load(std::memory_order_relaxed);
	}
}

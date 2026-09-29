#include "Tick.h"


namespace tick
{
	namespace
	{
		using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

		XInputGetState_t           g_previous = nullptr;
		std::atomic<Callback>      g_callback{ nullptr };
		DWORD                      g_gameThread = 0;
		std::atomic<std::uint64_t> g_reads{ 0 };

		DWORD WINAPI Chained(DWORD a_user, XINPUT_STATE* a_state)
		{
			const DWORD rc = g_previous ? g_previous(a_user, a_state) : ERROR_DEVICE_NOT_CONNECTED;
			if (a_user == 0 && GetCurrentThreadId() == g_gameThread) {
				g_reads.fetch_add(1, std::memory_order_relaxed);
				if (auto cb = g_callback.load(std::memory_order_acquire)) {
					cb(rc == ERROR_SUCCESS ? a_state : nullptr);
				}
			}
			return rc;
		}
	}

	bool Install(Callback a_callback)
	{
		g_callback.store(a_callback, std::memory_order_release);
		if (g_previous) {
			return true;
		}
		g_gameThread = GetCurrentThreadId();
		auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!dir.VirtualAddress) {
			logger::error("tick: the game has no import table - no tick");
			return false;
		}
		for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
			const char* dll = reinterpret_cast<const char*>(base + desc->Name);
			if (_strnicmp(dll, "xinput", 6) != 0) {
				continue;
			}
			auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
			auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
			for (; names->u1.AddressOfData; ++names, ++slots) {
				const bool match = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)
				                       ? IMAGE_ORDINAL64(names->u1.Ordinal) == 2
				                       : std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, "XInputGetState") == 0;
				if (!match) {
					continue;
				}
				auto* slot = reinterpret_cast<XInputGetState_t*>(&slots->u1.Function);
				DWORD old = 0;
				if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
					logger::error("tick: the import slot could not be made writable ({})", GetLastError());
					return false;
				}
				g_previous = *slot;
				*slot = &Chained;
				VirtualProtect(slot, sizeof(void*), old, &old);
				logger::info("tick: chained after the game's {} XInputGetState import (previously {})", dll, reinterpret_cast<const void*>(g_previous));
				return true;
			}
		}
		logger::error("tick: the game imports no XInputGetState - no tick");
		return false;
	}

	std::uint64_t Reads()
	{
		return g_reads.load(std::memory_order_relaxed);
	}
}

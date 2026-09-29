// tween.menu (rule 64). Runs on TestBench's listener thread; game state is read on the game thread (Pump) and
// waited for.
#include "Tool.h"

#include "Input.h"
#include "Menu.h"
#include "Settings.h"
#include "TestBenchAPI.h"
#include "Tick.h"

#include <condition_variable>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace tool
{
	namespace
	{
		TestBenchAPI::ITestBenchInterface001* g_tb = nullptr;
		std::mutex              g_lock;
		std::condition_variable g_cv;
		bool                    g_wanted = false, g_done = false;
		json                    g_answer;

		void Write(void* a_sink, TestBenchAPI::WriteFn a_write, const json& a_j) { a_write(a_sink, a_j.dump().c_str()); }

		json State()
		{
			const auto s = input::GetStatus();
			return { { "version", TWM_VERSION }, { "layout", settings::Get().layout }, { "ticks", tick::Reads() },
				{ "action_created", s.actionCreated }, { "mapped", s.mapped }, { "wait_keys_moved", s.waitKeysMoved },
				{ "rows_added", s.rowsAdded }, { "bound_keys", s.boundKeys }, { "presses", s.presses }, { "problem", s.problem },
				{ "menu", menu::Status() } };
		}

		void Tool(void*, const char* a_args, void* a_sink, TestBenchAPI::WriteFn a_write)
		{
			json args = json::parse(a_args ? a_args : "{}", nullptr, false);
			const std::string op = args.is_discarded() ? "state" : args.value("op", "state");
			if (op != "state") {
				Write(a_sink, a_write, { { "ok", false }, { "error", "op: state" } });
				return;
			}
			std::unique_lock l(g_lock);
			g_wanted = true;
			g_done = false;
			if (!g_cv.wait_for(l, 3s, [] { return g_done; })) {
				g_wanted = false;
				Write(a_sink, a_write, { { "ok", false }, { "error", "the game thread did not answer in 3 s" } });
				return;
			}
			json out = g_answer;
			out["ok"] = true;
			Write(a_sink, a_write, out);
		}
	}

	void Pump()
	{
		{
			std::scoped_lock l(g_lock);
			if (!g_wanted) {
				return;
			}
		}
		json a = State();
		std::scoped_lock l(g_lock);
		g_answer = std::move(a);
		g_wanted = false;
		g_done = true;
		g_cv.notify_all();
	}

	bool Register()
	{
		if (g_tb) {
			return true;
		}
		HMODULE tb = ::GetModuleHandleW(L"TestBench.dll");
		auto get = tb ? reinterpret_cast<void* (*)(unsigned)>(::GetProcAddress(tb, "TestBench_GetInterface")) : nullptr;
		g_tb = get ? static_cast<TestBenchAPI::ITestBenchInterface001*>(get(1)) : nullptr;
		if (!g_tb) {
			return false;
		}
		g_tb->RegisterTool("tween.menu",
			R"({"description":"Tween Menu state: op state (default) - input action, mapping, Controls rows, bound keys, presses","inputSchema":{"type":"object","properties":{"op":{"type":"string"}}},"readOnly":true})",
			&Tool, nullptr);
		logger::info("TestBench tool registered: tween.menu");
		return true;
	}
}

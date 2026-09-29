#pragma once

// The TestBench driving tool (rule 64): tween.menu - reads the tween menu's state back (game thread).
namespace tool
{
	bool Register();   // once TestBench.dll is loaded (idempotent)
	void Pump();       // every tick, on the game thread: answers a pending request
}

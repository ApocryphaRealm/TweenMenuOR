#pragma once

// Every visible string goes through TR("TWM_<Key>", "English") (rule 66). The eleven files are UTF-16LE with a BOM,
// "$TWM_<Key><TAB>text" per line: OBSE\Plugins\TweenMenu\Translations\TweenMenu_<language>.txt. The language is the
// game's own (KismetInternationalizationLibrary::GetCurrentLanguage), read on the game thread.

namespace strings
{
	void Refresh();                                              // game thread; reloads only when the game's language changed
	const char* Get(const char* a_key, const char* a_english);   // valid until the next reload
	const std::string& Language();
}

#define TR(key, english) ::strings::Get(key, english)

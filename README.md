# Tween Menu for Oblivion

**Version 1.0.0** - an original OBSE64 plugin for The Elder Scrolls IV: Oblivion Remastered.

A tween-style hub menu in the spirit of Skyrim's Tween Menu Overhaul. One button brings up the game's own
Controls-page boxes in four directions around the centre of the screen, with no background, and each box opens one of
the game's menus through the game's own input action:

| Side  | Options            |
|-------|--------------------|
| Up    | Character, Quests  |
| Right | Inventory          |
| Down  | Map, Wait          |
| Left  | Magic              |

* **It follows Wait.** The menu opens on whatever key and button the game's Controls page gives Wait - Select (View) and
  T by default - and Wait becomes one of its options. The game's saved controls are never edited: the plugin moves
  Wait's keys in `IMC_Game_Default` to its own input action at start-up, whenever gameplay resumes, and every 60
  ticks, because the game re-applies its saved map on loading a save and after some menus.
* **Controller and keyboard.** D-pad / left stick or the arrow keys move; A or Enter opens; B, Backspace or the tween
  key closes. While the menu is open the game reads an empty pad.
* **Start opens System** on its far-left Save & Load page, and System always opens there (`bStartOpensSystem`,
  `bSystemOpensFirstPage` in `TweenMenu.ini`).
* Option names in eleven languages (`dist/.../TweenMenu/Translations`), following the game's language.
* A TestBench tool, `tween.menu`, reports the action, its keys and the menu state live.

The plan and the owner's rulings behind the design are recorded in the project's plan document; the changelog is
[CHANGELOG.md](CHANGELOG.md).

## How it runs

OBSE64 has no main-loop interface, so the plugin chains two imports of the game executable: `USER32!PeekMessageW` (the
frame tick, on the game thread, controller or not) and `XINPUT1_3!XInputGetState` (the pad state the game is about to
read, which the open menu consumes). The menu is a UserWidget built at runtime from `WBP_OriginalImageTile_C` tiles with
the Controls page's own brushes and `WBP_AltarTextBlock_C` labels; it stays on the viewport and is shown and hidden, so
the garbage collector never frees it.

## Building

* [xmake](https://xmake.io) 3.0+, MSVC with C++23, and the submodule: `git clone --recurse-submodules`.
* `xmake build TweenMenu`. nlohmann/json and spdlog come from xmake's package registry.
* The plugin goes to `OblivionRemastered\Binaries\Win64\OBSE\Plugins\` beside the game executable (with Mod Organizer
  2, the Root Builder layout) together with everything under `dist/OblivionRemastered/Binaries/Win64/OBSE/Plugins/`.

## Licence

GPL-3.0-or-later (`LICENSE`, `dist/NOTICE.md`); third-party notices in `dist/THIRD_PARTY_NOTICES.md`. The modding
exception from the CommonLibOB64 plugin template is kept as `EXCEPTIONS`.

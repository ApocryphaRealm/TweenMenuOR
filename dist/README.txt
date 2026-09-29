Tween Menu (Oblivion Remastered)
=======================
Version 1.0.0

An original, GPL-3.0-or-later OBSE64 plugin for The Elder Scrolls IV: Oblivion Remastered: a
tween-style hub menu in the spirit of Skyrim's Tween Menu Overhaul. One button brings up the
game's own Controls-page boxes around the centre of the screen - no background - and each box
opens one of the game's menus.

THE MENU
--------
          Quests
         Character
  Magic            Inventory
            Map
            Wait

  * Up: Character and Quests. Right: Inventory. Down: Map and Wait. Left: Magic.
  * Every option opens the game's own menu the way its own key does, with the game's own rules.
  * The world keeps running while the menu is up.

OPENING IT - IT FOLLOWS WAIT
----------------------------
The tween menu opens on whatever key and button the game's Controls page gives Wait - Select
(View) on a controller and T on the keyboard, out of the box. Wait itself becomes one of the
menu's options (Down). To move the tween menu to another key or button, rebind Wait on the
Controls page. The game's saved controls are never edited.

USING IT
--------
  Controller: the D-pad or the left stick moves, A opens, B or the tween button closes.
  Keyboard:   the arrow keys move, Enter opens, Backspace or the tween key closes.
  While the menu is open the game does not see the controller, so nothing you press in the menu
  also happens in the game.

START OPENS SYSTEM
------------------
  * The Start (Menu) button opens System on its Save & Load page instead of the Character page.
  * System always opens on that far-left page, not the page it was last left on.
  Both can be switched off in TweenMenu.ini.

NOT YET IN THIS VERSION
-----------------------
  * Choosing which functions appear, and on which side, from the Controls page.
  * The Alternative layout (icons instead of text boxes).
  * A setting that pauses the world while the menu is open.

REQUIREMENTS
------------
  * The Elder Scrolls IV: Oblivion Remastered (Steam, runtime 1.512.105)
  * OBSE64 (Oblivion Script Extender 64)
  * Address Library for OBSE Plugins

INSTALLING
----------
The plugin goes beside the game executable, in
OblivionRemastered\Binaries\Win64\OBSE\Plugins\. With Mod Organizer 2 that means the Root
folder layout (Root Builder); launch the game through OBSE64.

FILES
-----
  * OBSE/Plugins/TweenMenu.dll - the plugin (TweenMenu.pdb, its debug symbols, beside it)
  * OBSE/Plugins/TweenMenu.ini - its settings (Start opens System, System's first page, log level)
  * OBSE/Plugins/TweenMenu/Translations - the option names in eleven languages
  * The log is Documents/My Games/Oblivion Remastered/OBSE/Logs/TweenMenu.log.
    It is written at info; set uLogLevel=0 in the INI for everything when reporting a problem.

LICENCE
-------
GPL-3.0-or-later, original work (LICENSE, NOTICE.md). The components it links and their
notices: THIRD_PARTY_NOTICES.md.

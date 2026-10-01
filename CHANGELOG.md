# TweenMenuOR - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Each version carries its
**version-ledger status**, so this file cannot quietly claim more than the ledger does:

* **working** - observed running in game
* **untested** - built and packaged, not yet confirmed
* **failed** - built but crashed or malfunctioned; the number was reclaimed
* **scratch** - a hypothesis-test build that never held a real number

## 1.0.1 - 2026-09-30 - untested

### Added
- `TweenMenu_IsOpen()` exported for other mods: true while the menu is up, and just after it closes - while the
  controller is still drained and while the option picked in it is waiting to open. Improved Wheel Menu looks it up by
  name and keeps its wheels (the ammo wheel included) shut then (the owner, 2026-09-30: "The ammo wheel shouldn't be
  able to be called during the tween menu event for when you're done").

### Fixed
- The menu's widget is created through a fault-guarded call, and never with a controller that is being destroyed
  (gate rule or-world-context-calls-are-guarded, after Minimap Menu's crash on quitting, 2026-09-30).
- The "is this widget still alive" check reads the widget's slot index under a fault guard, so a widget the game has
  already garbage-collected returns "gone" instead of crashing (Apocrypha Menu Framework crashed this way on a loadout
  swap, 2026-09-30).
- The reflection self-check asks again when the game has not linked the property it checks yet (the first frames of a
  launch) instead of giving up for the session (gate rule or-reflect-selfcheck-never-latches-not-found, as in Simple
  Loadout System).

## 1.0.0 - 2026-09-29 - working

### Added
- the tween menu - a hub opened on one button, built from the game's own Controls-page boxes in four directions with no background: Character and Quests up, Inventory right, Map and Wait down, Magic left. Each option opens the game's own menu through its own input action.
- linked key mapping: the tween menu opens on whatever key and button the Controls page gives Wait (Select / View and T by default), and Wait becomes one of its options. Rebind Wait to move it; the game's saved controls are never edited.
- controller (D-pad or left stick, A opens, B or the tween button closes) and keyboard (arrow keys, Enter opens, Backspace or the tween key closes) navigation; while the menu is open the game sees an empty pad.
- Start opens System on its far-left Save & Load page (bStartOpensSystem), and System always opens there instead of the page it was last left on (bSystemOpensFirstPage).
- option labels in eleven languages, following the game's language.
- a TestBench tool (tween.menu) reporting the action, its keys and the menu state live.

### Known
- not yet: choosing which functions appear and on which side from the Controls page, the Alternative (icon) layout, and an INI toggle to pause the world while the menu is open.
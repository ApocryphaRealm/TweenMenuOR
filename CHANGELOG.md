# TweenMenuOR - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Each version carries its
**version-ledger status**, so this file cannot quietly claim more than the ledger does:

* **working** - observed running in game
* **untested** - built and packaged, not yet confirmed
* **failed** - built but crashed or malfunctioned; the number was reclaimed
* **scratch** - a hypothesis-test build that never held a real number

## 1.0.0 - 2026-09-29 - untested

### Added
- the tween menu - a hub opened on one button, built from the game's own Controls-page boxes in four directions with no background: Character and Quests up, Inventory right, Map and Wait down, Magic left. Each option opens the game's own menu through its own input action.
- linked key mapping: the tween menu opens on whatever key and button the Controls page gives Wait (Select / View and T by default), and Wait becomes one of its options. Rebind Wait to move it; the game's saved controls are never edited.
- controller (D-pad or left stick, A opens, B or the tween button closes) and keyboard (arrow keys, Enter opens, Backspace or the tween key closes) navigation; while the menu is open the game sees an empty pad.
- Start opens System on its far-left Save & Load page (bStartOpensSystem), and System always opens there instead of the page it was last left on (bSystemOpensFirstPage).
- option labels in eleven languages, following the game's language.
- a TestBench tool (tween.menu) reporting the action, its keys and the menu state live.

### Known
- not yet: choosing which functions appear and on which side from the Controls page, the Alternative (icon) layout, and an INI toggle to pause the world while the menu is open.
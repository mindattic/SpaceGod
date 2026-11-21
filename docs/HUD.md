# The HUD: drawers, docking and the HUD key

The HUD keeps a quiet screen until it is asked for more. Its panels are becoming **drawers**: windows that can be pulled out
from any screen edge, collapsed, pinned, moved, docked to an edge or to each other, or left floating. One key, the **HUD
key**, switches between the quiet screen and the full HUD with its drawers.

Code: `Cpp/Source/SpaceGod/UI/HudDrawers.h/.cpp` (the registry and window manager), `UI/HudDock.h` (the layout math, unit
checked by `SpaceGodCore/Tests/HudDock/HudDockTests.cpp` through `tools/build-core.ps1`), `UI/Hud.Drawer.*` (the drawers
registered so far), `UI/Hud.cpp` (`Hud::Draw`, the minimal HUD). Tickets: US-T438 (drawers), US-T439 (the HUD key and the
minimal HUD). Every other panel becomes a drawer in US-T440; saved layouts ("Combat", "Commerce", "Piracy"...) come in US-T441.

## The HUD key (minimal HUD and HUD mode)

The HUD key is **Caps Lock** by default (`KeyAction::HudMode`; Z, the first idea, is roll left at the controls). Rebind it
under Settings > Controls > Key bindings, View group, like any action (three slots, any key, mouse button or pad input).

**Minimal HUD (HUD mode off, how a game starts).** The screen shows only:

- the reticle and the flight markers, and everything drawn on the world: names, brackets, markers, off-screen arrows;
- the combat overlays: the lock box and lead pip, the combat gauges, hit markers, damage arcs and the kill feed;
- the drawers that are **pinned**;
- urgent alerts: toasts, the threat and patrol hail lines, the rewind readout and bar, the self-destruct countdown, the Bio
  Mass warning, lane, course and interdiction banners, the embody gate's refusals; the salvage and boarding prompts;
- the edge tab of any drawer raising an urgent alert (it glows red, with the alert's line);
- whatever the player opens by its key (market, cargo, outfitting, boards, fleet, trade desk, port, picker, saves, the news
  feed, comms, the battle plot, the full screens and maps) and any drawer opened by its key (Shift+U, Shift+X).

**HUD mode (the key pressed).** The cursor is freed (the mouse no longer steers at the controls; the keys still fly the
ship, and a click fires only when the pointer is off the HUD, as before), the gamepad drives the GUI (its menu mode), every
drawer that is pulled out shows, every edge shows its closed drawers as pull-out tabs, and the panels that are not drawers
yet show as they always did. Press the key again for the quiet screen.

While the guided tutorial runs the full HUD shows (it points at HUD elements), without freeing the cursor. **U** still hides
the HUD's panels altogether (names and markers stay) and **F11** still clears the screen of everything but the reticle; both
work the same in either mode.

Why the game starts in the minimal HUD: the player asked for a quiet screen by default, and the screen stays usable in it:
everything the player opens by a key still shows, every prompt that asks for an answer still shows, and the full HUD is one
key away. What the minimal HUD hides until US-T440 makes it a drawer that can be pinned: the top bar (time controls; their
keys still work), the navigator, the selection card, the status line, the opportunity board and the other always-on panels.

## Drawers

A drawer lives in a **window**. A window holds one drawer, or several docked to each other (a **group**).

| What | How |
| --- | --- |
| Pull a drawer out | Click its tab on the screen edge (HUD mode), or its key |
| Collapse / open | Click the title bar (the window shrinks to it); in a group, click the tab already shown |
| Move | Drag the title bar |
| Dock to an edge | Let go near a screen edge: it stacks with what is docked there (down the left edge from the top, up the right edge from the bottom, left to right along the bottom, centred along the top) |
| Float | Let go anywhere else; drag the corner grip to resize it |
| Dock to another drawer | Let go over another window's title bar: they become one window (a group) |
| Tabs or stack | A group's TABS / STACK button: one drawer at a time under a strip of tabs, or all of them one over another, each with its own header |
| Tear one off a group | Drag its tab (or, stacked, its header) out |
| Tear off from an edge tab | Drag the edge tab out: the drawer floats under the pointer |
| Pin | The PIN button: the window stays on screen in the minimal HUD |
| Close | The x button: back to its edge tab |
| Size (keys) | PgUp / PgDn on a drawer opened by its key: its window a step bigger or smaller (smaller than the least closes it) |

Windows are kept on screen at any resolution and UI scale: a floating window is never bigger than the screen and is moved
back in whole. Each drawer can raise an **alert**: its tab glows (amber for a notice, a red pulse for something urgent) and
its window's title bar is tinted; in the minimal HUD an urgent one shows its tab with the alert's line.

### The drawers so far

| Drawer | Starts | Alert |
| --- | --- | --- |
| Navigation, Ships, Notices, Objectives | one window of tabs along the bottom edge, closed (US-T200's bottom drawer) | Objectives: offers waiting |
| Reactor | docked at the foot of the right edge, at the controls of a craft with a reactor to share | OFFLINE (urgent) |
| Sensors | over the reactor, at a ship's helm | LIT UP after a ping (urgent), RESTARTING |

The bottom four keep their keys: **Shift+U** opens and closes the last of them used, **Shift+X** opens Notices. One opened by
its key has the keys while it shows: Tab / Shift+Tab (or Left / Right) the next of the four, Up / Down the rows, Enter acts,
F steps the Notices filter, PgUp / PgDn the size, Esc closes. The cursor is free meanwhile.

### Saving a layout

`HudDrawers::Save()` writes the whole layout as a JSON object, and `Load()` reads it back (drawers it does not name keep
their default windows; names that are not registered are dropped):

```json
{ "version": 1, "windows": [
  { "drawers": ["navigation", "ships", "notices", "objectives"], "active": 0, "tabbed": true,
    "shown": false, "collapsed": false, "pinned": false,
    "docked": true, "edge": "bottom", "order": 0, "w": 0, "h": 420,
    "x": 240, "y": 160, "fw": 480, "fh": 420 } ] }
```

`w`/`h` is the docked size (0 along the edge fills it), `x`/`y`/`fw`/`fh` the floating rect. US-T441's presets store these.

## For code: making a panel a drawer

```cpp
DrawerSpec s;
s.id = "cargo";                       // stable: layouts are saved by it
s.title = "CARGO";
s.edge = HudDock::Edge::Left;         // where it docks by default
s.size = Vector2(300, 260);           // w x h docked (0 along the edge: fill)
s.shown = true;                       // pulled out to begin with (else closed to its tab)
s.pinned = false;                     // shown in the minimal HUD to begin with
s.group = "";                         // drawers with the same group start as one window of tabs
s.available = [this] { return app->IsPiloting(); };      // when it can show at all (null: always)
s.alert = [this] { return DrawerAlert{ 2, "HOLD FULL" }; }; // what it raises (null: never)
s.draw = [this](Rect r) { DrawCargoBody(r); };           // fills the content rect it is handed
drawers.Register(std::move(s));       // in Hud::RegisterDrawers
```

The draw callback draws what the panel drew inside its own rect, with no `Panel()` call and no `HudLayout::Take`: the
manager places it, frames it through the HUD's `Panel()` (so the world under it takes no clicks and world labels hide under
it) and draws its title bar. It is called once per GUI pass while the drawer shows; use a scroll view for content that may
not fit. Drive it with `Open(id, peek)` (peek: show it in the minimal HUD too, for a key), `Close`, `Toggle`, `Visible`,
`SetPinned`, `SetCollapsed`, `Dock(id, edge, order)`, `Float(id, rect)`, `Join(id, into)`, `Detach(id, rect)`, `Grow(id, dir)`;
`RectOf(id)` (or the static `PublishedRect(id)`) says where it is drawn, for the tutorial's outline.

## Checking it

`sg.HudLayout shot <dir> [quit]` takes the controls of a practice fighter and screenshots: the minimal HUD (the sensors' tab
glowing after a ping), HUD mode with its edge tabs, a drawer floated, two drawers docked into a tabbed group, and a collapsed
and a pinned window (in HUD mode and in the minimal HUD); it checks that the layout saves and loads back the same, and puts
the layout and the mode back as they were.

```powershell
./tools/play.ps1 -Offscreen -Capture '' -Seconds 300 -ExecCmds "sg.HudLayout shot R:/Temp/hudlayout quit"
```

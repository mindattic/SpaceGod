# The HUD: drawers, docking and the HUD key

The HUD keeps a quiet screen until it is asked for more. Every one of its panels is a **drawer**: a window that can be pulled
out from any screen edge, collapsed, pinned, moved, docked to an edge or to each other, or left floating. One key, the **HUD
key**, switches between the quiet screen and the full HUD with its drawers.

Code: `Cpp/Source/SpaceGod/UI/HudDrawers.h/.cpp` (the registry and window manager), `UI/HudDock.h` (the layout math, unit
checked by `SpaceGodCore/Tests/HudDock/HudDockTests.cpp` through `tools/build-core.ps1`), `UI/Hud.Drawer.*` (the bottom
drawer's four, the reactor and the sensors), `UI/Hud.DrawerPanels.cpp` (every other panel's drawer and its default edge),
`UI/Hud.cpp` (`Hud::Draw`, the minimal HUD). Tickets: US-T438 (drawers), US-T439 (the HUD key and the minimal HUD), US-T440
(every panel a drawer); saved layouts ("Combat", "Commerce", "Piracy"...) come in US-T441.

## The HUD key (minimal HUD and HUD mode)

The HUD key is **Caps Lock** by default (`KeyAction::HudMode`; Z, the first idea, is roll left at the controls). Rebind it
under Settings > Controls > Key bindings, View group, like any action (three slots, any key, mouse button or pad input).

**Minimal HUD (HUD mode off, how a game starts).** The screen shows only:

- the reticle and the flight markers, and everything drawn on the world: names, brackets, markers, off-screen arrows;
- the combat overlays: the lock box and lead pip, the combat gauges, hit markers, damage arcs and the kill feed;
- the drawers that are **pinned**;
- urgent alerts: toasts, the threat and patrol hail lines, the rewind readout and bar, the self-destruct countdown, the Bio
  Mass warning, lane, course and interdiction banners, the embody gate's refusals; the salvage and boarding prompts (the
  rewind readout and bar, a lane ride's off-ramps, the interdiction line, the comms panel and the tutorial's card are
  drawers flagged **alert, always shown**: drawn as the overlays they were, with no title bar, in the minimal HUD);
- the edge tab of any drawer raising an urgent alert (it glows red, with the alert's line);
- whatever the player opens by its key (market, cargo, outfitting, boards, fleet, trade desk, port, picker, saves, the news
  feed, comms, the battle plot, the full screens and maps) and any drawer opened by its key (Shift+U, Shift+X).

**HUD mode (the key pressed).** The cursor is freed (the mouse no longer steers at the controls; the keys still fly the
ship, and a click fires only when the pointer is off the HUD, as before), the gamepad drives the GUI (its menu mode), every
drawer that is pulled out shows (each with its title bar: the alerts too, so they can be moved), and every edge shows its
closed drawers as pull-out tabs (between the windows docked there). Press the key again for the quiet screen.

While the guided tutorial runs the full HUD shows (it points at HUD elements), without freeing the cursor. **U** still hides
the HUD's panels altogether (names and markers stay) and **F11** still clears the screen of everything but the reticle; both
work the same in either mode.

Why the game starts in the minimal HUD: the player asked for a quiet screen by default, and the screen stays usable in it:
everything the player opens by a key still shows, every prompt that asks for an answer still shows, and the full HUD is one
key away. What the minimal HUD hides (the top bar, whose time keys still work, the navigator, the selection card, the status
line, the opportunity board and the other always-on panels) can be pinned to stay in it.

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

### The drawers

Every HUD panel is a drawer (US-T440), and starts where the panel always was, so the screen looks as it did. Each edge holds
its windows one over another: the left edge down from the top (its far stack up from the foot), the right edge up from the
foot (its far stack, the right-hand column's top, down from the top), the top edge down from the top and the bottom edge up
from the foot, each window centred across the screen. A floating drawer marked centred stays centred across the screen until
it is moved. A drawer marked *fit* takes the size its content asks for (a market with more rows is taller), until it is
resized by hand.

| Drawer (id) | Starts | Shows |
| --- | --- | --- |
| Clock & scale (`topbar`) | top edge, first | HUD mode |
| Rewind timeline (`rewindbar`) | top edge, under the top bar | alert, always shown (after Shift+F8, while a seek replays) |
| Cruise drive (`cruise`) | top edge | HUD mode, while the cruise drive spools or cruises |
| News (`news`) | top edge, under the top bar | its key (X), either mode |
| Headlines (`ticker`) | top edge, under the top bar | HUD mode, while the news feed is closed |
| Navigator (`navigator`) | left edge, first | HUD mode |
| Influence (`influence`) | left edge, under the navigator | HUD mode |
| Cargo (`cargo`) | left edge, at the foot (its far stack) | its key, either mode |
| Rewind (`holdrewind`) | right edge, at the top (REWIND ready / its cooldown) | alert, always shown, no frame in the minimal HUD |
| Opportunities (`opportunities`) | right edge, top stack | HUD mode, while the market is closed |
| Market (`market`) | right edge, top stack (the opportunities' place) | its key, either mode |
| Gas works (`harvest`), Hunted (`hunters`) | right edge, top stack | HUD mode, when there is something to show |
| Reactor (`reactor`), Sensors (`sensors`) | right edge, at the foot | HUD mode, at the controls; alerts OFFLINE, LIT UP |
| Target (`target`), Selection (`selection`) | right edge, over the sensors | HUD mode; the selection card takes what the edge leaves and scrolls in it |
| Status (`status`) | bottom edge, at the foot | HUD mode |
| Seat (`seat`), Helm (`helm`), Tractor beam (`tractor`), FTL drive (`ftl`) | bottom edge, over the status line | HUD mode, when there is something to show |
| Hyperlane (`laneride`), Interdiction (`interdiction`) | bottom edge | alert, always shown |
| Navigation, Ships, Notices, Objectives | one window of tabs along the bottom edge, closed, over the rest (US-T200's bottom drawer) | HUD mode; Objectives: offers waiting |
| Bounties, Contracts, Fleet, Outfitting (and its mechlab view), Trade desk, Port, Take charge (the seat picker), Save & load | floating, centred, where each always opened | its key, either mode (Save & load with the HUD hidden too) |
| Comms (`comms`), Tutorial (`tutorial`) | floating, centred over the foot | alert, always shown while it is up |

A drawer opened by a key or a flag keeps it: the key (or the console, or co-op) sets the flag (`ShowMarket`, `ShowCargo`,
`ShowPort`, `ShowNews`...) and the flag opens and closes the drawer; the drawer's edge tab and close button set the flag back
(`Hud::SyncPanelFlags`), so everything that reads the flag sees the drawer as it is.

What stays an overlay, not a drawer, and why: the combat overlays (the combat gauges, the lock box and lead pip, the flight
markers, the off-screen arrows, hit feedback), which belong where the world is; the banners and alerts at the top middle (the
Bio Mass warning, the squad's banner, a plotted course, the autopilot offer), which stack under the top edge's drawers
(`HudLayout`, TopCentre); toasts, the threat and patrol hail lines, the self-destruct countdown, the embody gate's refusal,
the hold rewind's loud lines (a hold under way, the hull's or the fight's end held back, co-op's wait: centred, as the alerts
they are); the salvage and boarding prompts, the dock prompt, the tutorial's welcome offer and the outline round what a step
points at; the seat's sight, deck guide and role panel; the between-the-stars banners (a trip, FTL cruise, a void stop's
sensors); the mining beam's readout; the full screens (maps, encyclopedia, society, help, logs), which stay full screens. The
flight readout ("FLIGHT ASSIST ON") keeps over the bottom edge's drawers and their tabs.

In a group, a tab too narrow for its drawer's title shows its short title (REACTOR is PWR, SENSORS is SNS, the bottom four NAV,
SHIPS, NOTES, OBJ), and a narrow window's title-bar buttons are one letter (P pins, = or T stacks or tabs), so the titles keep
their room. A long row (a notice, a name) is cut with an ellipsis, its details keeping a third of the row.

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

`w`/`h` is the docked size (a `w` of 0 fills the edge's width, an `h` of 0 on the left or right edge its height),
`x`/`y`/`fw`/`fh` the floating rect, `far` the edge's far stack, `fit` whether it still follows its content's size, `centred`
whether a floating window is still kept centred. US-T441's presets store these.

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

A panel that sizes itself (most of the HUD's) is registered with `AddPanel` (UI/Hud.DrawerPanels.cpp) instead: its `Draw*`
works out its w x h as it always did and asks for it with `auto r = Place(w, h);` (the drawer is *fit*: its window takes that
size from the next pass), then draws into `r` as it drew into its own panel; `PlaceRoom()` says how tall it may be (what its
edge leaves it). A fit drawer that asks for nothing when drawn (the panel had nothing to show) is not drawn, and is asked again
off the screen on each Layout pass. `AddPanel(spec, body, &app->ShowX)` keeps a flag in step with it. The spec's other US-T440
fields: `keyed` (shown in either mode while pulled out), `always` (an alert: shown whenever it can, no close button), `bare`
(no title bar in the minimal HUD; `frameless`: no panel either), `floatY` / `floatBottom` (starts floating, centred across),
`far` (the edge's far stack), `yields` (takes what its edge leaves), `shortTitle` (for a narrow tab).

The draw callback draws what the panel drew inside its own rect, with no `Panel()` call and no `HudLayout::Take`: the
manager places it, frames it through the HUD's `Panel()` (so the world under it takes no clicks and world labels hide under
it) and draws its title bar. It is called once per GUI pass while the drawer shows; use a scroll view for content that may
not fit. Drive it with `Open(id, peek)` (peek: show it in the minimal HUD too, for a key), `Close`, `Toggle`, `Visible`,
`SetPinned`, `SetCollapsed`, `Dock(id, edge, order)`, `Float(id, rect)`, `Join(id, into)`, `Detach(id, rect)`, `Grow(id, dir)`;
`RectOf(id)` (or the static `PublishedRect(id)`, and `PublishedContent(id)` for its content rect) says where it is drawn: the
tutorial's outline asks it where the top bar's time and scale controls and the navigator are (`Hud::TutorialHighlightRect`),
and the showcase tour's strip stops short of the right edge's drawers (`HudLayout::PublishedColumnLeft`).

## Checking it

`sg.HudLayout shot <dir> [quit]` takes the controls of a practice fighter and screenshots: the minimal HUD (the sensors' tab
glowing after a ping), HUD mode with its edge tabs, a drawer floated, two drawers docked into a tabbed group, and a collapsed
and a pinned window (in HUD mode and in the minimal HUD); then (US-T440) the layout reset with Earth selected, HUD mode with
every default drawer (`all-drawers`), the market, the cargo hold, the fleet and the port (docked at Earth) each opened by its
flag, and the market again in the minimal HUD (`minimal-market`). It checks the layout saves and loads back the same, that
each drawer starts where its panel was (the top bar centred at the top, the navigator top left, the status line on the foot,
the selection card over the reactor, the fleet floating centred), that each modal opens and closes with its flag, and puts
the layout and the mode back as they were.

```powershell
./tools/play.ps1 -Offscreen -Capture '' -Seconds 300 -ExecCmds "sg.HudLayout shot R:/Temp/hudlayout quit"
```

// Port of Unity.SpaceGod Core/ControlsHelp.cs (see docs/MIGRATION.md).

#include "ControlsHelp.h"

namespace SpaceGod::Core
{
	const cs::List<cs::Ref<HelpSection>>& ControlsHelp::Sections()
	{
		static const cs::List<cs::Ref<HelpSection>> value =
		{
			cs::New<HelpSection>("CAMERA AND GOD VIEW",
				L("W A S D", "swim"),
				L("Q / E  or  C / ]", "down / up"),
				L("Shift", "boost"),
				L("Ctrl", "fine"),
				L("Wheel", "swim speed / zoom"),
				L("Right-drag", "look / orbit"),
				L("Left-click", "select (double-click: focus)"),
				L("F", "focus the selection"),
				L("Home", "system view"),
				L("1-9, 0", "planets (twice to focus)"),
				L("Tab", "cycle ships"),
				L("B", "watch a battle"),
				L("Space", "pause (eases to a stop and back)"),
				L(",  .", "time warp slower / faster"),
				L("T", "back to now"),
				L("G  or  `", "toggle GAMEPLAY / REALISTIC scale"),
				L("R", "auto-reorient on / off (keep this system's plane level)"),
				L("O", "orbits"),
				L("L", "labels"),
				L("N", "hyperlanes"),
				L("K", "satellites"),
				L("Shift+K  or  numpad *", "skybox: the next sky for the system in view, which keeps it (Shift+Alt+K or numpad / goes back)"),
				L("Navigator", "STAR SYSTEMS: fly to another star (Esc to skip the trip)"),
				L("Esc", "close panels / leave focus / deselect, then the pause menu")),

			cs::New<HelpSection>("HELM AND FLIGHT",
				L("P", "pilot the selected ship (P again hands it back)"),
				L("V", "spawn & pilot: pick a hull from the grid to spawn and pilot it at the camera"),
				L("Shift+V", "practice fighter: one of your fighters at the camera, clear of what is there, you at its helm, to fly beside other hulls and get the feel of them (again brings the same one over)"),
				L("Mouse", "steer"),
				L("W / S", "thrust"),
				L("A / D  Q / E", "strafe"),
				L("Z / X", "roll"),
				L("F", "flight assist: on, the thrusters cancel drift and it flies where it points; off, true Newtonian: it keeps its momentum, the hull turns free of its track, W/S are raw thrust (S the retros, braking without a flip) and the HUD shows what stopping costs"),
				L("Alt (hold)", "flip: swing the nose onto the retrograde, then W burns the speed off (a turn and burn); let go and the stick has the nose again"),
				More("fighters: the mouse aims and the hull turns after it"),
				L("Shift + A/D Q/E", "light fighters, interceptors: a lateral boost, a hard sideways dash on the afterburner"),				More("heavy fighters, freighters: the mouse turns the nose"),
				More("heavy fighters: weightier, more momentum; Shift is a hard boost that carries"),
				More("capital ships: A/D Q/E turn the hull, the mouse looks around"),
				L("Alt (hold), capital", "broadside stance: A/D Q/E slide the hull sideways and up/down instead of turning it, and the helm lays it beam-on to the locked hull (T) so the nearer flank's batteries bear"),
				L("Arrows", "reactor power, at any controls (a helm, a fighter, a gun, a station): Left / Up / Right a pip to weapons / shields / engines, Down an even share; with Shift, all it can spare to that one (the REACTOR panel shows the split; a gun draws on its hull's)"),
				More("weapons: reload, rate of fire, guns cool; shields: recharge and how much field they hold; engines: top speed, thrust, boost recharge"),
				L("Ctrl", "freighters, miners, in a port's approach: docking mode (docking speed, a gentle turn)"),
				L("Ring gate", "hyperlanes: fly through a ramp's ring gate to ride the lane"),
				L("1  /  2-9", "on the lane: 1 come off, 2-9 carry on at the off-ramp"),
				L("Shift", "interdiction (riding): a raider's field ahead, surge through"),
				L("Ctrl", "interdiction (riding): drop out short of the field"),
				L("Y", "back into the lane where you left it"),
				L("Enter", "FTL (capital ships, survey hulls): beyond the system's outer rim, put the nose on another star, Enter to spool (hold it there); it engages for that star"),
				L("PgDn / PgUp", "plot a course to the next star or beacon (nearest first), or pick one on the galaxy map ([): ETA, fuel, wells on the way"),
				L("Del", "clear the plotted course"),
				L("End", "FTL autopilot: flies out beyond the rim, turns onto the star, spools and engages, time compressed (Settings > FTL); End again takes the helm back"),
				L("F3", "silent running: transponder off, heat damped -- only what your scanners hold is shown: blips, then class, then name"),
				More("your sensors are passive: they hear what others give off, as far as your scanners reach their signature; planets, moons and big rocks block the line of sight both ways"),
				L("Tab", "active ping: for 3 s your scanners reach three times as far and resolve twice as fine -- but you are LIT UP for 15 s, heard by every sensor in reach (hostiles hold you, witnesses see you); again after 20 s"),
				L("Shift+F3", "go dark: the reactor to minimum -- guns, shields and drive offline, transponder off, drifting on your momentum, your heat cooling toward nothing (hide behind a rock); Shift+F3 again powers up: a timed restart (8 s a fighter to a minute a capital), nothing online until it is done and your heat climbing back"),
				L("1, then 1-4", "derelicts and wrecks: match velocity alongside, 1 latch on, then 1-4 as the panel shows")),

			cs::New<HelpSection>("COMBAT",
				L("Left mouse", "guns, down the crosshair (watch the heat)"),
				L("T", "lock the hull under the crosshair; hold the crosshair on it to lock"),
				L("Right mouse", "homing and hive missiles once locked; dumb-fire rockets straight down the nose, no lock"),
				More("a vulcan rotary cannon spins up as it fires: short bursts from cold, a torrent once spun (watch the heat)"),
				L("Middle mouse", "EMP (knocks out a small craft's drives and shields)"),
				L("Ctrl", "drop a mine astern; an auto turret fires on its own"),
				L("Insert", "retune the phaser arrays: a narrow lance for hulls or a wide sweep for fighters and missiles"),
				More("a plasma cannon hits hardest close in"),
				L("Hold left mouse", "capital ships: look at a side and hold to build a broadside, let go to fire it"),
				More("the turrets fight on their own, at the locked hull first"),
				More("captain's orders, to the side you look at:"),
				L("1", "that battery at will / hold fire / volley"),
				L("2", "gunners: focus the weakest shield patch / spread fire"),
				L("3", "brace"),
				L("4", "damage control focus"),
				L("5  6  7", "line ahead, line abreast, echelon"),
				L("8  9", "screen, engage at will"),
				L("0", "all batteries at will"),
				More("angle the hull to the enemy's line to make shots glance"),
				L("Shift+Insert", "big ships: run hot (guns and drive at full past the heat limit, the crew and systems cook) or throttle for the heat"),
				More("shoot a hull's radiators away to make it overheat"),
				L("Shift+Del", "capital ships: the helm rolls your weak shield patches away from the heaviest fire (the readout above the orders names them)"),
				L("B", "boarding (a fight aboard your hull, or one your marines are in): the panel of compartments and orders"),
				L("1  2  3  0", "with it open: pick a compartment, push for it / muster on it, hold, auto-resolve")),

			cs::New<HelpSection>("TRADE",
				L("M", "market: Buy / Sell and Refuel appear while piloting near the shown settlement's body"),
				L("I", "cargo hold (while piloting): click a crate, then a cell, to move it"),
				L("\\", "trade desk: runs from your price intelligence, brokers, trade news, charters, insurance, warehouses, the back door"),
				L("F7", "contracts board of the port in view: freight, passengers, colonists, escorts, patrols, surveys, bounties"),
				L("F2", "outfitting at a shipyard (or Outfit on the market): guns, shields and drives on the hardpoints; plate, racks, scanners, a brig in the internal space"),
				L("X", "news feed: shocks, price spikes, embargoes, battles and captures as they happen")),

			cs::New<HelpSection>("DIPLOMACY AND THE LAW",
				L("F12", "hail the polity of the ship locked or selected (or the patrol hailing you) and treat with it"),
				L("[", "the galaxy map's DIPLOMACY panel: pacts, tribute, fealty, war and peace"),
				L("T, then fire", "lock a ship, hold fire on it to attack (a crime if seen)"),
				More("patrols scan: hold still"),
				L("1", "pay a fine when hailed"),
				L("F4", "bounty board of the port in view: outlaws wanted there, their records, where last seen, who hunts them")),

			cs::New<HelpSection>("FLEET AND COMMAND",
				L("F10", "your fleet: buy hulls, warehouses, station shares and claims; hire captains and give them standing orders"),
				L("'  ;", "take charge, any time: jump into the next seat in the fight in view (a ship's helm, a fighter or bomber, a station), straight from the one you hold"),
				L("/", "man the next gun on the hull or station you hold (or have selected): the view sits on the mount, down the barrel; past the last gun, back to the helm or station (Shift+/ is the launched craft's, below)"),
				L("Shift+1 ... Shift+0", "at a helm, the hull's own abilities, as its ability bar lists them from what it carries: a carrier launches a squadron of each class it has aboard (interceptors, fighters, bombers, heavy fighters, recon), recalls its wing, and calls it onto your locked hull, to guard the carrier, or to its own fights; a shuttle bay, a Breacher bay or boarding pods send a boarding shuttle, a Breacher or a pod salvo at the hull locked (T). In a fight only; a carrier you fly into a fight holds its wing aboard for you to launch"),
				L("Shift+/", "from a carrier's helm: jump into the craft it launched last (a fighter, bomber or boarding shuttle), again for the next one, past the last back to the carrier's helm"),
				L("Backspace", "at a gun: up to the helm of the hull it is on (or the whole station); in a launched craft, back to its carrier's helm"),
				L("Shift+Backspace (hold)", "self-destruct, at any controls: held a second it arms a countdown (5 s a fighter, 8 s a light hull, 15 s a capital, 20 s a station; a gun sets off its hull), pressed again it aborts; at zero the craft blows with everyone aboard, the blast worst on whatever it is touching"),
				L("F9", "take-charge picker: every seat on the hull in hand and around the camera (helms, fighters, stations, gun mounts), click one to jump in"),
				More("in a fighter: mouse aims, W/S throttle (assist off: raw thrust), Alt flip, left mouse guns, right mouse torpedoes"),
				More("at any seat the craft has the keys and mouse, the camera only follows it (the wheel zooms the chase); keys held through a switch wait to be let go"),
				L("P", "hands the seat back to its AI and returns you to command")),

			cs::New<HelpSection>("SCREENS AND PANELS",
				L("H  or  F1", "this help screen"),
				L("Esc", "pause menu (straight away at the helm): Resume, Help, Save / Load, Settings > Display (resolution, window mode, VSync), Main Menu, Quit"),
				L("[", "galaxy map: every system, who holds it, gate traffic, war fronts, blockades and raiders, as far as the news goes; click a star to inspect, double-click to travel"),
				L("U", "HUD panels and readouts on / off (the names and markers on the view stay)"),
				L("F11", "clear the screen: every bit of UI gone but the reticle in use; F11 again (or opening a panel) brings it all back"),
				L("F5", "quicksave"),
				L("F8", "quickload"),
				L("F6", "save slots"),
				More("autosaves every 5 minutes and on quit; -continue resumes the latest")),
		};
		return value;
	}

	cs::List<HelpLine> ControlsHelp::AllLines()
	{
		// C# yields lazily; nothing depends on that, so the list is built eagerly.
		cs::List<HelpLine> result;
		for (const auto& s : Sections())
			for (const auto& l : s->Lines)
				result.Add(l);
		return result;
	}
}

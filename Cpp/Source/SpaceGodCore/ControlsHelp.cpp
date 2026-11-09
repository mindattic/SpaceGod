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
				L("Tab  /  Shift+Tab", "cycle ships forward / back"),
				L("B", "watch a battle"),
				L("Space  or  Pause", "pause (eases to a stop and back); in a mech Space is its jump jets, Pause still pauses"), // US-T83
				L(",  .  or  -  =  or  numpad -  +", "time warp slower / faster; below real time, slow motion (1/2, 1/4, 1/10, 1/30)"), // US-T166
				L("T", "back to now"),
				L("G  or  `", "toggle GAMEPLAY / REALISTIC scale"),
				L("R", "auto-reorient on / off (keep this system's plane level); at a fighter's controls, held, R is drift"),
				L("O", "orbits"),
				L("L", "labels"),
				L("Shift+L", "recovered logs: the ship logs and station records you have found, to read (Up / Down a log, Left / Right a page)"), // US-T261
				L("Shift+O", "discoveries: everything you have found, by kind and the day (stars charted or visited, bodies surveyed, settlements, pirate havens, graveyards, derelict stations, void sights, sightings, pilots met); each shows its readings and description, with Select, Focus and Plot course buttons; O alone stays the orbits"), // US-T259
				L("Shift+I", "Encyclopedia: every article in one place, the controls as bound now, the manual's guides, the lore, and everything you have learned of (bodies and stars, goods, industries and markets, factions, hulls, stations, settlements, systems, pilots), unlocked as you find them; type to search titles, text and keywords, click a see-also link to follow it, Back / Forward (Alt+Left / Alt+Right) through what you read; I alone stays the cargo hold"), // US-T271
				L("N", "hyperlanes"),
				L("K", "satellites"),
				L("Shift+K  or  numpad *", "skybox: the next sky for the system in view, which keeps it (Shift+Alt+K or numpad / goes back)"),
				More("the sky has two layers: the Milky Way, which never moves, and the system's painting as a cloud the size of its rim that thins as you fly out (Shift+F1 > Travel > Sky: cube, sphere or volume to compare; sg.EdgeRun flies a hull out and back)"), // US-T145
				More("bright points over the sky are the stars your jump drive can reach, each where it really is: put the nose on one to aim at it"), // US-T145
				L("Navigator", "STAR SYSTEMS: fly to another star (Esc to skip the trip)"),
				L("Esc", "close panels / leave focus / deselect, then the pause menu")),

			cs::New<HelpSection>("HELM AND FLIGHT",
				L("P", "embody what is selected or under the mouse: a ship's helm, a hull or station in a fight (P again lets go to the free SpaceGod view)"),
				L("V", "spawn & pilot: pick a hull from the grid to spawn and pilot it at the camera"),
				L("Shift+V", "practice fighter: one of your fighters at the camera, clear of what is there, you at its helm, to fly beside other hulls and get the feel of them (again brings the same one over)"),
				L("Mouse", "steer"),
				L("W / S", "thrust"),
				L("A / D  Q / E", "strafe"),
				L("Z / X", "roll"),
				L("F", "flight assist: on, the thrusters cancel drift and it flies where it points; off, true Newtonian: it keeps its momentum, the hull turns free of its track, W/S are raw thrust (S the retros, braking without a flip) and the HUD shows what stopping costs"),
				L("Alt (hold)", "flip: swing the nose onto the retrograde, then W burns the speed off (a turn and burn); let go and the stick has the nose again"),
				More("every fighter (and a launched craft's seat) is pulled by a string: the camera looks where the mouse puts it and the hull chases it; two reticles, the crosshair where you aim and the nose's boresight swinging over to meet it; the guns converge on the crosshair as far as they gimbal"),
				More("light fighters, interceptors: the string is slack, the view swings anywhere at once (a full 180, to look behind) and the hull whips round after it, the harder the further it has to go"),
				L("Shift + A/D Q/E", "light fighters, interceptors: a lateral boost, a hard sideways dash on the afterburner"),				More("heavy fighters, bombers, freighters: the string is taut, the view leads the nose a little and turns barely faster than the hull, so the gap stays small"),
				L("R (hold)", "fighters, heavy fighters, bombers (and a launched craft): drift -- the dogfight lets go, the hull barrels on along its way while the mouse swings it freely on its RCS thrusters (face backwards at a pursuer, rake a capital ship's length); let go and it snaps back to assisted flight in the new facing, boosting out. Settings > Controls > Drift key: Toggle makes R start the drift and R again end it"), // US-T165
				More("heavy fighters: weightier, more momentum; Shift is a hard boost that carries"),
				More("capital ships: A/D Q/E turn the hull, the mouse looks around"),
				L("Alt (hold), capital", "broadside stance: A/D Q/E slide the hull sideways and up/down instead of turning it, and the helm lays it beam-on to the locked hull (T) so the nearer flank's batteries bear"),
				L("Arrows", "reactor power, at any controls (a helm, a fighter, a gun, a station): Left / Up / Right a pip to weapons / shields / engines, Down an even share; with Shift, all it can spare to that one (the REACTOR panel shows the split; a gun draws on its hull's)"),
				More("weapons: reload, rate of fire, guns cool; shields: recharge and how much field they hold; engines: top speed, thrust, boost recharge"),
				L("Ctrl", "freighters, miners, in a port's approach: docking mode (docking speed, a gentle turn)"),
				L("Ring gate", "hyperlanes: fly through a ramp's ring gate to ride the lane"),
				L("1  /  2-9", "on the lane: 1 come off, 2-9 carry on at the off-ramp"),
				L("A/D  Q/E  (riding)", "steer about inside the lane's tube while it carries you at its speed (slip round traffic on the centre line); out past the tube's wall you leave the lane still at its speed, which bleeds off smoothly to your own"), // US-T196
				L("Shift", "interdiction (riding): a raider's field ahead, surge through"),
				L("Ctrl", "interdiction (riding): drop out short of the field"),
				L("Y", "back into the lane where you left it"),
				L("J (hold)", "tactical cruise: a stronger drive tier ramps up while it is held, with the nose straight and the throttle up, out of a fight; it snaps off at a hit, a fight or a hard turn"),
				L("Shift+J", "cruise drive, inside a system: spools a few seconds (longer the heavier the hull), then carries you along the nose at up to about 1000 c, for the world or station the nose is on (or the hull locked); the mouse steers, it slows as it closes and drops out off the target -- Earth to Mars in about half a minute. Shift+J again drops out; a fight, a hit or a world's gravity well drop it too"), // US-T127
				L("Enter  or  numpad Enter", "FTL (capital ships, survey hulls): beyond the system's outer rim, put the nose on another star, Enter to spool (hold it there); it engages for that star"),
				More("a jump reaches about 7 ly (survey drive 9, jump ring 5); farther stars are flown in even jumps with a stop in the void between each: the drive cools, then spools, then jumps on -- the HUD shows the stop, the cooldown and the spool"),
				More("small craft: dock a jump ring at a shipyard (F2, outfitting) to jump like a capital hull; most traffic still goes by the gates"),
				L("Tab  (void stop)", "between jumps: an active ping -- a ghost closing on you shows at once and may break off, but you are lit up a while and easier to find"),
				L("Shift+F3  (void stop)", "between jumps: run dark -- a quarter as easy to find, and a ghost closing may lose you, but no charge builds until Shift+F3 powers up again"),
				L("PgDn / PgUp", "plot a course to the next star or beacon (nearest first), or pick one on the galaxy map ([): ETA, fuel, wells on the way"),
				L("Del", "clear the plotted course"),
				L("End", "FTL autopilot: flies out beyond the rim, turns onto the star, spools and engages, time compressed (Settings > FTL); End again takes the helm back"),
				L("F3", "silent running: transponder off, heat damped -- only what your scanners hold is shown: blips, then class, then name"),
				More("your sensors are passive: they hear what others give off, as far as your scanners reach their signature; planets, moons and big rocks block the line of sight both ways"),
				L("Tab", "active ping: for 3 s your scanners reach three times as far and resolve twice as fine -- but you are LIT UP for 15 s, heard by every sensor in reach (hostiles hold you, witnesses see you); again after 20 s"),
				L("Shift+F3", "go dark: the reactor to minimum -- guns, shields and drive offline, transponder off, drifting on your momentum, your heat cooling toward nothing (hide behind a rock); Shift+F3 again powers up: a timed restart (8 s a fighter to a minute a capital), nothing online until it is done and your heat climbing back"),
				L("1, then 1-4", "derelicts and wrecks: match velocity alongside, 1 latch on, then 1-4 as the panel shows"),
				L("Shift+T", "tractor beam: locks on the nearest drifting cargo pod in reach (a blown-open ship's cargo, or what a threatened ship drops) and hauls it into your hold, then takes the next; again lets go. Reach and pull by hull class, about 3 km a strike craft to 15 a tug"), // US-T95
				L("Left mouse (hold), mining", "out of a fight at a miner's helm (or any hull with a mining laser fitted at a yard): the mining beam cuts the asteroid, drift rock or ice chunk down your aim into ore crates for the hold (I); a better laser reaches further and cuts faster, a rock works out, and cutting a settlement's rock under another flag is claim jumping")), // US-T264

			cs::New<HelpSection>("COMBAT",
				L("Left mouse", "guns, down the crosshair (watch the heat)"),
				L("T", "lock the hull under the crosshair; hold the crosshair on it to lock"),
				More("a locked hull gets the target card, bottom right: its class, flag and stance, range and closing speed, hull, the shield of each emitter's patch, its gun heat, its subsystems' health and the weak point you are going for"), // US-T256
				L("6", "at a fighter's controls, a destroyer or bigger locked: the next weak point to go for (marked on the target card, OPEN when your guns can reach it)"), // US-T256
				More("off-screen targets: an arrow on the screen's edge points the way to turn to each hostile, the locked hull (ringed), the selection, the hunters and a plotted course; the closer it is (against your guns' reach, the same at either scale), the bigger; red hostile, green your side, grey neutral, blue the course"),
				L("Right mouse", "homing and hive missiles once locked; dumb-fire rockets straight down the nose, no lock"),
				More("a vulcan rotary cannon spins up as it fires: short bursts from cold, a torrent once spun (watch the heat)"),
				L("Middle mouse", "EMP (knocks out a small craft's drives and shields)"),
				L("Ctrl", "drop a mine astern; an auto turret fires on its own"),
				L("Insert", "retune the phaser arrays: a narrow lance for hulls or a wide sweep for fighters and missiles"),
				More("a plasma cannon hits hardest close in"),
				L("Hold left mouse", "capital ships: look at a side and hold to build a broadside, let go to fire it"),
				More("the turrets fight on their own, at the locked hull first"),
				More("captain's orders, to the side you look at:"),
				L("1", "that battery at will / hold fire / volley / chain fire"),
				L("2", "gunners: focus the weakest shield patch / spread fire"),
				L("3", "brace"),
				L("4", "damage control focus"),
				L("5  6  7", "line ahead, line abreast, echelon"),
				L("8  9", "screen, engage at will"),
				L("0", "all batteries at will"),
				L("Alt+1-9", "fire a weapon group now (1-6 bow/stern/starboard/port/dorsal/ventral, 7 spinal, 8 missiles and torpedoes, 9 everything) on the locked target, wherever you are looking"),
				L("Ctrl+1-9", "that weapon group: chain fire (one mount a press) / volley (every bearing, loaded mount together)"),
				More("angle the hull to the enemy's line to make shots glance"),
				L("Shift+Insert", "big ships: run hot (guns and drive at full past the heat limit, the crew and systems cook) or throttle for the heat"),
				More("shoot a hull's radiators away to make it overheat"),
				L("Shift+Del", "capital ships: the helm rolls your weak shield patches away from the heaviest fire (the readout above the orders names them)"),
				L("B", "boarding (a fight aboard your hull, or one your marines are in): the panel of compartments and orders"),
				L("1  2  3  0", "with it open: pick a compartment, push for it / muster on it, hold, auto-resolve"),
				More("Bio Mass (the red-bracketed globules): every gun fires on it on sight, yours within 5,000 km; console sg.BioMass spawns one beside you")),

			cs::New<HelpSection>("TRADE",
				L("M", "market: Buy / Sell and Refuel appear while piloting near the shown settlement's body"),
				L("Shift+M", "port services: at a helm alongside a port, ask for docking clearance (by the port's trade with the hull and its owner's standing toward the pilot); docked, one screen with tabs (Market, Outfitting at a yard, Repair & Rearm, Contracts, Bounties, Fleet & property, Trade desk, Crew, News); flying off or leaving the helm undocks"), // US-T266
				More("exploration pays: a find past charted space (a far star, a first body, a void sight, an anomaly, a graveyard) leaves survey data aboard the hull; the market board at a cartographer (a lawful colony or station of 20,000 people or more) buys it, once per faction"), // US-T260
				L("I", "cargo hold (while piloting): click a crate, then a cell, to move it"),
				L("\\", "trade desk: runs from your price intelligence, brokers, trade news, charters, insurance, warehouses, the back door"),
				L("F7", "contracts board of the port in view: freight, passengers, colonists, escorts, patrols, surveys, bounties"),
				L("Home  (at a gas giant)", "at a helm by a giant's gas works: take its protection contract -- guard a skimmer platform for some orbits and beat off the raiders that come; pay and standing to the hull you hold and its pilot"), // US-T146
				L("Shift+Home  (by a skimmer)", "raid the nearest gas skimmer's tanks: its helium-3 and volatiles come aboard as stolen crates, and its owner's fuel supply is cut (piracy)"), // US-T146
				L("F2", "outfitting at a shipyard (or Outfit on the market): guns, shields and drives on the hardpoints; plate, racks, scanners, a brig in the internal space"),
				L("X", "news feed: shocks, price spikes, embargoes, battles and captures as they happen")),

			cs::New<HelpSection>("DIPLOMACY AND THE LAW",
				L("F12", "hail the polity of the ship locked or selected (or the patrol hailing you) and treat with it"),
				L("[", "the galaxy map's DIPLOMACY panel: pacts, tribute, fealty, war and peace"),
				L("T, then fire", "lock a ship, hold fire on it to attack (a crime if seen)"),
				More("patrols scan: hold still"),
				L("1", "pay a fine when hailed"),
				L("F4", "bounty board of the port in view: outlaws wanted there, their records, where last seen, who hunts them")),

			cs::New<HelpSection>("COMMS AND PILOTS",
				L("Shift+H", "comms (at any controls): hail any ship or station in range, as the unit you hold; Shift+H again closes"),
				L("Up / Down, Enter", "pick who to hail (colour: their stance toward you) and open the channel"),
				L("1-9", "say it: hail, ask about the area (threats by range and bearing off your nose), the news, to trade, buy a crate, ask about them, threaten"),
				More("threaten a ship for its cargo: by its pilot's nerve, its load and the odds it complies (the cargo comes aboard as stolen crates), runs or fights"),
				More("a hull that strikes its colours to your side hails you: 1-4 accept, plunder, let it go, or refuse quarter; a pirate taken alive by a lawful hull is turned in"),
				More("you are no one pilot: bounties, grudges and the story land on the pilot of the body you hold, who owns it all once you leave"),
				L("Esc / Left", "back to the contacts, then close")),

			cs::New<HelpSection>("EMBODY LIMITS AND INFLUENCE",
				More("no embodying in battle: a hull marked in a fight under way can't be taken (no starting a fight, then hijacking the enemy); take it before it engages, or once the fight is over; what is on a hull you already hold (its guns, EVA crews, launched craft) stays yours"),
				More("Influence (the meter top left) is the SpaceGod's own, never a pilot's: it grows with XP from interventions (acts a pilot owns once you leave, a squad talked down), missions (contracts) and careers you steered (a pilot you flew promoted)"),
				More("it gates what you can take: small craft and working hulls 1, gunships and corvettes 2, frigates and destroyers 3, cruisers 4, carriers and battleships 5, command cruisers 6, gate breakers 7; a defence platform 3, a gate fort 5; a named Epic entity (a feared outlaw's flagship, a pirate warlord or king, a captain at the top of their ladder) more"),
				L("Shift + the bar's last number (squad)", "at a band captain's helm, before a fight: order the squad to stand down (the bar's last entry); by your Influence and the captain's hold on the crews (rank, reputation, loyalty, history) they obey, hesitate, argue, or mutiny and put someone else at the head of the band"),
				More("a refusal shows on the HUD with its reason; the dev menu (Shift+F1, SpaceGod) grants Influence, lifts the limits for testing, and sets up the stand-down scenario")),

			cs::New<HelpSection>("FLEET AND COMMAND",
				L("F10", "your fleet: buy hulls, warehouses, station shares and claims; hire captains and give them standing orders"),
				L("'  ;", "the next seat in the fight in view (a ship's helm, a fighter or bomber, a station), straight from the one you hold: only on a hull you already hold (its guns, crews and craft); a hull already in the battle is refused"),
				L("/", "man the next gun on the hull or station you hold (or have selected): the view sits on the mount, down the barrel; past the last gun, back to the helm or station (Shift+/ is the launched craft's, below)"),
				L("Shift+1 ... Shift+0", "at a helm, the hull's own abilities, as its ability bar lists them from what it carries: a carrier launches a squadron of each class it has aboard (interceptors, fighters, bombers, heavy fighters, recon), recalls its wing, and calls it onto your locked hull, to guard the carrier, or to its own fights; a shuttle bay, a Breacher bay or boarding pods send a boarding shuttle, a Breacher or a pod salvo at the hull locked (T). In a fight only; a carrier you fly into a fight holds its wing aboard for you to launch"),
				L("Shift+/", "from a carrier's helm: jump into the craft it launched last (a fighter, bomber or boarding shuttle), again for the next one, past the last back to the carrier's helm"),
				L("Backspace", "at a gun: up to the helm of the hull it is on (or the whole station); in a launched craft, back to its carrier's helm"),
				L("Shift+Backspace (hold)", "self-destruct, at any controls: held a second it arms a countdown (5 s a fighter, 8 s a light hull, 15 s a capital, 20 s a station; a gun sets off its hull), pressed again it aborts; at zero the craft blows with everyone aboard, the blast worst on whatever it is touching"),
				L("F9", "take-charge picker: every seat on the hull in hand and around the camera (helms, fighters, stations, gun mounts, EVA workers), click one to jump in"),
				L("Shift+P", "EVA: out on the hull held, flown or selected (a capital ship or station in a fight) as its next repair worker; again for the next"),
				L("Shift+Y", "eject from a launched craft's cockpit: out in the suit (thrusters on W/S A/D Q/E, Shift), left mouse tethers onto a torpedo in reach, W walks it to the warhead, left mouse taps the safing, right mouse lets go"), // US-T166
				L("Shift+N", "mech: out of the bay of the hull held, flown or selected (a capital ship or station in a fight), you at its controls: W/S walk (Shift runs), A/D turn the legs, the mouse twists the torso and aims the arms, left mouse the autocannons, right mouse a missile salvo at the hostile nearest the aim; hold Space for the jump jets, off the plating and flying in open space (W/S A/D Q/E steer the push, Shift at full), let go to coast and the mag boots catch on any hull you reach; ' ; finds it again"), // US-T83
				More("as a worker: W/S A/D crawl the plating where you look, Shift the suit's jets; reach the bracketed damaged hardpoint (a shield generator, an engine, a turret) and the torch lights"),
				L("Left mouse (worker)", "weld: tap as the marker crosses the green zone; good welds raise the weld's quality and the repair runs up to 2.75x the team's own rate (1.25x just for your hands), a miss sputters the torch"),
				L("Backspace (worker)", "back inside, to the hull's helm or the station"),
				More("in a fighter: mouse aims, W/S throttle (assist off: raw thrust), Alt flip, left mouse guns, right mouse torpedoes"),
				More("at any seat the craft has the keys and mouse, the camera only follows it (the wheel zooms the chase); keys held through a switch wait to be let go"),
				More("at every seat a role panel down the left lists that role's keys, greyed when they cannot be used now"),
				More("co-op or PvP: a hull another player embodies shines in their seat colour (P1 amber, P2 cyan, P3 rose, P4 lime): a rim glow, a glint sweeping its plating, now and then a star flare, a twinkling glint when it is far; one running dark shines only inside visual range. The other side of a fight is fair game, players or not"),
				L("P", "hands the seat back to its AI and returns you to the free SpaceGod view")),

			cs::New<HelpSection>("SCREENS AND PANELS",
				L("H  or  F1", "this help screen (the bottom line's key list shrinks to \"H all keys\" when the screen is too narrow for it)"), // US-T155
				L("Shift+F1", "dev menu, pulled down over the view: spawn any hull, station or growth for any faction (Shift+F2 opens it there), enemies (pirates small, medium and large, a hostile navy, Bio Mass), the cruise drive and Cruise to, teleports to a world, station or star, helms and seats, the dogfight demo, a big battle to watch (US-T164), time warp and the two scales; type to filter, Enter or click runs, Tab steps the categories, Esc closes. The game runs on; the craft holds its heading"), // US-T126
				L("Shift+F2", "spawn menu (the dev menu on its Spawn tab), any time: every hull in the ship catalog (fighters, freighters and working hulls, warships and capitals), stations, pirate groups and Bio Mass growths; Faction steps who it is for (yours, a rival's, the independents', the pirates'); each has Spawn (beside you, flying its own AI) and Spawn and pilot (you at its helm). Spawned hostile to a ship near, it engages it at once"), // US-T186
				L("Showcase tour", "every feature in turn, chapter by chapter (main menu, dev menu > Showcase, or sg.Tour): Right / N next, Left / B previous, S skip the chapter, Space pause, 1-9 a chapter, O also each stop at the other scale, Esc ends it where it is"), // US-T147
				L("Tutorial", "a guided first run, played by doing (offered once on a profile's first New Game; main menu Extras or the Help screen replays it): each step a card with its keys that waits for you to do it; Shift+Enter skips the step, Shift+Esc ends the tutorial; it never takes any other key"), // US-T254
				L("Esc", "pause menu (straight away at the helm): Resume, Help, Save / Load, Settings > Display (resolution, window mode, VSync), Main Menu, Quit"),
				L("[", "galaxy map: every system, who holds it, gate traffic, war fronts, blockades and raiders, as far as the news goes; click a star to inspect, double-click to travel"),
				L("Shift+[", "system map: this system top-down, its worlds and orbits, settlements, stations, gas works, lanes and ramps, gates, belts, fields, nebulae, graveyards and the ships your sensors see; hover for a line on it, click to select, drag pans, wheel zooms; at a helm CRUISE THERE or plot the LANE ROUTE (the off-ramps' exits picked for you)"), // US-T258
				L("U", "HUD panels and readouts on / off (the names and markers on the view stay)"),
				L("Shift+U", "the bottom drawer (or its handle, bottom left), pulled up over the view: Navigation lists the star systems (travel, or plot the course at a helm) and this system's worlds, rings and stations (cruise there at a helm, else fly the view there); Ships in area lists the hulls about with range, faction, class and role, a click hailing one in range. Tab the tabs, Up / Down and Enter the rows, PgUp / PgDn the height, Esc closes"), // US-T200
				L("F11", "clear the screen: every bit of UI gone but the reticle in use; F11 again (or opening a panel) brings it all back"),
				L("F5", "quicksave"),
				L("F8", "quickload"),
				L("F6", "save slots"),
				L("Alt+F9 / F10 / F11", "only while capture runs: screenshot, start / stop recording, save the replay (instead of the picker, the fleet and clear screen; docs/CAPTURE.md)"),
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

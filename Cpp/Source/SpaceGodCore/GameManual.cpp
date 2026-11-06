// Port of Unity.SpaceGod Core/GameManual.cs (see docs/MIGRATION.md).

#include "GameManual.h"
#include "ControlsHelp.h"

namespace SpaceGod::Core
{
	cs::List<cs::Ref<HelpSection>> ManualChapter::Controls() const
	{
		// C# yields lazily; nothing depends on that, so the list is built eagerly.
		cs::List<cs::Ref<HelpSection>> result;
		for (const auto& name : ControlGroups)
			for (const auto& s : ControlsHelp::Sections())
				if (s->Title == name)
					result.Add(s);
		return result;
	}

	const cs::List<cs::Ref<ManualChapter>>& GameManual::Chapters()
	{
		static const cs::List<cs::Ref<ManualChapter>> value =
		{
			cs::New<ManualChapter>("Getting started", "What the game is, and the first minutes in it", Keys(),
				T("What this is",
				  "A living galaxy you can watch from above or step into anywhere. The solar system runs on its real orbits "
				  "and clock; on top of it, factions trade, build, fight wars, raid, make treaties and colonise, all on their "
				  "own. You can play it as a strategy game, a dogfighter, a capital-ship broadside game, a trader's career or a "
				  "diplomat's, or take nothing at all and let it play out."),
				T("The main menu",
				  "Continue loads the latest save, New Game starts at the current date beside Earth, Load Game opens the "
				  "saves, Profiles switches player profile (each keeps its own saves and settings), Settings opens every "
				  "tunable, Help opens this manual and Quit leaves."),
				T("Your first minutes",
				  "You start in free swim beside Earth, with Earth selected, at gameplay scale and real time. Swim with "
				  "W A S D, look with the right mouse button, and click anything to select it. Open the navigator to pick "
				  "a body or a ship, press Tab to cycle through ships, and B to jump to the biggest battle going. Select a "
				  "ship and press P to take its helm; P again hands it back to its crew."),
				T("Two ways to play",
				  "From the god view you watch, inspect and command (polities, fleets, your own house). At the helm you fly "
				  "one hull, or step into any seat in a fight: a ship's helm, a fighter's cockpit, a gun. Nothing pauses and "
				  "nothing loads when you move between them."),
				T("Finding your way in this manual",
				  "Pick a chapter in the contents on the left. Each chapter explains a part of the game and ends with the "
				  "keys for it; the last chapter, Every key, is the whole reference card.")),

			cs::New<ManualChapter>("The god view", "The free camera, time, scale and what the panels show", Keys("CAMERA AND GOD VIEW"),
				T("Swimming and focus",
				  "The camera swims freely: Shift to go faster, Ctrl for fine moves, the wheel for swim speed or zoom. Click "
				  "to select, double-click or F to focus on the selection and orbit it with the right mouse. Home shows the "
				  "whole system and the number keys jump to the planets."),
				T("The navigator",
				  "A tree of the system: bodies, fleets, docking rings and their queues, and everything in orbit. STAR SYSTEMS "
				  "at its top switches to another star: the camera pulls out, glides between the stars and falls into the "
				  "other system (Esc skips the trip). Every system runs all the time on the shared clock; only the one you "
				  "are in is drawn."),
				T("The details panel",
				  "For a body: distance, light time, orbital speed, period, eccentricity, gravity, and each settlement's "
				  "exports, shortages and defences. For a ship: status, speed, ETA, cargo and trade profit, or hull, shields, "
				  "lost hardpoints, fighters aboard and orders. The selected ship's flight path is drawn, lanes included."),
				T("Time",
				  "The clock starts at the current date and runs from real time up to a year a second. Space pauses (easing "
				  "to a stop), comma and period slow and speed the warp, and T brings it back to now. Watching a battle or "
				  "taking a helm drops to real time."),
				T("Gameplay and realistic scale",
				  "G switches scale. At gameplay scale worlds are drawn large and close so a system can be read at a glance; "
				  "at realistic scale everything drops back to its true size and distance, and most things become icons. "
				  "The simulation itself is the same at both."),
				T("What is drawn",
				  "Orbits, labels, hyperlanes and satellites each toggle on their own. Far or small things fade from model to "
				  "simpler model, to icon, to one marker for a fleet, so the view stays readable at any distance."),
				T("Light on the hulls", // US-T172
				  "Ships and stations are lit as space lights them: one hard star, whose light throws each hull's own shadows across "
				  "its plating, and the dark. The shadow side is never quite black: it takes the colour of the system's sky, a faint "
				  "cool rim picks out the silhouette, and near a world its lit face bounces light on to you. Behind a world, in its "
				  "shadow, the star goes out and a hull is drawn by its own lights: lit windows and panels, red nav lights to port, "
				  "green to starboard, a white strobe on top, engine glows. Small craft carry headlights, and a Void Station's "
				  "Embers light the station round them. It is the same at both scales."),
				T("The sky",
				  "Every system has a skybox behind it. Shift+K (or numpad *) moves the system in view on to the next sky and "
				  "Shift+Alt+K (or numpad /) back; each system keeps the sky you gave it, in your profile, Sol or any other. "
				  "Until you pick one, Sol shows the real sky (NASA's Deep Star Map, every star where it really is) and every "
				  "other system its own nebulae, made from its seed so the same star always looks the same. The skies: this "
				  "system's own nebulae; Real sky: NASA Deep Star Map; Real sky: ESO Milky Way panorama; Blue nebulae; Hazy "
				  "nebulae; Deep starfield; Ringed giant (8K panoramas from Space Spheremaps); and Bare starfield, the old black "
				  "sky with point stars. Settings > Graphics sets the default sky, its brightness and how finely seeded nebulae "
				  "are made. Your own 2:1 panoramas (.jpg or .png) dropped into the Skyboxes folder of the game's data folder "
				  "(on Windows %USERPROFILE%\\AppData\\LocalLow\\MindAttic\\Unity.SpaceGod\\Skyboxes) join the cycle as "
				  "'Your sky' the next time a game starts."),
				T("Inside a system's sky, and between the stars",
				  "A system's sky is two layers. The far one is the Milky Way, its band and bright bulge where they really are, "
				  "and it never moves. The near one is the system's own painting, not flat but a cloud about the size of the "
				  "system's outer rim: from inside it looks like the painted sky, as you fly toward the rim it thins and its "
				  "wisps stream past, and from outside it is a glowing cloud wrapped round a shrinking star. Past the rim is "
				  "open space under the Milky Way. Every star your jump drive can reach is a bright point in its true "
				  "direction over the sky, so the star you see is the star you aim at. As the drive spools the stars streak "
				  "and the sky smears into a tunnel; at full strength the system's sky gives way to the Milky Way, which is "
				  "the sky through the jump and at the void stops between jumps, and dropping out plays the tunnel backwards "
				  "into the next system's sky with a flash. One system is drawn with a hand-painted hero sky, 'Alien Space "
				  "Nebula 1 (Skybox)' by Jungle Jim (CC BY 4.0), its white-hot core lighting the system. The dev menu "
				  "(Shift+F1 > Travel) switches every sky between the cube (the painting at infinity), the sphere (the hero "
				  "painting everywhere) and the volume (the two layers, the default), flies a hull out through the rim and "
				  "back (Edge run), and previews the jump's tunnel and the Milky Way.")),

			cs::New<ManualChapter>("Flying", "Taking a helm, the flight models, propellant, lanes and gates", Keys("HELM AND FLIGHT"),
				T("Any ship, anywhere",
				  "Select a ship and press P, whether it is parked, in transit, on a lane, queued at a ring, landed or in a "
				  "battle. V spawns any hull you pick from a grid at the camera and puts you at its helm. P again hands the "
				  "ship back to its crew, who carry on from where it is."),
				T("A practice fighter",
				  "Shift+V (or Practice Fighter Here on the V screen) puts a fighter of your own side at the camera, in any "
				  "view and any system: deep space, beside a station, among a fleet or over a world. It starts just clear of "
				  "whatever is there and you are straight at its helm, so you can fly alongside other hulls to see how big "
				  "they are and practise the flight. Each system keeps one: pressing again brings the same fighter, mended "
				  "and refuelled, to wherever the camera is now, so practice never fills your saves with spare hulls. A "
				  "practice fighter lost, taken or still in a fight is left to it and a new one takes its place."),
				T("Newtonian flight",
				  "Every hull has mass, a main drive, weaker retro thrusters and manoeuvre thrusters, so it carries its "
				  "momentum. The mouse steers, W and S thrust, A D Q E strafe, Z and X roll, Shift is the afterburner. Flight "
				  "assist (on by default) is the compensated mode: the thrusters cancel drift and hold the velocity you ask "
				  "for, so the hull flies where it points, like a dogfighter in air. F turns it off for true Newtonian flight, "
				  "in a helm or a fighter seat alike: the hull keeps its momentum and turns free of its track, so you can "
				  "spin to fire behind you while sliding on, and W and S are raw thrust. To stop, turn and burn: hold Alt "
				  "and the nose swings onto the retrograde, then W burns the speed off; or brake on S, the retro thrusters, "
				  "slower but without turning. With assist off the HUD shows what each would cost, in seconds and km, the "
				  "same at either scale. Near a world its gravity pulls on the hull."),
				T("A flight model per family",
				  "Light fighters fly Star Conflict style: very agile, the nose snapping on and off a target, the track carved "
				  "after the nose, strafing on all six axes (W S, A D, Q E), and Shift held while strafing is a lateral boost, a "
				  "hard sideways dash on the afterburner; they turn tightest at their corner speed. Interceptors push "
				  "that further (their grip, strafe, corner speed and boost turn penalty, by speed or only while boosting, are "
				  "in Settings > Dogfighting, and enemy crews fly the same model); bombers are a steady torpedo platform. Heavy fighters (the gunship) fly Everspace 2 style: weightier, "
				  "with more inertia, they turn at one stiff rate at any speed, the nose a beat slower to start and stop than a "
				  "light fighter's, settle on the throttle and strafe through an ease you can feel, and boost hard: the surge "
				  "kicks in at once and carries through the target, bleeding off over a few seconds once let go. Capital ships ring an engine telegraph a step at a "
				  "time (Full Astern, Stop, Slow, Half, Full, Flank), turn best at Half and trace a turning circle; pitch, yaw "
				  "and roll are all free, slow and heavy. They fight with their broadsides, Age-of-Sail style: hold Alt for the "
				  "broadside stance and the turn keys slide the hull slowly sideways and up or down instead of turning it, "
				  "while the helm lays it beam-on to the hull you have locked, so the nearer flank's batteries bear as you "
				  "work along or across the enemy's line. Freighters, miners and the other work hulls fly heavy on what they "
				  "carry. With assist off, every hull flies plain Newtonian."),
				T("Every role at the helm",
				  "Every ship can be flown, and each is flown with the controls its work wants. Every fighter is pulled by a "
				  "string: the camera looks where the mouse puts it and the hull chases it, so the HUD shows two reticles, the "
				  "crosshair where you aim and the nose's boresight swinging over until it meets it, and the guns converge on the "
				  "crosshair as far as they gimbal (a craft launched from a carrier flies the same from its seat). A light "
				  "fighter's string is slack: the view swings anywhere at once, a full 180 to look behind you, and the hull "
				  "whips round after it, harder the further it has to go. A heavy fighter's, bomber's or picket's is taut: the "
				  "view leads the nose only a little and turns barely faster than the hull can, so the gap stays small. "
				  "Hold R in any of them to drift, as in Chorus: the dogfight lets go, the hull barrels on along the way it was "
				  "going while the mouse swings it freely on its RCS thrusters (you see them fire), so you can face backwards and "
				  "shoot what is chasing you, or rake a capital ship's length as you pass it; let go and it snaps back to assisted "
				  "flight in the new facing with a boost-out. Set the drift key to Toggle under Controls and R starts the drift, "
				  "R again ends it. A courier "
				  "flies a fighter's stick on the longest boost, to run rather than fight; an escort taken from a formation is cut "
				  "loose from it and flies as its class does; freighters and miners have a slower mouse, fly heavy on their load "
				  "and dock on Ctrl; capital ships ring the engine telegraph and turn on the keys while the mouse looks round. "
				  "Taking a helm names the role and its keys."),
				T("Flying a hauler",
				  "A freighter or miner is heavy, and heavier laden: what is in its hold and its tanks slows its drive, its "
				  "braking and its turn, so a full hull is noticeably slower than an empty one. Its retros are weak, and the "
				  "HUD shows the stop point, where full retro would bring it to rest and how long that takes, so start "
				  "braking early. Inside a port's approach, hold Ctrl for docking mode: the throttle asks only for docking "
				  "speed and the nose turns gently, for a clean docking. Its strafe is weak and its boost a short emergency "
				  "burn, not a dodge."),
				T("Cruise",
				  "Hold J flying straight at full throttle and the cruise drive spools up to many times normal speed. It "
				  "drops out if you take damage, enter a battle, throttle down or turn hard, and the guns cannot fire in it."),
				T("The cruise drive", // US-T127
				  "Shift+J crosses a system in tens of seconds. The drive spools for a few seconds (a fighter's in two and a "
				  "half, a carrier's in eight), then carries the hull along its nose: it climbs fast to up to about a thousand "
				  "times light's speed, and slows as it nears the world, station or locked hull it is bound for, dropping out "
				  "a short flight off it. It is bound for whatever world or station the nose is on (or the hull locked with T), "
				  "and you steer the whole way; turn off the target and it flies free until the nose finds another. Near any "
				  "world its gravity well holds it back, and too deep in one it drops out. Shift+J again drops out at once; a "
				  "fight, a hit or the drives knocked out end it too. The dev menu's Travel > Cruise to picks the target and "
				  "swings the nose onto it for you. The readout gives the speed, the target, how far and the seconds to the "
				  "drop: true kilometres and seconds, the same at the realistic and the game scale."),
				T("Propellant",
				  "Every change of speed is paid for in reaction mass, by the rocket equation, and a dry hull only coasts. The "
				  "status bar shows the tanks and the ship panel the delta-v left. Refuel on the market board at a port, or "
				  "call a tanker when low, at a call-out fee and a steep price."),
				T("Collisions",
				  "Hulls, stations, asteroids and debris have real volumes. A hit exchanges momentum and does damage scaled "
				  "to the hull's own mass, so a courier that rams a battleship is wrecked and the battleship scratched. "
				  "Bumps under 5 m/s do nothing."),
				T("Sensors and silent running",
				  "At the helm you see only what your scanners hold: an unknown blip, then a class, then a named, coloured "
				  "contact. Your own signature jumps when the drive lights. F3 runs silent: transponder off and heat damped, "
				  "though a burn still shows."),
				T("Passive, active and dark",
				  "Your sensors are passive: they hear what others give off, and nobody hears them. Tab sends an active ping: "
				  "for 3 s your scanners reach three times as far and see twice as fine, but for 15 s you are lit up, a flash "
				  "every sensor in reach picks up at once, so hostiles hold you and witnesses see you; the emitters need 20 s "
				  "between pings. Shift+F3 goes dark: the reactor drops to minimum, guns, shields and drive go offline, the "
				  "transponder stops and the hull drifts on its momentum while its heat cools toward almost nothing (bigger "
				  "hulls hold their heat longer). Shift+F3 again powers up, a timed restart (8 s in a fighter, up to a minute "
				  "in a capital ship) with nothing back until it is done and your heat climbing all the while. Planets, moons "
				  "and big asteroids block the line of sight both ways, so the way to vanish is dark, behind a rock, and cold. "
				  "The SENSORS panel above the reactor's shows which: PASSIVE, LIT UP, DARK and cooling, or RESTARTING."),
				T("Hyperlanes",
				  "Fly through a ramp's ring gate to ride the lane at up to 4% of light, the clock running fast. On the lane, "
				  "1 comes off at the next ramp and 2-9 carry on along another lane. Raiders can lie beside a lane with an "
				  "interdiction pulse: Shift surges through, Ctrl drops out short, or ride into it and fight. Y gets back "
				  "into a lane where you left it."),
				T("Docking and landing",
				  "Merchants and couriers land on populous worlds through docking rings over their cities: approach, queue, "
				  "descend tail-first and trade. Warships hold orbit."),
				T("Derelicts and wrecks",
				  "Come alongside a derelict or wreck, match velocity (within about a kilometre and 20 m/s) and press 1 to "
				  "latch on. Fight off any raiders aboard, then salvage cargo, strip a component or claim the hull with a "
				  "prize crew."),
				T("Tractor beam and cargo pods",
				  "Blow a laden ship open and most of the cargo it carried spills as drifting pods (amber canisters with a "
				  "faint blinking marker; red, stolen goods), the rest staying in the wreck's hold for latching. Cargo a "
				  "threatened ship drops to you over comms floats out the same way. At any helm, Shift+T locks the tractor "
				  "beam on the nearest pod in reach and hauls it in; alongside, its crates go into your own hold, the haul "
				  "goes on the record of the pilot you are flying, and the beam takes the next pod in reach by itself. "
				  "Shift+T again lets go; it slips if you run off past half again its reach, and stops for a full hold. "
				  "Reach and pull go by hull class: a strike craft about 3 km, light hulls 5, freighters and destroyers 7-8, "
				  "capital ships 12, the tugs 15 and hardest; heavy pods come in slower. Pirates who win a fight tractor in "
				  "their share of the dead hulls' pods. The dev menu's Spawn > Cargo-laden wreck (or console sg.Tractor wreck) "
				  "blows a laden freighter apart just ahead of you.")),

			cs::New<ManualChapter>("Ships: strike craft", "Fighters, interceptors, bombers, heavy fighters, recon and boarding craft", Keys(),
				T("Space superiority fighter",
				  "The balanced workhorse: guns and four missiles, the carving Star Conflict style turn. Flown from a "
				  "carrier's squadron or on a navy patrol wing."),
				T("Interceptor",
				  "Fast, the tightest turn and the hardest strafe, light guns and dogfight missiles. It hunts fighters and "
				  "goes first for one closing on its own bombers."),
				T("Bomber",
				  "Slow and armoured, with two torpedoes for capital ships. Its torpedo does three times the damage on bare "
				  "hull where the shield is down, and two through an open reactor vent bring down a battleship. It needs "
				  "an escort."),
				T("Heavy fighter / gunship",
				  "Tougher and stiffer, guns and four missiles; a carrier with a fourth squadron flies them, and gunships "
				  "lead navy wings and escort convoys."),
				T("Recon / EW fighter",
				  "A pair flies from every carrier: they hold off the enemy capital its bombers are going for, jam every ship "
				  "near it (its gunners fire less accurately) and paint it, so the bombers' torpedoes steer harder onto it."),
				T("Boarding shuttle",
				  "Armoured and unarmed; a carrier keeps two. It carries a team of 24 marines to a disabled hull or a "
				  "silenced station or gate fort, latches on and lets them in through the airlock, so the prize is taken intact. "
				  "Each is a craft of its own, docked in its carrier's shuttle berths: it comes home as itself, keeps its "
				  "pilot and its damage until a yard mends it, and only one lost is built anew."),
				T("Flight decks",
				  "Carriers launch their craft down fore-and-aft flight decks and out of the bow. Craft come home by flying "
				  "the glide path in through the stern, are refuelled, rearmed and repaired, and launch again. At the stick "
				  "you can land on a friendly deck the same way; the HUD gives the guidance."),
				T("Dogfighting keys",
				  "Left mouse guns, right mouse the second trigger's weapon group -- homing missiles, hive missiles, dumb-fire "
				  "rockets or torpedoes (1 cycles them; the readout names the group and its rounds) -- T locks the hostile "
				  "nearest the crosshair, Ctrl throws flares and chaff, 7 / 8 / 9 move reactor power to weapons, shields or "
				  "engines and 0 evens it (the arrows too, as at any controls), 2-5 order your wingmen, and 6 picks a capital's next weak point."),
				T("Off-screen arrows",
				  "At any controls, whatever is worth turning to and off the screen gets an arrow on the screen's edge, pointing "
				  "the way to turn: every hostile in the fight (the nearest named by its range), the hull you have locked, ringed "
				  "and named, what you have selected, the hunters running you down, and a plotted course's next star. An arrow "
				  "grows as its target closes, measured against four times your longest gun's reach (the scanners' reach for the "
				  "selection out of a fight and for the course) in true kilometres, so it reads the same at the realistic and the "
				  "game scale. Red is hostile, green your own side, grey anyone else, blue the course."),
				T("Reactor power",
				  "Whatever you take the controls of -- a ship's helm, a fighter, a gun (which draws on the hull it is mounted on) or "
				  "a station -- shares its reactor three ways, six pips in all and at most four to one system, two each at an even "
				  "share. Left, Up and Right move a pip to weapons, shields or engines, from whichever other system has the most; "
				  "Down evens it; Shift with Left, Up or Right gives one system all the reactor can spare (four, one, one). Each pip "
				  "over two runs that system a quarter harder, each pip under a quarter softer: weapons reload (and so fire) faster "
				  "and your guns cool faster; shields recharge faster and hold an eighth more field a pip (power taken off them "
				  "bleeds the excess away); engines give more top speed and thrust and refill the boost faster. The REACTOR panel "
				  "at the lower right shows the split. A hull keeps the split you set, saved with it; under its own crew it runs "
				  "even, as designed.")),

			cs::New<ManualChapter>("Ships: capital ships", "Carriers, battleships, command cruisers and gate breakers", Keys(),
				T("Carrier",
				  "Has its own FTL drive. Launches and recovers fighter, bomber, recon and boarding-shuttle squadrons, and can "
				  "carry frigates clamped to its hull. Losing it strands its wings, which look for another deck or a friendly port."),
				T("Battleship / dreadnought",
				  "Built around two spinal mounts down the keel, a railgun and a coilgun, each fed by a spinal power plant; a "
				  "yard may fit a tachyon lance or plasma spinal cannon instead. It holds the rear line, sniping from the "
				  "extreme reach of its spinal guns with its bow on the mark and backing from anything that closes."),
				T("Command cruiser / battlecruiser",
				  "Gives up most of a cruiser's guns for a sensor array, a communications suite and targeting computers. "
				  "Ships on its side within reach find targets faster, shoot truer and concentrate on its mark; lose it and "
				  "they are thrown into confusion for a while, so enemy gunners go for it first."),
				T("Gate breaker",
				  "The siege capital: the heaviest armour afloat, the slowest drive, plasma siege guns, the biggest marine "
				  "barracks, boarding shuttles and engineering teams. A war fleet with one takes an enemy jump gate intact "
				  "the day its forts are silenced. It always has its own FTL drive."),
				T("Capital handling",
				  "All capitals fly the engine telegraph and turn slowly; a destroyer comes about in about 6 seconds at Half, "
				  "a carrier in 18. Big ships shed heat through radiator panels that gunners can shoot away."),
				T("How a fleet action flies", // US-T164
				  "A battle is never two parked lines. Fighters on a capital fly the strafing run: they burn hard for a point "
				  "off one end of the hull, cut the drive and coast down its whole length with flight assist off, the nose "
				  "kept on the plating so the guns rake it end to end, then flip, burn and come round for the next pass. "
				  "Fighters on fighters chase tails, pull loops through the merge and throw rolling breaks. Corvettes, "
				  "frigates and destroyers make passes at speed, their noses weaving. Capitals steam across the enemy's line "
				  "broadside-on at their slow turn rate and come about from time to time, or when the side they have been "
				  "showing is worn, so the fresh broadside and its shield arrays take the fire; hits land on whichever "
				  "shield patch faces them, so the turn really spares the damaged side. The fleets open about 14 km apart, "
				  "so contact comes at once. Every fighter in a squadron flies its own runs on a capital (the fighters, heavy "
				  "fighters and bombers go for the hulls, the interceptors for the craft round them), and with their missile "
				  "racks empty they fight on with their guns: only the badly hurt, bombers out of torpedoes and craft with no "
				  "gun left fly home, and a flight deck turns one round in about 10 s. Dev menu, Combat: Start a big "
				  "battle and watch.")),

			cs::New<ManualChapter>("Ships: escorts and sub-capitals", "Cruisers, destroyers, frigates, corvettes and gate pickets", Keys(),
				T("Cruiser",
				  "Long-endurance patrol on the gates and lanes, needing no support fleet. Can carry phaser arrays. No FTL drive."),
				T("Destroyer",
				  "The anti-fighter and point-defence screen, with a fixed spinal railgun and phaser arrays it can tune between "
				  "a narrow lance and a wide sweep. Leads destroyer wings and lane picket wings."),
				T("Frigate",
				  "A missile and torpedo boat, the fastest warship hull and the thinnest. It fights by the alpha strike: "
				  "burns in holding every launcher, looses them all at once, flips and burns back out while they reload. "
				  "No FTL; it crosses by the gates or clamped to a carrier."),
				T("Corvette",
				  "System defence with no FTL: deep tanks, a long-reach patrol search suite and a brawler's guns. Every navy "
				  "stations corvettes at its ports; they ride the home system's lanes as the local law, hailing and scanning "
				  "traffic and running down smugglers and pirates."),
				T("Gate picket",
				  "A small, sensor-heavy hull posted just inside each gate a navy holds. It sees hostiles coming through the "
				  "gate or dropping out of FTL nearby and calls a group to meet them, so raiders shoot it first."),
				T("Lane interdictor",
				  "Not a class but heavy equipment: an interdiction field projector fitted into any hull big enough (a "
				  "destroyer, cruiser or converted freighter). It pulses ships out of a lane. Every raider band sails with "
				  "one, and navies fly lane picket wings with one.")),

			cs::New<ManualChapter>("Ships: logistics and civilian craft", "Freighters, haulers, couriers, tugs, tenders and private craft", Keys(),
				T("Bulk freighter",
				  "A truss with its engine astern and cargo pods and fuel spheres clamped along it; a yard adds or removes "
				  "pods and spheres. It never lands, carries no armour or weapons (its escorts carry the guns), rides the "
				  "lanes at a bulk toll and is slow to speed up and slow down off them."),
				T("Ore haulers and couriers",
				  "Ore haulers carry raw ore between works and smelters on a weak drive; couriers are small and fast and "
				  "carry urgent cargo and surveys. Both land at docking rings."),
				T("Ore tugs and capture tugs",
				  "An engine with claws. Ore tugs push rocks and comet ice hundreds of metres across over weeks, through "
				  "open space or on a heavy lane; capture tugs bring smaller rocks in to a refinery's grinders. Each is a "
				  "real hull you can take the helm of, and raiders can seize it only where it is."),
				T("Lane tenders",
				  "Unarmed maintenance hulls that keep a lane's rings powered and aligned. A lane with nobody tending it "
				  "wears, rides slower, stalls and finally goes dark, so tenders are a prime target in war."),
				T("Colony ships, convoys and supply hulls",
				  "Colony ships carry settlers on their voyage's timetable; navies run supply convoys of tankers and colliers "
				  "to their fleets; belt works send material convoys to their depots. All are real hulls that fly the trip."),
				T("Private craft",
				  "Yachts, survey ships, deep-space explorers and prospectors go where they please. Every inhabited port "
				  "also has its local traffic: buses, taxis, skiffs, runabouts and tourers flying round its own planet, "
				  "never in a fight.")),

			cs::New<ManualChapter>("Combat and boarding", "Battles, broadsides, weapons, shields, surrender and boarding", Keys("COMBAT"),
				T("How a battle starts",
				  "Wherever hostile forces meet under orders that put them there: groups, merchants, a port's defence "
				  "platforms. Flying near hostiles starts or joins one. Battles are fought in a local space tens of "
				  "kilometres across, every hardpoint picking its own target."),
				T("Weapons",
				  "Railgun and gun rounds fly a lead-compensated line and land only when they arrive; beams sweep onto their "
				  "target; missiles and torpedoes home. Phaser arrays tune between a lance for hulls and a sweep for fighters "
				  "and missiles; plasma cannons hit hardest close in; the tachyon lance lands the instant it fires. Point "
				  "defence shoots down missiles and fighters. Every weapon aims within a fire cone in full 3D. A yard also fits "
				  "a vulcan rotary cannon (a short-ranged gatling that spins up from a quarter of its rate to a torrent of "
				  "tracers, and runs hot), hive missiles (on the lock, each round splits after a second into eight homing "
				  "seekers that point defence can thin) and dumb-fire rocket pods (salvos of four straight down the nose, no "
				  "lock, hitting only what they fly into); the homing missile is the missile launcher every warship carries."),
				T("Shields and armour",
				  "Shields come from emitters on the hull, each covering the patch around it; a patch shot down is holed while "
				  "the rest stays covered, so gunners go for the weak spot. Shots that meet the plate slanted glance off, so "
				  "angling the hull to the enemy's line pays. A warship's bridge is buried at its centre."),
				T("Broadsides at a capital's helm",
				  "Look at a side and hold left mouse to build a volley with every gun that bears, release to fire. The number "
				  "keys are the captain's orders: battery orders, focus or spread fire, brace, damage control focus, the "
				  "formation of your own warships, and all batteries at will. Shift+Delete has the helm roll weak shield "
				  "patches away from the heaviest fire."),
				T("Heat",
				  "Big ships shed heat through radiators. Lose enough and the crew throttles the guns and drive; Shift+Insert "
				  "runs hot instead, at full past the limit, at a cost in crew and systems."),
				T("Hails, surrender and wrecks",
				  "In a battle the number keys answer hails: to a pirate demand, 1 hands over the cargo and 2 refuses; to an "
				  "enemy that strikes its colours, 1 demands its cargo, 2 takes the ship and 3 lets it go. A destroyed ship "
				  "leaves a wreck with its cargo as salvage, and its survivors in escape pods that whoever finds them decides the fate of."),
				T("Boarding",
				  "Every hull has an interior of compartments with crew and marines in them. A disabled ship can be boarded "
				  "through the airlock, by boarding pods, by a carrier's shuttles or by a Breacher round fired from a Breacher "
				  "bay. The fight goes compartment by compartment; holding the bridge and engineering takes the ship. While "
				  "your hull is being boarded, or your marines are boarding, B opens the boarding panel: pick a compartment "
				  "and push for it or muster on it, hold, or auto-resolve."),
				T("Boarding with allies",
				  "Marines of allied factions on the same side of a battle can board one hull together. They fight its "
				  "defenders as one party, as well as the mix of them who landed, and your orders from the boarding panel "
				  "command all of them. A faction on the other side still cannot land on a hull someone is boarding. Once the "
				  "hull is taken, each ally's claim is the fighting strength it sent aboard. The cargo is split by claim. The "
				  "hull goes to the largest claimant that can pay the others their share of its worth, and if none can, it "
				  "is sold to the breakers and the money is shared."),
				T("Taking any seat",
				  "You are the SpaceGod, never any one ship or pilot: you embody whatever you choose and let it go again. Select "
				  "it (or point at it) and press P: a ship's helm, a hull or station in a fight, a launched craft. F9 lists every "
				  "seat near you to click into. From the god view ' jumps into the nearest seat in the fight in view and ' and ; "
				  "step through the rest; / mans the next gun; Shift+P puts you out on the hull as one of its EVA repair workers. "
				  "P again lets go to the free SpaceGod view. What you do while embodied is that unit's doing: its faction and "
				  "its crew keep the credit and the blame after you leave."),
				T("Other players: allies or rivals",
				  "In co-op every player embodies what they like, and nothing says you must fly together: the side of a fight "
				  "a unit is on decides who it shoots, players or not, so two players on opposite sides fight each other's "
				  "hulls, every shot resolved the same on every machine. A hull another player embodies catches the eye: a rim "
				  "glow in their seat colour (P1 amber, P2 cyan, P3 rose, P4 lime), a slow glint of light sweeping its plating "
				  "and now and then a star-like flare; far off, when the hull is a few pixels, a twinkling glint the same size "
				  "on any screen. Your own does not shine for you. A hull running dark (silent running, or its reactor down) "
				  "shines only inside visual range, the distance at which the eye makes out the hull at all (about 3,400 of "
				  "its own lengths). The co-op roster shows each player in their colour and how they stand to you by the units "
				  "you both hold (ally, neutral, hostile); a rival's selection stays theirs, and a rival running dark out of "
				  "sight has no marker. The console command sg.Shine fakes another player on the hull selected or nearest, "
				  "to see it alone (sg.Shine dark, sg.Shine clear)."),
				T("The Bio Mass",
				  "A space-dwelling lifeform and an infection: pulsing globules of flesh, bone, tissue, mouths and hair that "
				  "seep into every system from the dark, drift, swell and creep toward the jump gates to spread. Everything "
				  "shoots it on sight. Every star, planet and moon carries orbital guns, embedded and on satellites round it, "
				  "that open fire the moment a globule comes in reach, with the defence platforms of the settlements there; a "
				  "jump gate's guns and forts burn it on its approach, and the far gate fires again the moment one comes out. "
				  "Every armed hull of every faction engages it within 5,000 km, yours too: fly at it and your guns fire. It "
				  "eats the wrecks it touches and grows, gnaws at a hull it touches and eats into a station it reaches, and a "
				  "big one buds in two, but each system holds only ten and none grows past a few megatonnes. The HUD brackets "
				  "every globule in red with its size, health and range, and warns of one within 2 million km. The console "
				  "command sg.BioMass spawns one beside your hull, at game scale (sg.BioMass real for realistic).")),

			cs::New<ManualChapter>("Trade", "Markets, cargo, contracts, the trade desk and outfitting", Keys("TRADE"),
				T("The economy",
				  "Sixteen goods are mined, made and eaten across the settlements, each priced from its own stock against what "
				  "it uses. AI freighters carry whatever earns most per day, so goods flow from surplus to shortage, and "
				  "shocks (mine accidents, blights, strikes, blockades, embargoes) move prices for real."),
				T("Buying and selling",
				  "Near a settlement's world the market board (M) has Buy and Sell, trading whole crates at the prices and tax "
				  "AI traders pay, and Refuel. I opens the grid cargo hold: click a crate, then a cell, to move it."),
				T("Contracts",
				  "Every lawful port posts jobs from what the system needs: freight, passengers and colonists, escorts, "
				  "patrols, surveys and bounties (F7). Completing one pays and raises standing with the patron; failing one costs it."),
				T("Gas giant harvesting", // US-T146
				  "Every gas giant has a gas works: three to six skimmer platforms in low orbit, each a tank farm whose skimmers "
				  "dive into the cloud tops and climb back laden (a plume of glowing gas runs from each platform down to the "
				  "clouds), and a tanker for each platform that carries a full tank to the port that wants it most, the owner's "
				  "own first. They scoop helium-3, the fusion fuel every port sells for drives and navies' supply ships draw, and "
				  "hydrocarbons and nitrogen, the volatiles: a Jupiter-like giant more helium-3, an ice giant more volatiles. "
				  "Raiders prey on them: pirates, the Bio Mass where it drifts near, ghost ships at the outermost giant beyond Sol, "
				  "a power at war with the owner. A platform raided loses its tanks and is down for days, a tanker taken loses its "
				  "load; either cuts the owner's fuel supply and prices climb where the gas no longer comes. Each works posts "
				  "protection contracts: at a helm by the giant, Home takes the best one, to guard a platform for a number of its "
				  "orbits (a few hours each). Waves come during it, raiders, ghosts, warships or Bio Mass, beside you: beat them "
				  "off before they gut the platform, and stay at the giant, for a wave that finds the platform unguarded fails the "
				  "contract. The pay goes into the purse of the hull you hold and the deed into its pilot's story, the standing to "
				  "that pilot's own record and goodwill to their colours, never to you. Shift+Home raids the nearest skimmer "
				  "instead: its gas comes aboard as stolen crates, and it is piracy on the pilot's record. The dev menu (Shift+F1) "
				  "has Travel > Go to a harvester, and Combat > Start a protection contract, Harvester wave now and Raid a harvester."),
				T("The trade desk",
				  "The backslash key opens it: the runs your known prices add up to (prices you saw, your captains report or a "
				  "broker sells, each ageing), trade news, time charters of your hulls to the companies, insurance, warehouses "
				  "and the back door, where smugglers buy what the open market will not, at the risk of customs."),
				T("Outfitting",
				  "At a shipyard F2 (or Outfit on the market board) opens the hull's hardpoints and internal space: guns, "
				  "shields and drives on the hardpoints; plate, cargo racks, scanners, a brig, marine habitats and more inside. "
				  "Every part is bought from that yard's market, and every tonne costs acceleration."),
				T("The news",
				  "X opens the news feed: shocks, price spikes, embargoes, battles and captures as they happen, with news from "
				  "other systems as old as the gates made it.")),

			cs::New<ManualChapter>("Diplomacy and the law", "States, treaties, crime, bounties and patrols", Keys("DIPLOMACY AND THE LAW"),
				T("States and opinion",
				  "Every state holds an opinion of every other, drifting with wars, pacts, common enemies and strength. They "
				  "sign non-aggression pacts, trade pacts, alliances, tribute and vassalage, and the AI puts deals to you each month."),
				T("Treating with a state",
				  "F12 hails the polity of the ship you have locked or selected and opens the envoy; the galaxy map's "
				  "DIPLOMACY panel does the same for any state. Each offer shows how it would be taken. Breaking a pact is "
				  "noticed by everyone."),
				T("Crime",
				  "Attacking a ship you are not at war with, piracy, smuggling, trespass and running from a scan are crimes, "
				  "but only if someone's scanners saw them. A crime posts a bounty and lowers your standing, which sets your "
				  "prices and whether a port lets you dock."),
				T("Patrols and scans",
				  "Navy patrols and corvettes hail you in their space for a cargo scan: hold still. Wanted, you are ordered to "
				  "heave to and pay (1) or be interdicted. Fines can be paid at any of a faction's ports."),
				T("Bounties and outlaws",
				  "Every raider captain has a record and a price. The bounty board (F4) lists who is wanted, where last seen "
				  "and who hunts them. A bounty pays in full alive (board the flagship and hand the captain over) and half "
				  "dead, on proof from close to the wreck. Bounty hunters come for you too once your price is high enough.")),

			cs::New<ManualChapter>("Comms and pilots", "Hailing ships and stations, piracy by radio, and the people you meet", Keys("COMMS AND PILOTS"),
				T("Hailing",
				  "Shift+H opens comms at any controls. The contacts are every hull your sensors hold in hailing range, and the "
				  "stations near you, nearest first, coloured by how they feel about you: green friendly, white neutral, amber "
				  "wary, red hostile. Pick one and open the channel; then say something with 1-9 or a click. A raider lying "
				  "dark may answer with nothing but static."),
				T("What you can say",
				  "Ask about the area and they tell you what their own side's sensors hold that would go for you, with the "
				  "range and the bearing off your own nose (\"pirates, 12 klicks out at bearing 104 mark 12\"), any fight on "
				  "nearby, or the nearest friendly patrol. Ranges are true at either scale, the same the HUD shows. Ask "
				  "the news for the latest the system's log carries. Ask to trade: a freighter names a price for what is in "
				  "its hold (its pilot's greed and how it feels about you set it) and sells you a crate at a time; a station "
				  "tells you what it is long on and what it pays well for. Ask about them and they tell you their life."),
				T("Piracy by radio",
				  "Threaten a ship for its cargo and its pilot decides, by their nerve and greed, what they carry and the real "
				  "strength of both groups: they dump it (it comes aboard as stolen crates, as far as your hold has room), "
				  "they run (and you can run them down), or they turn to fight. Robbery is piracy, booked by whoever saw it. "
				  "A hull losing a fight strikes its colours and hails you: accept what it offers, plunder it, let it go, or "
				  "refuse quarter. A pirate taken alive by a lawful hull is turned in."),
				T("Raiders on the trade routes",
				  "Pirate bands lie in wait beside the lanes and off the ports for loaded freighters. They pull one out, strip "
				  "its shields and engines, and once it lies dead they demand its cargo; it jettisons the hold, their drones "
				  "collect it, and they leave. That cargo never reaches the market it was bound for. The freighter runs while "
				  "it can and calls for help the moment it is set upon: the nearest armed navy patrols at either end of the "
				  "lane, or parked at the same world, answer, and arrive a minute or a few later depending on how far they "
				  "have to come. Raiders who see more guns arrive than they bargained for break off before they have the "
				  "loot."),
				T("You are no one",
				  "The SpaceGod is never one pilot. Whatever you say and do is said and done by the unit you hold, and the "
				  "consequences land on that pilot, that hull and that faction: the bounty, the grudges, the bad blood between "
				  "factions. Leave the body and the pilot is left holding it all, and believes every bit of it was their own "
				  "idea. Nobody out there knows you exist."),
				T("Named pilots",
				  "Anyone you hail, fly, fight or rescue becomes a named character with a past read from the system's own "
				  "history: where they were born, who they work for, the hulls they flew and lost, the battles they lived "
				  "through, their grudges. Their careers move on what they do: a pirate fighter rises by renown, to lieutenant "
				  "only by betrayal, to captain, warlord and pirate king; one taken alive goes to a detention centre, then "
				  "rehabilitation, fighter school, a pilot's posting, officer, and at last captain of a capital ship of their "
				  "own. The comms panel shows their age, home and next step.")),

			cs::New<ManualChapter>("Embody limits and Influence", "What the SpaceGod may take, how its reach grows, and ordering a squad through its leader", Keys("EMBODY LIMITS AND INFLUENCE"),
				T("No embodying in battle",
				  "A hull marked in a fight under way can't be taken. You can't start a battle and then jump into the enemy "
				  "to win it. Take a hull before its squad engages, while it is still closing, or once the fight is over. "
				  "What is on a body you already hold stays yours in a fight: its guns, its EVA repair crews, the craft it "
				  "launched and their carrier. A refused take says why, in a red banner under the news."),
				T("Influence",
				  "Influence is the SpaceGod's, never a pilot's. The meter at the top left shows its level (Whisper, Murmur, "
				  "Voice, Presence, Hand, Will, Dominion, Sovereign, Ascendant, Godhead) and the XP to the next. XP comes "
				  "from interventions: 10 for a life you reach into for the first time, 20 for each act its pilot owns once "
				  "you leave, and 25 for a squad talked down. Missions (contracts carried through) and careers you steered (a "
				  "pilot you flew, promoted) add more. A pilot's reputation, crimes and story stay with that pilot. Only the "
				  "reach is yours. One Influence holds for the whole galaxy and travels with you between the stars. In co-op "
				  "every seat shares it, because together you are one SpaceGod."),
				T("What it lets you take",
				  "Small craft and working hulls (fighters, shuttles, freighters, miners, couriers, tugs, tenders) ask "
				  "Influence 1. Gunships and corvettes ask 2, frigates and destroyers 3, cruisers 4, carriers and battleships "
				  "5, command cruisers 6 and gate breakers 7. A defence platform asks 3 and a gate's fort 5. One gun or one "
				  "repair crew asks two less than its hull. A named Epic entity asks more: the flagship of a feared outlaw "
				  "captain asks at least 6, and the hull of a pirate warlord or king, or of a captain at the top of their "
				  "ladder, at least 5. A practice hull (Shift+V), a hull you spawn (V), a loaded save's helm and a hull "
				  "carried on through a lane or FTL are yours already and never refused."),
				T("Ordering through a leader",
				  "Take a raider band's captain while the band is closing on a mark, or lying in wait, and the helm's bar ends "
				  "with \"Squad: stand down\". Whether the crews comply is theirs to decide. Your Influence weighs in. So does "
				  "the captain's hold on them: rank, renown, how loyal a captain they have been (honour, warmth, no spite, no "
				  "betrayals behind them) and the fights lived through together. Against that pulls the prize: a lone "
				  "freighter riding low pulls hardest, and every order they have already argued down makes the next harder. "
				  "They obey and break off for half a day. They hesitate, holding fire for 45 s, and you can order again. "
				  "They argue and keep closing. Or they mutiny: the band's strongest captain deposes yours and goes in at "
				  "once. Like everything a body does, the order is its captain's own act once you leave. It is a squad "
				  "order, given before a fight. Once the squad has engaged it is too late."),
				T("Testing",
				  "The dev menu (Shift+F1) has a SpaceGod tab. It grants Influence and lifts the limits for testing (never "
				  "saved). It also sets up the stand-down scenario: a pirate band closing on a lone freighter, with you at "
				  "the captain's helm. The console command sg.StandDown does the same.")),

			cs::New<ManualChapter>("Fleets and command", "Your house, your fleet, and commanding a polity", Keys("FLEET AND COMMAND"),
				T("Your house",
				  "F10 opens your fleet: buy hulls at a lawful yard or take the one you fly, hire captains and give them "
				  "standing orders (trade routes, mining a claim, escorts, patrols, free trading). Buy warehouses, station "
				  "shares and mining claims. Every credit is on the books, and your house is ranked by what it is worth."),
				T("Commanding a polity",
				  "On the galaxy map's COMMAND panel any polity with a navy can be steered: a production focus, a build plan "
				  "and a research line, and objectives for one squadron, a system's groups or the whole navy (take, hold, "
				  "blockade, escort, explore). Without a directive a polity runs itself."),
				T("How navies fight",
				  "Gate fleets march through the gates and are met at the exits; jump strike groups (a carrier and FTL capitals "
				  "with escorts docked) drop out on a system's flank. Navies keep patrol wings, supply convoys, corvettes and "
				  "gate pickets, and send groups to clear raiders off their gates."),
				T("Taking a seat in a fight",
				  "' jumps into the fight in view, ; and ' step through its seats (every helm, every fighter and bomber aloft, and "
				  "every station fighting there), / mans a gun, P returns you to command with the view and warp you had. A switch "
				  "from one seat to another is instant: the old one goes back to its crew, the new one is yours that frame, and "
				  "the view is there in a blink. Each key has one owner at a time: at a seat the craft has the keys and the mouse "
				  "(F is flight assist, T a lock, Backspace up to the helm) and the camera only follows it, the wheel zooming the "
				  "chase; in command the same keys work the camera (F focus, T back to now, Backspace home). A key or button "
				  "still held as you take or leave a seat is ignored until you let it go, so a burn held as you hand the helm back "
				  "does not swim the camera off."),
				T("Manning a gun",
				  "/ mans the next gun on the hull you fly or have selected, or on a station's defence platform when the station "
				  "is selected while its defences fight. The view sits on the mount, looking down the barrel: the mouse lays it "
				  "(a heavy gun swings slowly, and only within its arc) and the left button fires it. / again steps to the next "
				  "gun, Backspace moves up to the hull's helm (or the whole station), and P hands it back to its crew. Past the "
				  "last gun / goes back to the helm or station, and / there starts on the first gun again, so every gun on a "
				  "capital ship or a station is a few presses away for a better angle."),
				T("EVA repair: welding a hull back together",
				  "A capital ship's or a station's damage control teams can go outside. Shift+P (at its helm, on one of its guns, "
				  "or with it selected in a fight) puts you in the suit of its next EVA repair worker, out on the plating; again "
				  "for the next. The camera rides over the worker's shoulder; W/S and A/D crawl the plating the way you look, "
				  "Shift fires the suit's jets, and an orange bracket marks the nearest damaged hardpoint -- a shield generator, "
				  "an engine, a turret -- with how far it is. Reach it and the bracket turns green and the torch lights: the "
				  "worker welds at its team's own rate, a quarter faster for your hands on it. Above the seat line a marker "
				  "sweeps a bar: tap left mouse as it crosses the green zone for a good weld, which raises the weld's quality "
				  "and speeds the job, up to two and three-quarter times what the team manages alone; each hit moves the zone "
				  "and a streak narrows it, and a miss sputters the torch for a moment and halves the quality. A damaged "
				  "hardpoint comes up to 85%; one shot away (a shield emitter or gun mount) is brought back as the emergency "
				  "crews do. While you hold a worker its team is outside, not in the damage control pool; let go (P, or "
				  "Backspace back inside to the helm or station) and it rejoins them. Distances are in hull lengths, so it "
				  "plays the same at gameplay and realistic scale."),
				T("A hull's own abilities: launch, then fly what you launched",
				  "At a helm the ability bar above the flight readout lists what that hull can order from what it really carries, "
				  "each on Shift and a digit (Shift+1 the first, Shift+0 the tenth). A carrier has one launch for each class of "
				  "craft its hangar holds -- interceptors, fighters, bombers, heavy fighters, the recon flight -- and each press "
				  "puts up the next squadron of that class aboard and fit to fly, out through its flight decks; then a recall, which "
				  "brings the wing home to land and stay aboard until launched again, and the wing's call, stepping from their own "
				  "fights to onto your locked hull (T) to guard the carrier and back. A hull with a shuttle bay sends a boarding "
				  "shuttle with a marine team at the hull locked (a disabled one, or one that has struck on terms that open it); a "
				  "Breacher bay fires a Breacher round at it (its shields down to a quarter); boarding pods fire a salvo at it (its "
				  "shields down). A hull with none of these has no bar. The orders are for a fight: out of one they are refused, "
				  "saying why, and a carrier you fly into a fight holds its wing aboard for you to launch. Shift+/ then jumps you "
				  "straight into the craft launched last, again into the next, and past the last back to the carrier's helm; "
				  "Backspace in any of its craft goes back to the helm too. Every switch is instant. Leave the carrier and its "
				  "craft alone (back to command, or another seat) and its captain takes over: the recall and the call lapse and "
				  "whatever you held aboard goes up."),
				T("Self-destruct",
				  "Whatever you hold -- a fighter, a ship's helm, a station, or a gun, which sets off the hull or station it is "
				  "mounted on -- can be made to blow itself up. Hold Shift+Backspace for a second to arm it: a countdown starts, "
				  "big in the middle of the screen (5 seconds in a fighter, 8 in a light hull, 15 in a capital ship, 20 on a "
				  "station), and Shift+Backspace again aborts it, as does leaving the seat. At zero the reactor goes: the craft is "
				  "destroyed as any kill is, leaving its wreck, and everyone aboard dies with it. The blast reaches everything "
				  "near, friend or foe, worst on whatever it is touching and falling off fast with the gap: a fighter is a big "
				  "grenade (a third of a kilometre), a corvette reaches about a kilometre, a cruiser four, a carrier eight, and a "
				  "hull whose reactor is shot up blows weaker. It strikes as a hit does -- the shield over that patch takes it "
				  "first, then the armour and the hardpoints on that side, and a small craft's blast finds an open weak point -- "
				  "so a hull flown up against a capital's bow and set off guts the bow. You simply leave: the view backs off to a "
				  "safe distance. The deed is the craft's own: its faction and its captain answer for it, never you.")),

			cs::New<ManualChapter>("The galaxy and travel", "Star systems, gates, lanes, FTL and the galaxy map", Keys(),
				T("The systems",
				  "Sol on its real orbits, Alpha Centauri with its colonies and factions, and twelve more of the nearest real "
				  "systems; past them the galaxy runs to the rim, real catalogue stars near Sol and stars generated from the "
				  "galaxy's seed everywhere else, charted as you go."),
				T("Hyperlanes",
				  "Inside a system, lanes of accelerator rings link the planetary systems and the jump gates. They carry most "
				  "traffic cheaply but predictably, so raiders ambush them, and they need tenders to keep running."),
				T("Jump gates",
				  "Gates stand at a system's edge, each facing the star it leads to. Fly within 300 km and press Y to come out "
				  "at the far gate, unless it is shut to you. Whoever holds a gate controls its traffic, and gates shut after raids."),
				T("Your own FTL drive",
				  "Carriers, gate breakers, fitted battleships and command cruisers and deep-space explorers have their own "
				  "drive. No jump is possible inside a system's gravity wells: fly out beyond the outer rim, put the nose on "
				  "another star and press Enter to spool, holding the nose on it. PgDn / PgUp or the galaxy map plot a course "
				  "with its ETA and fuel, and End engages the FTL autopilot, compressing the trip on the clock."),
				T("Jumps and void stops",
				  "A jump drive reaches about 7 light-years in one jump, as far as one jump gate links (a survey drive 9, a jump "
				  "ring 5). Aim at a star farther off and the whole way is still one flight, with no loading screen, but it is "
				  "flown as even jumps: after each the hull drops out in the void between the stars, the drive cools from the "
				  "jump and spools again before the next (a capital drive 12 hours cooling and 6 spooling, scaled in Settings > "
				  "FTL). The HUD shows VOID STOP, the cooldown, the spool and which jump of how many is next; the course line "
				  "counts the jumps and stops in its ETA, and the helm shows the drive cooling after a drop-out. A ship sitting "
				  "in the void is exposed, so a fast spool is worth having, and void stations make safer stops. The jump gates "
				  "stay the way most traffic goes."),
				T("Jump rings",
				  "Fighters, couriers and other small craft carry no drive of their own. At a shipyard (F2, outfitting) a small "
				  "craft can dock to a jump ring, a hyperdrive ring it rides between the stars like a capital hull: aim at a star "
				  "beyond the rim and press Enter. A ring's stops are long, its jumps short; let it go at a yard for half its price back."),
				T("Void Raiders",
				  "Something hunts the void between the stars. A ship stopped between jumps, its drive cooling and spooling, can "
				  "be found by ghost ships no yard has built, and the longer it sits there the likelier they come: the chance "
				  "grows with the square of the stop, so a fast spool is the best defence, and they never come within half a "
				  "light-year of a star or a void station. A ghost closes for a couple of hours before it strikes; a hull that "
				  "jumps first is gone, and one whose sensors pick it out (CONTACT on the HUD) meets the strike, losing less and "
				  "downing more of them. At a stop your sensors are yours: Tab pings (any ghost closing shows at once and may "
				  "break off, but you are lit up and three times as findable for a while), Shift+F3 runs dark (a quarter as "
				  "findable, a ghost closing may lose you, but no charge builds until you power up). A strike costs escorts, or "
				  "hull, reaction mass and a scrambled drive. Nobody knows at first what they are: unidentified sensor ghosts. "
				  "What they are comes out as raids pile up, as news (OUT OF THE VOID), and the wrecks of the ones brought down "
				  "are worth a salvager's while."),
				T("Deep space",
				  "Space between the stars is simulated: void stations, gravity projectors that pull ships out of FTL, and the "
				  "traffic crossing it."),
				T("Ship graveyards",
				  "Beyond Sol some systems and void stations keep a graveyard: capital wrecks and broken freighters among "
				  "dense, slowly tumbling wreckage, in an orange haze near a hot star or a cold blue one far out, and now and then "
				  "a dark derelict station. Match speed alongside a hulk or a station to latch on and salvage what is left "
				  "aboard. Graveyards are where raiders and worse like to lie up."),
				T("Void stations",
				  "A void station is a stop worth making, not just a relay. Round its generator: an asteroid drift whose rocks "
				  "you can stake mining claims on (F10), with an assay camp on the biggest that buys the ore and mines a rarer "
				  "seam of its own; a planetoid that cracked or blew apart, wrapped in a shell of its debris with its broken "
				  "chunks circling it; and a sight or two (a crystal moonlet, a frozen fountain, a lamplit derelict ark, a stone "
				  "hoop, a glowing rock) to visit while the drive cools. Each station's places are always the same, and they "
				  "read the same in realistic and game scale."),
				T("Void station mixes",
				  "No two void stations are alike: a station's field may be a stony drift, a loose rubble swarm, a dense metal "
				  "swarm rich in rare ore, or a glittering ice field mined for water and volatiles, and some stations keep a "
				  "megastructure, a city of stacked lit towers with docking spars and ships berthed along them."),
				T("Nebulae and ice fields",
				  "Beyond Sol some systems hold a coloured nebula a few AU out or a cloud of ice chunks, and some void stations "
				  "sit inside a nebula of their own. A nebula swallows sensors: inside it, or looking into it, your passive reach "
				  "and an active ping (Tab) both fall well short. Pirates hide havens in them; one is found only by flying in "
				  "close or pinging near it, and its raiders sail out from it."),
				T("The galaxy map",
				  "[ opens it: every system at its real place, who holds it, gate traffic, war fronts, blockades and raiders as "
				  "far as the news goes. Click a star to inspect it, double-click to travel; DIPLOMACY and COMMAND are its panels."),
				T("Information lag",
				  "News travels at light speed inside a system and through the gates between them, so what anyone knows of a "
				  "distant place is as old as the news that brought it.")),

			cs::New<ManualChapter>("Settings", "Everything you can change from the game", Keys(),
				T("The settings menu",
				  "Settings, from the main menu or the Esc menu, lists every tunable the game's systems declare by group: "
				  "Graphics, Render pipeline, LOD & render budget, Simulation, Economy, Controls, Audio, Dogfighting, "
				  "Broadsides, Weapons, FTL, AI, Civilizations and more. Search finds a setting across all of them."),
				T("Changing and resetting",
				  "Each change takes effect at once and is kept in your profile. Reset puts back one setting, a group or "
				  "everything found."),
				T("Post-processing (URP)",
				  "On URP, Render pipeline's post-processing settings put URP's own Volume stack over the view: HDR, so the "
				  "star's face glares and blooms above white; exposure fixed, automatic or on a histogram (both adapt like an "
				  "eye, the histogram ignoring black space and the star's face), or a physical camera's ISO, shutter and "
				  "aperture against sunny 16; a tone curve (none, Neutral or ACES) and contrast, saturation and white balance. "
				  "Fresh profiles keep the image as drawn, only the star glaring. Post-processing off draws it with none."),
				T("Display",
				  "Display has its own page: resolution, window mode and VSync."),
				T("Profiles",
				  "Each profile keeps its own settings and saves; switch or make one from Profiles on the main menu.")),

			cs::New<ManualChapter>("Screens and saving", "The HUD, the panels, the pause menu and saves", Keys("SCREENS AND PANELS"),
				T("The HUD",
				  "U hides the HUD panels and readouts (the names and markers in the view stay); F11 clears everything but "
				  "the reticle in use. Esc closes panels, then opens the pause menu."),
				T("The pause menu",
				  "Resume, Help, Save / Load, Settings, Main Menu (with an autosave) and Quit."),
				T("The dev menu", // US-T126
				  "Shift+F1 pulls a menu down over the top of the view with every dev and debug function in one place, by "
				  "category: Spawn (any hull beside you, friendly or neutral), Enemies (pirate raiders small, medium and "
				  "large, any pirate hull, a hostile navy's warships, a Bio Mass globule; at a helm they open a fight with "
				  "you), Travel (the cruise drive and Cruise to, teleports to a world or station, the other stars), Helm "
				  "(take a helm, the nearest ship, seats, guns, launched craft), Combat (the dogfight demo in each kit, a big "
				  "battle to watch, mend the hull flown) and Time & scale (pause, warp, the realistic or game scale). Type to filter everything, "
				  "Up and Down pick, Enter or a click runs, Right opens a submenu, Left backs out, Tab steps the categories, "
				  "Esc closes. While it is open it has the keys and the mouse; the game runs on and the craft holds its "
				  "heading. The console's sg.Cheat runs any entry by its path."),
				T("The showcase tour", // US-T147
				  "One guided run through every feature, so nothing has to be launched as a demo of its own: Showcase Tour on "
				  "the main menu, the dev menu's Showcase category, or sg.Tour in the console. It plays in chapters (Flight and "
				  "combat, Embodiment, Places, Threats, Presentation, Ships, Travel), each stop set up for you (the trip, the "
				  "helm, the fight, the scale) with a line at the bottom saying what to look at. Right or N goes on, Left or B "
				  "back, S skips the rest of the chapter, Space pauses, 1 to 9 jump to a chapter, O shows every stop again at "
				  "the other scale, and Esc ends it, leaving the game as the stop left it. sg.Tour auto shots <folder> plays it "
				  "unattended and screenshots every stop, then writes a contact sheet."),
				T("Saving",
				  "The whole simulation saves and resumes exactly where it was. F5 quicksaves and F8 loads it; F6 opens the "
				  "save slots, where you can make as many saves as you like. The game autosaves every five minutes and on "
				  "quit, keeping three, and saving waits while an FTL jump is under way.")),

			cs::New<ManualChapter>(EveryKeyTitle, "The whole reference card: every key and control, by what it is for", cs::List<cs::String>() /* C#: null */),
		};
		return value;
	}

	cs::List<cs::Ref<HelpSection>> GameManual::ControlsOf(const cs::Ref<ManualChapter>& chapter)
	{
		return chapter->Title == EveryKeyTitle ? ControlsHelp::Sections() : chapter->Controls();
	}

	cs::Ref<ManualChapter> GameManual::Find(const cs::String& title)
	{
		for (const auto& c : Chapters())
			if (c->Title == title)
				return c;
		return nullptr;
	}
}

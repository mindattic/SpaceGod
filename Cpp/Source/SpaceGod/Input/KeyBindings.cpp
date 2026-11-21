// US-T249: the action map (Input/KeyBindings.h); US-T437: any input in three slots, with trigger modes.

#include "Input/KeyBindings.h"
#include "Input/Gamepad.h"
#include "Settings/SettingsRegistry.h"
#include "UnityCompat/UnityRuntime.h"

#include <climits>
#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod::Input
{
	// ---------------------------------------------------------------- the setting

	KeyBindingSetting::KeyBindingSetting(const cs::String& key, const cs::String& group, const cs::String& label, const InputBinding (&defaults)[Slots],
		bool heldKind, const cs::String& description, const cs::String& keywords)
		: GameSetting(key, group, label, description, keywords), HeldKind(heldKind)
	{
		for (int i = 0; i < Slots; i++) Default[i] = Normalized(defaults[i], heldKind);
		CompactSlots(Default);
		SetToDefault();
		SettingsRegistry::Default()->RegisterDeclared(this);
	}

	const char* KeyBindingSetting::SlotName(int slot)
	{
		static const char* const names[Slots] = { "Primary", "Secondary", "Tertiary" };
		return slot >= 0 && slot < Slots ? names[slot] : "";
	}

	bool KeyBindingSetting::IsDefault() const
	{
		for (int i = 0; i < Slots; i++)
			if (Value[i] != Default[i]) return false;
		return true;
	}

	cs::String KeyBindingSetting::ValueText() const
	{
		cs::String s;
		for (const InputBinding& c : Value)
		{
			if (c.Empty()) continue;
			if (!s.empty()) s += "  /  ";
			s += c.Display();
		}
		return s.empty() ? cs::String("unbound") : s;
	}

	cs::String KeyBindingSetting::Format() const { return FormatSlots(Value, Default); }

	bool KeyBindingSetting::TryLoad(const cs::String& text)
	{
		InputBinding read[Slots];
		if (!ParseSlots(text, Default, HeldKind, read)) return false;
		for (int i = 0; i < Slots; i++) Value[i] = read[i];
		Version++;
		return true;
	}

	void KeyBindingSetting::SetToDefault()
	{
		for (int i = 0; i < Slots; i++) Value[i] = Default[i];
		Version++;
	}

	void KeyBindingSetting::AfterEdit(const InputBinding (&before)[Slots])
	{
		CompactSlots(Value);
		bool changed = false;
		for (int i = 0; i < Slots; i++)
			if (Value[i] != before[i]) changed = true;
		if (!changed) return;
		Version++;
		OnSet();
	}

	void KeyBindingSetting::Set(int slot, InputBinding binding)
	{
		if (slot < 0 || slot >= Slots) return;
		InputBinding before[Slots];
		for (int i = 0; i < Slots; i++) before[i] = Value[i];
		binding = Normalized(binding, HeldKind);
		if (!binding.Empty())
			for (int i = 0; i < Slots; i++)
				if (i != slot && Answers(Value[i], HeldKind, binding, HeldKind)) Value[i] = InputBinding();
		Value[slot] = binding;
		AfterEdit(before);
	}

	void KeyBindingSetting::SetTrigger(int slot, Trigger trigger)
	{
		if (slot < 0 || slot >= Slots || Value[slot].Empty()) return;
		InputBinding before[Slots];
		for (int i = 0; i < Slots; i++) before[i] = Value[i];
		Value[slot] = Normalized(Value[slot].With(trigger), HeldKind);
		// The same input in another slot answering the same touch now: that one goes (a slot never doubles another).
		for (int i = 0; i < Slots; i++)
			if (i != slot && Answers(Value[i], HeldKind, Value[slot], HeldKind)) Value[i] = InputBinding();
		AfterEdit(before);
	}

	void KeyBindingSetting::Assign(const InputBinding (&values)[Slots])
	{
		InputBinding before[Slots];
		for (int i = 0; i < Slots; i++)
		{
			before[i] = Value[i];
			Value[i] = Normalized(values[i], HeldKind);
		}
		AfterEdit(before);
	}

	bool KeyBindingSetting::Has(const InputBinding& binding) const
	{
		for (const InputBinding& c : Value)
			if (Answers(c, HeldKind, binding, HeldKind)) return true;
		return false;
	}

	bool KeyBindingSetting::HasByDefault(const InputBinding& binding) const
	{
		for (const InputBinding& c : Default)
			if (Answers(c, HeldKind, binding, HeldKind)) return true;
		return false;
	}

	// ---------------------------------------------------------------- the actions

	namespace
	{
		using namespace KeyContext;
		constexpr int S = KeyMods::Shift, A = KeyMods::Alt;
		constexpr bool HeldAct = true, EdgeKind = false;

		InputBinding Pad(PadButton b) { return InputBinding::OfPad(b); }

		struct ActionInfo
		{
			KeyAction action;
			const char* key;
			const char* name;
			int context;
			bool held;
			InputBinding defaults[KeyBindingSetting::Slots];
			const char* description;
		};

		// In the page's order (the enum's); the defaults are the original's keys and (US-T437) the pad's buttons of before.
		const std::vector<ActionInfo>& Actions()
		{
			static const std::vector<ActionInfo> actions =
			{
				{ KeyAction::MoveForward, "moveForward", "Move: forward / thrust", Both, HeldAct, { Key::W }, "Swim forward in command; thrust at the controls (a capital's telegraph a step ahead on the press)." },
				{ KeyAction::MoveBack, "moveBack", "Move: back / retro", Both, HeldAct, { Key::S }, "Swim back in command; the retros at the controls (a capital's telegraph a step astern)." },
				{ KeyAction::StrafeLeft, "strafeLeft", "Move: strafe left", Both, HeldAct, { Key::A }, "Swim or strafe left (a capital ship turns)." },
				{ KeyAction::StrafeRight, "strafeRight", "Move: strafe right", Both, HeldAct, { Key::D }, "Swim or strafe right (a capital ship turns)." },
				{ KeyAction::Rise, "rise", "Move: up", Both, HeldAct, { Key::E, Key::RightBracket }, "Swim or strafe up." },
				{ KeyAction::Sink, "sink", "Move: down", Both, HeldAct, { Key::Q, Key::C }, "Swim or strafe down." },
				{ KeyAction::Boost, "boost", "Move: boost", Both, HeldAct, { Key::LeftShift, Key::RightShift, Pad(PadButton::A) }, "Held: boost (and the hard boost of a fighter)." },
				{ KeyAction::Slow, "slow", "Move: fine / docking", Both, HeldAct, { Key::LeftCtrl, Key::RightCtrl }, "Held: fine movement; a freighter's docking mode in a port's approach." },

				{ KeyAction::RollLeft, "rollLeft", "Flight: roll left", Controls, HeldAct, { Key::Z, Pad(PadButton::LeftBumper) }, "Held: roll anticlockwise. (In command the pad's bumper sinks the camera instead.)" },
				{ KeyAction::RollRight, "rollRight", "Flight: roll right", Controls, HeldAct, { Key::X, Pad(PadButton::RightBumper) }, "Held: roll clockwise. (In command the pad's bumper raises the camera instead.)" },
				{ KeyAction::Manoeuvre, "manoeuvre", "Flight: flip (manoeuvre)", Controls, HeldAct, { Key::LeftAlt, Key::RightAlt, Pad(PadButton::X) }, "Held: swing the nose onto the retrograde for a turn and burn; a capital's broadside stance." },
				{ KeyAction::Cruise, "cruise", "Flight: tactical cruise", Controls, HeldAct, { Key::J }, "Held: the stronger drive tier, out of a fight." },
				{ KeyAction::SystemCruise, "systemCruise", "Flight: cruise drive", Controls, EdgeKind, { InputBinding(Key::J, S) }, "Spool the in-system cruise drive for what the nose is on, or drop out of it." },
				{ KeyAction::Drift, "drift", "Flight: drift", Controls, HeldAct, { Key::R, Pad(PadButton::B) }, "A fighter's drift (hold, or toggle as Controls > Drift key sets it). In command its pad button backs out, as Esc does." },
				{ KeyAction::FlightAssist, "flightAssist", "Flight: flight assist", Controls, EdgeKind, { Key::F, Pad(PadButton::RightStick) }, "Flight assist on or off." },
				{ KeyAction::JumpJets, "jumpJets", "Flight: jump jets (mech)", Controls, HeldAct, { Key::Space }, "Held in a mech: the jump jets lift it off the plating and fly it in open space (the pause's Space is theirs while you pilot one)." },

				{ KeyAction::Fire, "fire", "Combat: fire", Controls, HeldAct, { Pad(PadButton::RightTrigger) }, "Held: fires the guns at the controls. The left mouse button always fires too (it is fixed)." }, // US-T437: the pad's fire of before
				{ KeyAction::SecondaryFire, "secondaryFire", "Combat: second trigger", Controls, HeldAct, { Pad(PadButton::LeftTrigger) }, "Held: a seat's second trigger (the right mouse button's at the controls, which stays fixed too)." }, // US-T437
				{ KeyAction::LockTarget, "lockTarget", "Combat: lock target", Controls, EdgeKind, { Key::T, Pad(PadButton::Y) }, "Lock (or clear) the hull under the mouse or selected." },
				{ KeyAction::LockAttacker, "lockAttacker", "Combat: lock attacker", Controls, EdgeKind, { InputBinding(Key::B, S) }, "Lock the hostile engaging you, nearest first; again for the next one engaging you. With nobody on you, the nearest hostile." }, // US-T307
				{ KeyAction::Tractor, "tractor", "Combat: tractor beam", Controls, EdgeKind, { InputBinding(Key::T, S) }, "The tractor beam on the nearest cargo pod in reach, or off." },
				{ KeyAction::DropMine, "dropMine", "Combat: drop mine", Controls, EdgeKind, { Key::LeftCtrl, Key::RightCtrl }, "In a fight: a mine dropped astern." },
				{ KeyAction::TunePhasers, "tunePhasers", "Combat: tune phasers", Controls, EdgeKind, { Key::Insert }, "Retune the phaser arrays: a narrow lance or a wide sweep." },
				{ KeyAction::RunHot, "runHot", "Combat: run hot", Controls, EdgeKind, { InputBinding(Key::Insert, S) }, "At a big ship's helm: the crew runs the guns and drive hot, or throttles down." },
				{ KeyAction::SensorPing, "sensorPing", "Sensors: active ping", Controls, EdgeKind, { Key::Tab }, "An active ping (at a void stop too)." },
				{ KeyAction::SilentRunning, "silentRunning", "Sensors: silent running", Both, EdgeKind, { Key::F3 }, "Silent running on or off." },
				{ KeyAction::PowerToggle, "powerToggle", "Sensors: go dark / power up", Controls, EdgeKind, { InputBinding(Key::F3, S) }, "Power down to go dark, or power up again (at a void stop, run dark)." },
				{ KeyAction::SelfDestruct, "selfDestruct", "Combat: self-destruct", Both, HeldAct, { InputBinding(Key::Backspace, S) }, "Held a second at the controls: arms the self-destruct; pressed during a countdown: aborts it." },

				{ KeyAction::PowerWeapons, "powerWeapons", "Reactor: a pip to weapons", Controls, EdgeKind, { Key::LeftArrow }, "Reactor power: a pip to weapons." },
				{ KeyAction::PowerShields, "powerShields", "Reactor: a pip to shields", Controls, EdgeKind, { Key::UpArrow }, "Reactor power: a pip to shields." },
				{ KeyAction::PowerEngines, "powerEngines", "Reactor: a pip to engines", Controls, EdgeKind, { Key::RightArrow }, "Reactor power: a pip to engines." },
				{ KeyAction::PowerBalance, "powerBalance", "Reactor: even share", Controls, EdgeKind, { Key::DownArrow }, "Reactor power: an even share." },
				{ KeyAction::PowerAllWeapons, "powerAllWeapons", "Reactor: all to weapons", Controls, EdgeKind, { InputBinding(Key::LeftArrow, S) }, "Reactor power: all it can spare to weapons." },
				{ KeyAction::PowerAllShields, "powerAllShields", "Reactor: all to shields", Controls, EdgeKind, { InputBinding(Key::UpArrow, S) }, "Reactor power: all it can spare to shields." },
				{ KeyAction::PowerAllEngines, "powerAllEngines", "Reactor: all to engines", Controls, EdgeKind, { InputBinding(Key::RightArrow, S) }, "Reactor power: all it can spare to engines." },

				{ KeyAction::ActivateGate, "activateGate", "Travel: jump gate / rejoin lane", Controls, EdgeKind, { Key::Y }, "Activate the jump gate in range; back into the lane where you left it." },
				{ KeyAction::Eject, "eject", "Travel: eject", Controls, EdgeKind, { InputBinding(Key::Y, S) }, "In a launched craft's cockpit: eject in a pressure suit." },
				{ KeyAction::EngageFtl, "engageFtl", "Travel: FTL spool", Controls, EdgeKind, { Key::Enter, Key::NumpadEnter }, "Spool the FTL drive for the star the nose is on, or call a charge off." },
				{ KeyAction::FtlAutopilot, "ftlAutopilot", "Travel: FTL autopilot", Controls, EdgeKind, { Key::End }, "The FTL autopilot for the plotted course, or the helm back." },
				{ KeyAction::NextFtlTarget, "nextFtlTarget", "Travel: plot next star", Controls, EdgeKind, { Key::PageDown }, "Plot a course to the next star or beacon out, nearest first." },
				{ KeyAction::PrevFtlTarget, "prevFtlTarget", "Travel: plot previous star", Controls, EdgeKind, { Key::PageUp }, "Plot a course to the star or beacon before." },
				{ KeyAction::ClearFtlCourse, "clearFtlCourse", "Travel: clear course", Controls, EdgeKind, { Key::Delete }, "Clear the plotted course." },
				{ KeyAction::RollPatches, "rollPatches", "Combat: roll shield patches", Controls, EdgeKind, { InputBinding(Key::Delete, S) }, "At a capital's helm: the helm assist rolls weak shield patches away from the heaviest fire, or stops." },

				{ KeyAction::Pilot, "pilot", "Seats: embody / let go", Both, EdgeKind, { Key::P, Pad(PadButton::LeftStick) }, "Embody what is selected or under the mouse; at any controls, let go." },
				{ KeyAction::EmbodyCrew, "embodyCrew", "Seats: EVA repair worker", Both, EdgeKind, { InputBinding(Key::P, S) }, "Out on the hull as its next EVA repair worker." },
				{ KeyAction::DeployMech, "deployMech", "Seats: mech out of the bay", Both, EdgeKind, { InputBinding(Key::N, S) }, "In a fight: the mech out of the bay of the hull held, flown or selected (a capital ship or station), you at its controls." },
				{ KeyAction::NextSeat, "nextSeat", "Seats: next seat", Both, EdgeKind, { Key::Quote }, "Take the next seat in the fight in view." },
				{ KeyAction::PrevSeat, "prevSeat", "Seats: previous seat", Both, EdgeKind, { Key::Semicolon }, "Take the seat before." },
				{ KeyAction::ManGun, "manGun", "Seats: man the next gun", Both, EdgeKind, { Key::Slash }, "Man the next gun on the hull flown or selected; past the last, back to the helm." },
				{ KeyAction::JumpToCraft, "jumpToCraft", "Seats: into a launched craft", Both, EdgeKind, { InputBinding(Key::Slash, S) }, "Into the newest craft the carrier launched, then the next." },
				{ KeyAction::MoveToHost, "moveToHost", "Seats: up to the helm", Controls, EdgeKind, { Key::Backspace }, "From a gun mount up to the helm of its hull." },
				{ KeyAction::Picker, "picker", "Seats: take-charge picker", Both, EdgeKind, { Key::F9 }, "Every seat on the thing in hand and around the camera, to click into." },
				{ KeyAction::Boarding, "boarding", "Combat: boarding panel", Controls, EdgeKind, { Key::B }, "At the controls: the boarding command panel." },

				{ KeyAction::Focus, "focus", "Command: focus", Command, EdgeKind, { Key::F, Pad(PadButton::Y) }, "Focus the selection." },
				{ KeyAction::Home, "home", "Command: system view", Command, EdgeKind, { Key::Home, Key::Backspace }, "Back to the system view." },
				{ KeyAction::CycleShips, "cycleShips", "Command: next ship", Command, EdgeKind, { Key::Tab }, "Cycle the ships forward." },
				{ KeyAction::CycleBackward, "cycleBackward", "Command: previous ship", Command, EdgeKind, { InputBinding(Key::Tab, S) }, "Cycle the ships back." },
				{ KeyAction::WatchBattle, "watchBattle", "Command: watch a battle", Command, EdgeKind, { Key::B }, "Watch a battle." },
				{ KeyAction::ResetTime, "resetTime", "Command: back to now", Command, EdgeKind, { Key::T }, "Time back to now." },
				{ KeyAction::Reorient, "reorient", "Command: auto-reorient", Command, EdgeKind, { Key::R }, "Auto-reorient on or off." },
				{ KeyAction::News, "news", "Command: news feed", Command, EdgeKind, { Key::X }, "The news feed." },

				{ KeyAction::TogglePause, "pause", "Time: pause", Both, EdgeKind, { Key::Space, Key::Pause }, "Pause (eases to a stop and back)." },
				{ KeyAction::TimeFaster, "timeFaster", "Time: faster", Both, EdgeKind, { Key::Period, Key::Equals, Key::NumpadPlus }, "Time warp faster." },
				{ KeyAction::TimeSlower, "timeSlower", "Time: slower", Both, EdgeKind, { Key::Comma, Key::Minus, Key::NumpadMinus }, "Time warp slower (below real time, slow motion)." },
				{ KeyAction::ToggleScale, "scale", "Time: gameplay / realistic scale", Both, EdgeKind, { Key::G, Key::Backquote }, "Toggle GAMEPLAY / REALISTIC scale." },

				{ KeyAction::Orbits, "orbits", "View: orbits", Both, EdgeKind, { Key::O }, "Orbits on or off." },
				{ KeyAction::Labels, "labels", "View: labels", Both, EdgeKind, { Key::L }, "Labels on or off." },
				{ KeyAction::Lanes, "lanes", "View: hyperlanes", Both, EdgeKind, { Key::N }, "Hyperlanes on or off." },
				{ KeyAction::Satellites, "satellites", "View: satellites", Both, EdgeKind, { Key::K }, "Satellites on or off." },
				{ KeyAction::SkyNext, "skyNext", "View: next skybox", Both, EdgeKind, { InputBinding(Key::K, S), Key::NumpadMultiply }, "The system in view on to its next sky." },
				{ KeyAction::SkyPrev, "skyPrev", "View: previous skybox", Both, EdgeKind, { InputBinding(Key::K, S | A), Key::NumpadDivide }, "The system in view back to the sky before." },
				{ KeyAction::ToggleHud, "hud", "View: HUD", Both, EdgeKind, { Key::U }, "The HUD's panels and readouts off or on." },
				{ KeyAction::ClearScreen, "clearScreen", "View: clear screen", Both, EdgeKind, { Key::F11 }, "Every piece of UI but the reticle off, and back." },
				{ KeyAction::HudMode, "hudMode", "View: HUD mode", Both, EdgeKind, { Key::CapsLock }, "The cursor and the HUD's pull-out drawers on every edge, or back to the quiet screen. (Z, the owner's pick, is roll left at the controls: rebind it here if you like.)" }, // US-T437

				{ KeyAction::PauseMenu, "pauseMenu", "Screen: back / pause menu", Both, EdgeKind, { Pad(PadButton::Menu) }, "Back out of what is open, then the pause menu, as Esc does (Esc always does it too)." }, // US-T437: the pad's pause of before
				{ KeyAction::Help, "help", "Screen: help", Both, EdgeKind, { Key::H, Key::F1, Pad(PadButton::View) }, "The help screen." },
				{ KeyAction::Comms, "comms", "Screen: comms", Both, EdgeKind, { InputBinding(Key::H, S) }, "Hail: the comms panel." },
				{ KeyAction::Drawer, "drawer", "Screen: bottom drawer", Both, EdgeKind, { InputBinding(Key::U, S) }, "The bottom drawer up or down." },
				{ KeyAction::GalaxyMap, "galaxyMap", "Screen: galaxy map", Both, EdgeKind, { Key::LeftBracket }, "The galaxy map." },
				{ KeyAction::SystemMap, "systemMap", "Screen: system map", Both, EdgeKind, { InputBinding(Key::LeftBracket, S) }, "The system map." },
				{ KeyAction::Market, "market", "Screen: market", Both, EdgeKind, { Key::M }, "The market." },
				{ KeyAction::DockPort, "dockPort", "Screen: dock / port", Both, EdgeKind, { InputBinding(Key::M, S) }, "Docking clearance at the port alongside, then the port screen." },
				{ KeyAction::Cargo, "cargo", "Screen: cargo", Both, EdgeKind, { Key::I }, "The cargo hold." },
				{ KeyAction::Outfit, "outfit", "Screen: outfitting", Both, EdgeKind, { Key::F2 }, "Outfitting." },
				{ KeyAction::Bounties, "bounties", "Screen: bounty board", Both, EdgeKind, { Key::F4 }, "The bounty board of the port in view." },
				{ KeyAction::Contracts, "contracts", "Screen: contracts", Both, EdgeKind, { Key::F7 }, "The contracts board." },
				{ KeyAction::Fleet, "fleet", "Screen: fleet", Both, EdgeKind, { Key::F10 }, "Your fleet and property." },
				{ KeyAction::Trade, "trade", "Screen: trade desk", Both, EdgeKind, { Key::Backslash }, "The trade desk." },
				{ KeyAction::Hail, "hail", "Screen: envoy (hail polity)", Both, EdgeKind, { Key::F12 }, "Hail the polity of the ship locked, selected or hailing you." },
				{ KeyAction::Logs, "logs", "Screen: recovered logs", Both, EdgeKind, { InputBinding(Key::L, S) }, "The ship logs and station records recovered." },
				{ KeyAction::Discoveries, "discoveries", "Screen: discoveries", Both, EdgeKind, { InputBinding(Key::O, S) }, "Everything found, by kind and day." },
				{ KeyAction::Encyclopedia, "encyclopedia", "Screen: encyclopedia", Both, EdgeKind, { InputBinding(Key::I, S) }, "Every article in one place: controls, guides, lore and all you have learned, with a search." }, // US-T271
				{ KeyAction::Society, "society", "Screen: society", Both, EdgeKind, { InputBinding(Key::G, S) }, "The galaxy's people: the feed of their doings, houses, people, organisations and each world's mood, and the SpaceGod's hand in them." }, // US-T293
				{ KeyAction::Notices, "notices", "Screen: notice history", Both, EdgeKind, { InputBinding(Key::X, S) }, "The bottom drawer on its Notices tab: the last 100 notices with their game time, filtered by kind." }, // US-T345
				{ KeyAction::BattlePlot, "battlePlot", "Screen: battle plot", Both, EdgeKind, { InputBinding(Key::F10, S) }, "In a fight: the top-down plot of every hull around you by side, your lock and your calls' marks, on or off." }, // US-T367
				{ KeyAction::SpawnPilot, "spawnPilot", "Screen: spawn & pilot", Both, EdgeKind, { Key::V }, "Pick a hull to spawn and pilot at the camera." },
				{ KeyAction::SpawnPractice, "spawnPractice", "Screen: practice fighter", Both, EdgeKind, { InputBinding(Key::V, S) }, "The practice fighter at the camera, you at its helm." },
				{ KeyAction::QuickSave, "quickSave", "Game: quicksave", Both, EdgeKind, { Key::F5 }, "Save to the quicksave slot." },
				{ KeyAction::QuickLoad, "quickLoad", "Game: quickload", Both, EdgeKind, { Key::F8 }, "Load the quicksave slot." },
				{ KeyAction::Rewind, "rewind", "Game: rewind", Both, EdgeKind, { InputBinding(Key::F8, S) }, "Rewind to the last moment kept; again within a few seconds to go further back. Play on and the old future is replaced." }, // US-T99
				{ KeyAction::HoldRewind, "holdRewind", "Game: hold to rewind", Both, HeldAct, { InputBinding(Key::R, S) }, "Held: time drops to real speed and the last seconds of the fight (and your hull) play backward, up to 10 s; let go to play on from there. Free, then a cooldown." }, // US-T409
				{ KeyAction::Saves, "saves", "Game: save slots", Both, EdgeKind, { Key::F6 }, "The save slots." },
				{ KeyAction::CheatMenu, "devMenu", "Game: dev menu", Both, EdgeKind, { InputBinding(Key::F1, S) }, "The dev menu." },
				{ KeyAction::SpawnMenu, "spawnMenu", "Game: dev spawn menu", Both, EdgeKind, { InputBinding(Key::F2, S) }, "The dev menu on its Spawn tab." },
			};
			return actions;
		}

		const ActionInfo& Info(KeyAction action)
		{
			const auto& all = Actions();
			int i = static_cast<int>(action);
			return all[i >= 0 && i < static_cast<int>(all.size()) ? i : 0];
		}

		int SourceBit(const InputBinding& b)
		{
			if (b.IsPad()) return BindSources::Pad;
			return b.IsMouse() ? BindSources::Mouse : BindSources::Keyboard;
		}

		/// A binding whose answer needs what happened on earlier frames: a mode other than the action's own, or an analog
		/// reading (a stick, a trigger against a threshold), whose edges come from the last frame's reading.
		bool Stateful(const InputBinding& b) { return !b.Empty() && (b.trigger != Trigger::Auto || b.Analog()); }

		struct Raw
		{
			bool down = false, pressed = false, released = false;
		};

		float StickValue(const GamepadDevice& pad, int code)
		{
			const Vector2& s = code < 4 ? pad.leftStick : pad.rightStick;
			switch (code % 4)
			{
				case 0: return s.y; // up: pushed away from the player
				case 1: return -s.y;
				case 2: return -s.x;
				default: return s.x;
			}
		}

		/// The input's state this frame. An analog reading gives only <c>down</c> (its edges are the caller's).
		Raw ReadRaw(const InputBinding& b, const InputDevice& kb, const GamepadDevice& pad, int heldMods)
		{
			Raw r;
			auto fromButton = [&r](const ButtonControl& c, bool modsOk)
			{
				r.down = c.isPressed && modsOk;
				r.pressed = c.wasPressedThisFrame && modsOk;
				r.released = c.wasReleasedThisFrame;
			};
			switch (b.source)
			{
				case BindSource::Key:
					if (kb.present) fromButton(kb[static_cast<Key>(b.code)], (heldMods & b.mods) == b.mods);
					break;
				case BindSource::Mouse:
					if (kb.present) fromButton(kb.Button(static_cast<MouseButton>(b.code)), (heldMods & b.mods) == b.mods);
					break;
				case BindSource::Wheel:
					if (kb.present) fromButton(b.code == 0 ? kb.wheelUp : kb.wheelDown, (heldMods & b.mods) == b.mods);
					break;
				case BindSource::PadButton:
					if (pad.present) fromButton(pad[static_cast<PadButton>(b.code)], true);
					break;
				case BindSource::PadTrigger:
					if (!pad.present) break;
					if (b.thresholdPct <= 0) fromButton(pad[b.code == 1 ? PadButton::RightTrigger : PadButton::LeftTrigger], true); // the pad's own click
					else r.down = (b.code == 1 ? pad.rightTrigger : pad.leftTrigger) >= b.thresholdPct / 100.0f;
					break;
				case BindSource::PadStick:
					if (!pad.present) break;
					r.down = StickValue(pad, b.code) >= (b.thresholdPct > 0 ? b.thresholdPct / 100.0f : KeyBindings::Tuning().StickThreshold);
					break;
				default:
					break;
			}
			return r;
		}

		/// What a stateful binding remembers: its trigger timing, the last analog reading, and the frame it was stepped on.
		struct SlotState
		{
			int frame = INT_MIN;
			int version = -1;
			bool lastDown = false;
			TriggerState trigger;
			TriggerOut out;
		};

		SlotState& StateOf(KeyAction action, int slot)
		{
			static std::vector<SlotState> states(static_cast<size_t>(KeyAction::Count) * KeyBindingSetting::Slots);
			return states[static_cast<size_t>(action) * KeyBindingSetting::Slots + static_cast<size_t>(slot)];
		}

		/// The slot's answer this frame: straight from the device for a binding in the action's own mode on a button (as
		/// every binding was before US-T437), else through its timing, stepped once a frame.
		TriggerOut Evaluate(KeyAction action, int slot, const KeyBindingSetting& setting, const InputDevice& kb, const GamepadDevice& pad, int heldMods)
		{
			const InputBinding& b = setting.Value[slot];
			if (b.Empty()) return TriggerOut();
			if (!Stateful(b))
			{
				Raw r = ReadRaw(b, kb, pad, heldMods);
				return TriggerOut{ r.down, r.pressed };
			}
			SlotState& s = StateOf(action, slot);
			if (s.version != setting.Version)
			{
				s = SlotState();
				s.version = setting.Version;
			}
			if (s.frame != Time::frameCount)
			{
				s.frame = Time::frameCount;
				Raw r = ReadRaw(b, kb, pad, heldMods);
				if (b.Analog())
				{
					r.pressed = r.down && !s.lastDown;
					r.released = !r.down && s.lastDown;
					s.lastDown = r.down;
				}
				s.out = StepTrigger(s.trigger, b.trigger, setting.HeldKind, r.down, r.pressed, r.released, Time::unscaledTime, KeyBindings::Tuning());
			}
			return s.out;
		}

		/// Another action live where <paramref name="action"/> is has a binding on the same input needing more of the
		/// modifiers held, and they are: the press is that one's.
		bool PreEmpted(KeyAction action, const InputBinding& c, int held)
		{
			if (!c.UsesMods()) return false;
			int context = Info(action).context;
			for (const ActionInfo& other : Actions())
			{
				if (other.action == action || (other.context & context) == 0) continue;
				for (const InputBinding& o : KeyBindings::Binding(other.action)->Value)
					if (!o.Empty() && o.source == c.source && o.code == c.code && o.mods != c.mods && (o.mods & c.mods) == c.mods && (held & o.mods) == o.mods)
						return true;
			}
			return false;
		}

		/// While a menu has the pad (Gamepad::MenuMode) its bindings stand aside: it moves the menu's focus instead.
		int DefaultSources() { return Gamepad::MenuMode ? BindSources::KeyboardMouse : BindSources::All; }

		/// Listening: each stick direction pushed out on the last frame (a direction is taken only on a fresh push).
		bool stickWasOut[static_cast<int>(StickDir::Count)] = {};
	}

	void KeyBindings::Declare()
	{
		static_assert(static_cast<int>(KeyAction::Count) > 0, "no key actions");
		for (int i = 0; i < static_cast<int>(KeyAction::Count); i++) Binding(static_cast<KeyAction>(i));
	}

	const cs::Ref<KeyBindingSetting>& KeyBindings::Binding(KeyAction action)
	{
		static const std::vector<cs::Ref<KeyBindingSetting>> settings = []
		{
			std::vector<cs::Ref<KeyBindingSetting>> s;
			const auto& all = Actions();
			for (int i = 0; i < static_cast<int>(all.size()); i++)
			{
				const ActionInfo& a = all[i];
				if (static_cast<int>(a.action) != i) throw cs::InvalidOperationException(cs::Cat("Key action out of order: ", a.key));
				cs::String keywords = "key binding bindings rebind remap keyboard mouse gamepad controller";
				for (const InputBinding& c : a.defaults)
					if (!c.Empty()) keywords = cs::Cat(keywords, " ", c.Display());
				s.push_back(cs::New<KeyBindingSetting>(cs::Cat("controls.key.", a.key), Group, a.name, a.defaults, a.held, a.description, keywords));
			}
			if (static_cast<int>(s.size()) != static_cast<int>(KeyAction::Count)) throw cs::InvalidOperationException("Key actions out of step with the enum");
			return s;
		}();
		int i = static_cast<int>(action);
		return settings[i >= 0 && i < static_cast<int>(KeyAction::Count) ? i : 0];
	}

	const char* KeyBindings::Name(KeyAction action) { return Info(action).name; }

	cs::String KeyBindings::GroupOf(KeyAction action)
	{
		cs::String name = Info(action).name;
		size_t colon = name.find(':');
		return colon == cs::String::npos ? cs::String("Other") : name.substr(0, colon);
	}

	cs::String KeyBindings::ShortName(KeyAction action)
	{
		cs::String name = Info(action).name;
		size_t colon = name.find(':');
		cs::String label = colon == cs::String::npos ? name : cs::Strings::Trim(name.substr(colon + 1));
		if (!label.empty() && label[0] >= 'a' && label[0] <= 'z') label[0] = static_cast<char>(label[0] - 'a' + 'A');
		return label;
	}

	int KeyBindings::Context(KeyAction action) { return Info(action).context; }

	bool KeyBindings::HeldKind(KeyAction action) { return Info(action).held; }

	const BindingTuning& KeyBindings::Tuning()
	{
		static const BindingTuning tuning;
		return tuning;
	}

	int KeyBindings::HeldMods(const InputDevice& kb)
	{
		int m = KeyMods::None;
		if (kb[Key::LeftShift].isPressed || kb[Key::RightShift].isPressed) m |= KeyMods::Shift;
		if (kb[Key::LeftCtrl].isPressed || kb[Key::RightCtrl].isPressed) m |= KeyMods::Ctrl;
		if (kb[Key::LeftAlt].isPressed || kb[Key::RightAlt].isPressed) m |= KeyMods::Alt;
		return m;
	}

	bool KeyBindings::Held(KeyAction action, const InputDevice& kb) { return Held(action, kb, DefaultSources()); }

	bool KeyBindings::Pressed(KeyAction action, const InputDevice& kb) { return Pressed(action, kb, DefaultSources()); }

	bool KeyBindings::Held(KeyAction action, const InputDevice& kb, int sources)
	{
		const KeyBindingSetting& setting = *Binding(action);
		const GamepadDevice& pad = GamepadDevice::current();
		int held = kb.present ? HeldMods(kb) : 0;
		for (int i = 0; i < KeyBindingSetting::Slots; i++)
		{
			const InputBinding& b = setting.Value[i];
			if (b.Empty() || (SourceBit(b) & sources) == 0) continue;
			if (Evaluate(action, i, setting, kb, pad, held).held) return true;
		}
		return false;
	}

	bool KeyBindings::Pressed(KeyAction action, const InputDevice& kb, int sources)
	{
		const KeyBindingSetting& setting = *Binding(action);
		const GamepadDevice& pad = GamepadDevice::current();
		int held = kb.present ? HeldMods(kb) : 0;
		for (int i = 0; i < KeyBindingSetting::Slots; i++)
		{
			const InputBinding& b = setting.Value[i];
			if (b.Empty() || (SourceBit(b) & sources) == 0) continue;
			if (Evaluate(action, i, setting, kb, pad, held).pressed && (setting.HeldKind || !PreEmpted(action, b, held))) return true;
		}
		return false;
	}

	void KeyBindings::Tick()
	{
		const InputDevice& kb = InputDevice::current();
		const GamepadDevice& pad = GamepadDevice::current();
		int held = kb.present ? HeldMods(kb) : 0;
		for (int a = 0; a < static_cast<int>(KeyAction::Count); a++)
		{
			auto action = static_cast<KeyAction>(a);
			const KeyBindingSetting& setting = *Binding(action);
			for (int i = 0; i < KeyBindingSetting::Slots; i++)
				if (Stateful(setting.Value[i])) Evaluate(action, i, setting, kb, pad, held);
		}
	}

	cs::String KeyBindings::Keys(KeyAction action)
	{
		const KeyBindingSetting& setting = *Binding(action);
		bool pad = Gamepad::LastUsed;
		bool any = false;
		for (const InputBinding& c : setting.Value)
			if (!c.Empty() && c.IsPad() == pad) any = true;
		cs::String s;
		for (const InputBinding& c : setting.Value)
		{
			if (c.Empty() || (any && c.IsPad() != pad)) continue;
			if (!s.empty()) s += "  /  ";
			s += c.Display();
		}
		return s.empty() ? cs::String("unbound") : s;
	}

	void KeyBindings::BeginCapture(const GamepadDevice& pad)
	{
		for (int d = 0; d < static_cast<int>(StickDir::Count); d++)
			stickWasOut[d] = pad.present && StickValue(pad, d) >= Tuning().CaptureStick * 0.5f;
	}

	bool KeyBindings::Capture(const InputDevice& kb, const GamepadDevice& pad, InputBinding& binding)
	{
		if (kb.present)
		{
			int held = HeldMods(kb);
			for (int i = static_cast<int>(Key::None) + 1; i < static_cast<int>(Key::Count); i++)
			{
				auto k = static_cast<Key>(i);
				if (k == Key::Escape || IsModifierKey(k)) continue;
				if (kb[k].wasPressedThisFrame)
				{
					binding = InputBinding(k, held);
					return true;
				}
			}
			// The mouse: the side buttons, the middle and the wheel as they are; the left and right buttons only with a
			// modifier (alone they fire, select and look, and press the page's own buttons).
			for (MouseButton m : { MouseButton::Back, MouseButton::Forward, MouseButton::Middle, MouseButton::Left, MouseButton::Right })
			{
				if (!kb.Button(m).wasPressedThisFrame) continue;
				if ((m == MouseButton::Left || m == MouseButton::Right) && held == KeyMods::None) continue;
				binding = InputBinding::OfMouse(m, held);
				return true;
			}
			if (kb.wheelUp.wasPressedThisFrame || kb.wheelDown.wasPressedThisFrame)
			{
				binding = InputBinding::OfWheel(kb.wheelUp.wasPressedThisFrame, held);
				return true;
			}
			// A modifier let go of with nothing else pressed: the modifier itself (Shift to boost, Alt to flip).
			for (int i = static_cast<int>(Key::None) + 1; i < static_cast<int>(Key::Count); i++)
			{
				auto k = static_cast<Key>(i);
				if (IsModifierKey(k) && kb[k].wasReleasedThisFrame)
				{
					binding = InputBinding(k);
					return true;
				}
			}
		}
		if (pad.present)
		{
			for (int i = static_cast<int>(PadButton::None) + 1; i < static_cast<int>(PadButton::Count); i++)
			{
				auto b = static_cast<PadButton>(i);
				if (pad[b].wasPressedThisFrame)
				{
					binding = InputBinding::OfPad(b); // a trigger's click: the trigger at the pad's own threshold
					return true;
				}
			}
			for (int d = 0; d < static_cast<int>(StickDir::Count); d++)
			{
				float v = StickValue(pad, d);
				bool out = v >= Tuning().CaptureStick;
				bool fresh = out && !stickWasOut[d];
				if (v < Tuning().CaptureStick * 0.5f) stickWasOut[d] = false;
				else if (out) stickWasOut[d] = true;
				if (fresh)
				{
					binding = InputBinding::OfStick(static_cast<StickDir>(d));
					return true;
				}
			}
		}
		return false;
	}

	cs::List<KeyAction> KeyBindings::Conflicts(KeyAction action, const InputBinding& binding)
	{
		cs::List<KeyAction> found;
		if (binding.Empty()) return found;
		const ActionInfo& mine = Info(action);
		bool mineByDefault = Binding(action)->HasByDefault(binding);
		for (const ActionInfo& other : Actions())
		{
			if (other.action == action || (other.context & mine.context) == 0) continue;
			const auto& b = Binding(other.action);
			bool answers = false, byDefault = false;
			for (const InputBinding& o : b->Value)
				if (Answers(o, other.held, binding, mine.held))
				{
					answers = true;
					byDefault = byDefault || b->HasByDefault(o);
				}
			if (!answers) continue;
			if (mineByDefault && byDefault) continue; // shared on purpose, told apart by where each is live
			found.Add(other.action);
		}
		return found;
	}

	bool KeyBindings::AnyChanged()
	{
		for (int i = 0; i < static_cast<int>(KeyAction::Count); i++)
			if (!Binding(static_cast<KeyAction>(i))->IsDefault()) return true;
		return false;
	}

	KeyAction KeyBindings::ActionOf(const GameSetting& setting)
	{
		for (int i = 0; i < static_cast<int>(KeyAction::Count); i++)
			if (Binding(static_cast<KeyAction>(i)).get() == &setting) return static_cast<KeyAction>(i);
		return KeyAction::Count;
	}
}

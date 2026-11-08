// US-T249: the keyboard's action map (Input/KeyBindings.h).

#include "Input/KeyBindings.h"
#include "Settings/SettingsRegistry.h"

#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod::Input
{
	// ---------------------------------------------------------------- chords

	namespace
	{
		bool IsModifier(Key k)
		{
			return k == Key::LeftShift || k == Key::RightShift || k == Key::LeftCtrl || k == Key::RightCtrl || k == Key::LeftAlt || k == Key::RightAlt;
		}

		Key KeyNamed(const cs::String& name)
		{
			for (int i = static_cast<int>(Key::None) + 1; i < static_cast<int>(Key::Count); i++)
				if (name == ToString(static_cast<Key>(i))) return static_cast<Key>(i);
			return Key::None;
		}

		/// The key as a player reads it: the letter or symbol on the keycap.
		cs::String KeyDisplay(Key k)
		{
			switch (k)
			{
				case Key::Digit0: return "0"; case Key::Digit1: return "1"; case Key::Digit2: return "2"; case Key::Digit3: return "3";
				case Key::Digit4: return "4"; case Key::Digit5: return "5"; case Key::Digit6: return "6"; case Key::Digit7: return "7";
				case Key::Digit8: return "8"; case Key::Digit9: return "9";
				case Key::NumpadEnter: return "Numpad Enter";
				case Key::Escape: return "Esc";
				case Key::Backquote: return "`";
				case Key::Quote: return "'";
				case Key::Semicolon: return ";";
				case Key::Comma: return ",";
				case Key::Period: return ".";
				case Key::Slash: return "/";
				case Key::Backslash: return "\\";
				case Key::LeftBracket: return "[";
				case Key::RightBracket: return "]";
				case Key::Minus: return "-";
				case Key::Equals: return "=";
				case Key::NumpadPlus: return "Numpad +";
				case Key::NumpadMinus: return "Numpad -";
				case Key::NumpadMultiply: return "Numpad *";
				case Key::NumpadDivide: return "Numpad /";
				case Key::LeftShift: return "Left Shift";
				case Key::RightShift: return "Right Shift";
				case Key::LeftCtrl: return "Left Ctrl";
				case Key::RightCtrl: return "Right Ctrl";
				case Key::LeftAlt: return "Left Alt";
				case Key::RightAlt: return "Right Alt";
				case Key::Delete: return "Del";
				case Key::PageUp: return "PgUp";
				case Key::PageDown: return "PgDn";
				case Key::UpArrow: return "Up";
				case Key::DownArrow: return "Down";
				case Key::LeftArrow: return "Left";
				case Key::RightArrow: return "Right";
				default: return ToString(k);
			}
		}

		cs::String ModsPrefix(int mods)
		{
			cs::String s;
			if (mods & KeyMods::Ctrl) s += "Ctrl+";
			if (mods & KeyMods::Shift) s += "Shift+";
			if (mods & KeyMods::Alt) s += "Alt+";
			return s;
		}
	}

	cs::String KeyChord::Format() const
	{
		if (key == Key::None) return "None";
		return cs::Cat(ModsPrefix(mods), ToString(key));
	}

	cs::String KeyChord::Display() const
	{
		if (key == Key::None) return "";
		return cs::Cat(ModsPrefix(mods), KeyDisplay(key));
	}

	bool KeyChord::TryParse(const cs::String& text, KeyChord& chord)
	{
		cs::String t = cs::Strings::Trim(text);
		if (t == "None" || t.empty())
		{
			chord = KeyChord();
			return true;
		}
		int mods = KeyMods::None;
		for (;;)
		{
			if (cs::Strings::StartsWith(t, "Ctrl+")) { mods |= KeyMods::Ctrl; t = t.substr(5); }
			else if (cs::Strings::StartsWith(t, "Shift+")) { mods |= KeyMods::Shift; t = t.substr(6); }
			else if (cs::Strings::StartsWith(t, "Alt+")) { mods |= KeyMods::Alt; t = t.substr(4); }
			else break;
		}
		Key k = KeyNamed(t);
		if (k == Key::None || k == Key::Escape) return false;
		chord = KeyChord(k, mods);
		return true;
	}

	// ---------------------------------------------------------------- the setting

	KeyBindingSetting::KeyBindingSetting(const cs::String& key, const cs::String& group, const cs::String& label, const KeyChord (&defaults)[Slots],
		const cs::String& description, const cs::String& keywords)
		: GameSetting(key, group, label, description, keywords)
	{
		for (int i = 0; i < Slots; i++) Default[i] = defaults[i];
		SetToDefault();
		SettingsRegistry::Default()->RegisterDeclared(this);
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
		for (const KeyChord& c : Value)
		{
			if (c.Empty()) continue;
			if (!s.empty()) s += "  /  ";
			s += c.Display();
		}
		return s.empty() ? cs::String("unbound") : s;
	}

	cs::String KeyBindingSetting::Format() const
	{
		cs::String s;
		for (const KeyChord& c : Value)
		{
			if (c.Empty()) continue;
			if (!s.empty()) s += ", ";
			s += c.Format();
		}
		return s.empty() ? cs::String("None") : s;
	}

	bool KeyBindingSetting::TryLoad(const cs::String& text)
	{
		KeyChord read[Slots];
		int n = 0;
		for (const auto& part : cs::Strings::Split(text, ','))
		{
			KeyChord c;
			if (!KeyChord::TryParse(part, c)) return false;
			if (c.Empty()) continue;
			if (n >= Slots) return false;
			read[n++] = c;
		}
		for (int i = 0; i < Slots; i++) Value[i] = read[i];
		Compact();
		return true;
	}

	void KeyBindingSetting::SetToDefault()
	{
		for (int i = 0; i < Slots; i++) Value[i] = Default[i];
	}

	void KeyBindingSetting::Set(int slot, KeyChord chord)
	{
		if (slot < 0 || slot >= Slots) return;
		KeyChord before[Slots];
		for (int i = 0; i < Slots; i++) before[i] = Value[i];
		if (!chord.Empty())
			for (int i = 0; i < Slots; i++)
				if (i != slot && Value[i] == chord) Value[i] = KeyChord();
		Value[slot] = chord;
		Compact();
		bool changed = false;
		for (int i = 0; i < Slots; i++)
			if (Value[i] != before[i]) changed = true;
		if (changed) OnSet();
	}

	bool KeyBindingSetting::Has(const KeyChord& chord) const
	{
		if (chord.Empty()) return false;
		for (const KeyChord& c : Value)
			if (c == chord) return true;
		return false;
	}

	bool KeyBindingSetting::HasByDefault(const KeyChord& chord) const
	{
		if (chord.Empty()) return false;
		for (const KeyChord& c : Default)
			if (c == chord) return true;
		return false;
	}

	void KeyBindingSetting::Compact()
	{
		int n = 0;
		for (int i = 0; i < Slots; i++)
			if (!Value[i].Empty()) Value[n++] = Value[i];
		for (; n < Slots; n++) Value[n] = KeyChord();
	}

	// ---------------------------------------------------------------- the actions

	namespace
	{
		using namespace KeyContext;
		constexpr int S = KeyMods::Shift, A = KeyMods::Alt;
		constexpr bool HeldKind = true, EdgeKind = false;

		struct ActionInfo
		{
			KeyAction action;
			const char* key;
			const char* name;
			int context;
			bool held;
			KeyChord defaults[KeyBindingSetting::Slots];
			const char* description;
		};

		// In the page's order (the enum's); the defaults are the original's keys.
		const std::vector<ActionInfo>& Actions()
		{
			static const std::vector<ActionInfo> actions =
			{
				{ KeyAction::MoveForward, "moveForward", "Move: forward / thrust", Both, HeldKind, { Key::W }, "Swim forward in command; thrust at the controls (a capital's telegraph a step ahead on the press)." },
				{ KeyAction::MoveBack, "moveBack", "Move: back / retro", Both, HeldKind, { Key::S }, "Swim back in command; the retros at the controls (a capital's telegraph a step astern)." },
				{ KeyAction::StrafeLeft, "strafeLeft", "Move: strafe left", Both, HeldKind, { Key::A }, "Swim or strafe left (a capital ship turns)." },
				{ KeyAction::StrafeRight, "strafeRight", "Move: strafe right", Both, HeldKind, { Key::D }, "Swim or strafe right (a capital ship turns)." },
				{ KeyAction::Rise, "rise", "Move: up", Both, HeldKind, { Key::E, Key::RightBracket }, "Swim or strafe up." },
				{ KeyAction::Sink, "sink", "Move: down", Both, HeldKind, { Key::Q, Key::C }, "Swim or strafe down." },
				{ KeyAction::Boost, "boost", "Move: boost", Both, HeldKind, { Key::LeftShift, Key::RightShift }, "Held: boost (and the hard boost of a fighter)." },
				{ KeyAction::Slow, "slow", "Move: fine / docking", Both, HeldKind, { Key::LeftCtrl, Key::RightCtrl }, "Held: fine movement; a freighter's docking mode in a port's approach." },

				{ KeyAction::RollLeft, "rollLeft", "Flight: roll left", Controls, HeldKind, { Key::Z }, "Held: roll anticlockwise." },
				{ KeyAction::RollRight, "rollRight", "Flight: roll right", Controls, HeldKind, { Key::X }, "Held: roll clockwise." },
				{ KeyAction::Manoeuvre, "manoeuvre", "Flight: flip (manoeuvre)", Controls, HeldKind, { Key::LeftAlt, Key::RightAlt }, "Held: swing the nose onto the retrograde for a turn and burn; a capital's broadside stance." },
				{ KeyAction::Cruise, "cruise", "Flight: tactical cruise", Controls, HeldKind, { Key::J }, "Held: the stronger drive tier, out of a fight." },
				{ KeyAction::SystemCruise, "systemCruise", "Flight: cruise drive", Controls, EdgeKind, { KeyChord(Key::J, S) }, "Spool the in-system cruise drive for what the nose is on, or drop out of it." },
				{ KeyAction::Drift, "drift", "Flight: drift", Controls, HeldKind, { Key::R }, "A fighter's drift (hold, or toggle as Controls > Drift key sets it)." },
				{ KeyAction::FlightAssist, "flightAssist", "Flight: flight assist", Controls, EdgeKind, { Key::F }, "Flight assist on or off." },
				{ KeyAction::JumpJets, "jumpJets", "Flight: jump jets (mech)", Controls, HeldKind, { Key::Space }, "Held in a mech: the jump jets lift it off the plating and fly it in open space (the pause's Space is theirs while you pilot one)." },

				{ KeyAction::LockTarget, "lockTarget", "Combat: lock target", Controls, EdgeKind, { Key::T }, "Lock (or clear) the hull under the mouse or selected." },
				{ KeyAction::Tractor, "tractor", "Combat: tractor beam", Controls, EdgeKind, { KeyChord(Key::T, S) }, "The tractor beam on the nearest cargo pod in reach, or off." },
				{ KeyAction::DropMine, "dropMine", "Combat: drop mine", Controls, EdgeKind, { Key::LeftCtrl, Key::RightCtrl }, "In a fight: a mine dropped astern." },
				{ KeyAction::TunePhasers, "tunePhasers", "Combat: tune phasers", Controls, EdgeKind, { Key::Insert }, "Retune the phaser arrays: a narrow lance or a wide sweep." },
				{ KeyAction::RunHot, "runHot", "Combat: run hot", Controls, EdgeKind, { KeyChord(Key::Insert, S) }, "At a big ship's helm: the crew runs the guns and drive hot, or throttles down." },
				{ KeyAction::SensorPing, "sensorPing", "Sensors: active ping", Controls, EdgeKind, { Key::Tab }, "An active ping (at a void stop too)." },
				{ KeyAction::SilentRunning, "silentRunning", "Sensors: silent running", Both, EdgeKind, { Key::F3 }, "Silent running on or off." },
				{ KeyAction::PowerToggle, "powerToggle", "Sensors: go dark / power up", Controls, EdgeKind, { KeyChord(Key::F3, S) }, "Power down to go dark, or power up again (at a void stop, run dark)." },
				{ KeyAction::SelfDestruct, "selfDestruct", "Combat: self-destruct", Both, HeldKind, { KeyChord(Key::Backspace, S) }, "Held a second at the controls: arms the self-destruct; pressed during a countdown: aborts it." },

				{ KeyAction::PowerWeapons, "powerWeapons", "Reactor: a pip to weapons", Controls, EdgeKind, { Key::LeftArrow }, "Reactor power: a pip to weapons." },
				{ KeyAction::PowerShields, "powerShields", "Reactor: a pip to shields", Controls, EdgeKind, { Key::UpArrow }, "Reactor power: a pip to shields." },
				{ KeyAction::PowerEngines, "powerEngines", "Reactor: a pip to engines", Controls, EdgeKind, { Key::RightArrow }, "Reactor power: a pip to engines." },
				{ KeyAction::PowerBalance, "powerBalance", "Reactor: even share", Controls, EdgeKind, { Key::DownArrow }, "Reactor power: an even share." },
				{ KeyAction::PowerAllWeapons, "powerAllWeapons", "Reactor: all to weapons", Controls, EdgeKind, { KeyChord(Key::LeftArrow, S) }, "Reactor power: all it can spare to weapons." },
				{ KeyAction::PowerAllShields, "powerAllShields", "Reactor: all to shields", Controls, EdgeKind, { KeyChord(Key::UpArrow, S) }, "Reactor power: all it can spare to shields." },
				{ KeyAction::PowerAllEngines, "powerAllEngines", "Reactor: all to engines", Controls, EdgeKind, { KeyChord(Key::RightArrow, S) }, "Reactor power: all it can spare to engines." },

				{ KeyAction::ActivateGate, "activateGate", "Travel: jump gate / rejoin lane", Controls, EdgeKind, { Key::Y }, "Activate the jump gate in range; back into the lane where you left it." },
				{ KeyAction::Eject, "eject", "Travel: eject", Controls, EdgeKind, { KeyChord(Key::Y, S) }, "In a launched craft's cockpit: eject in a pressure suit." },
				{ KeyAction::EngageFtl, "engageFtl", "Travel: FTL spool", Controls, EdgeKind, { Key::Enter, Key::NumpadEnter }, "Spool the FTL drive for the star the nose is on, or call a charge off." },
				{ KeyAction::FtlAutopilot, "ftlAutopilot", "Travel: FTL autopilot", Controls, EdgeKind, { Key::End }, "The FTL autopilot for the plotted course, or the helm back." },
				{ KeyAction::NextFtlTarget, "nextFtlTarget", "Travel: plot next star", Controls, EdgeKind, { Key::PageDown }, "Plot a course to the next star or beacon out, nearest first." },
				{ KeyAction::PrevFtlTarget, "prevFtlTarget", "Travel: plot previous star", Controls, EdgeKind, { Key::PageUp }, "Plot a course to the star or beacon before." },
				{ KeyAction::ClearFtlCourse, "clearFtlCourse", "Travel: clear course", Controls, EdgeKind, { Key::Delete }, "Clear the plotted course." },
				{ KeyAction::RollPatches, "rollPatches", "Combat: roll shield patches", Controls, EdgeKind, { KeyChord(Key::Delete, S) }, "At a capital's helm: the helm assist rolls weak shield patches away from the heaviest fire, or stops." },

				{ KeyAction::Pilot, "pilot", "Seats: embody / let go", Both, EdgeKind, { Key::P }, "Embody what is selected or under the mouse; at any controls, let go." },
				{ KeyAction::EmbodyCrew, "embodyCrew", "Seats: EVA repair worker", Both, EdgeKind, { KeyChord(Key::P, S) }, "Out on the hull as its next EVA repair worker." },
				{ KeyAction::DeployMech, "deployMech", "Seats: mech out of the bay", Both, EdgeKind, { KeyChord(Key::N, S) }, "In a fight: the mech out of the bay of the hull held, flown or selected (a capital ship or station), you at its controls." },
				{ KeyAction::NextSeat, "nextSeat", "Seats: next seat", Both, EdgeKind, { Key::Quote }, "Take the next seat in the fight in view." },
				{ KeyAction::PrevSeat, "prevSeat", "Seats: previous seat", Both, EdgeKind, { Key::Semicolon }, "Take the seat before." },
				{ KeyAction::ManGun, "manGun", "Seats: man the next gun", Both, EdgeKind, { Key::Slash }, "Man the next gun on the hull flown or selected; past the last, back to the helm." },
				{ KeyAction::JumpToCraft, "jumpToCraft", "Seats: into a launched craft", Both, EdgeKind, { KeyChord(Key::Slash, S) }, "Into the newest craft the carrier launched, then the next." },
				{ KeyAction::MoveToHost, "moveToHost", "Seats: up to the helm", Controls, EdgeKind, { Key::Backspace }, "From a gun mount up to the helm of its hull." },
				{ KeyAction::Picker, "picker", "Seats: take-charge picker", Both, EdgeKind, { Key::F9 }, "Every seat on the thing in hand and around the camera, to click into." },
				{ KeyAction::Boarding, "boarding", "Combat: boarding panel", Controls, EdgeKind, { Key::B }, "At the controls: the boarding command panel." },

				{ KeyAction::Focus, "focus", "Command: focus", Command, EdgeKind, { Key::F }, "Focus the selection." },
				{ KeyAction::Home, "home", "Command: system view", Command, EdgeKind, { Key::Home, Key::Backspace }, "Back to the system view." },
				{ KeyAction::CycleShips, "cycleShips", "Command: next ship", Command, EdgeKind, { Key::Tab }, "Cycle the ships forward." },
				{ KeyAction::CycleBackward, "cycleBackward", "Command: previous ship", Command, EdgeKind, { KeyChord(Key::Tab, S) }, "Cycle the ships back." },
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
				{ KeyAction::SkyNext, "skyNext", "View: next skybox", Both, EdgeKind, { KeyChord(Key::K, S), Key::NumpadMultiply }, "The system in view on to its next sky." },
				{ KeyAction::SkyPrev, "skyPrev", "View: previous skybox", Both, EdgeKind, { KeyChord(Key::K, S | A), Key::NumpadDivide }, "The system in view back to the sky before." },
				{ KeyAction::ToggleHud, "hud", "View: HUD", Both, EdgeKind, { Key::U }, "The HUD's panels and readouts off or on." },
				{ KeyAction::ClearScreen, "clearScreen", "View: clear screen", Both, EdgeKind, { Key::F11 }, "Every piece of UI but the reticle off, and back." },

				{ KeyAction::Help, "help", "Screen: help", Both, EdgeKind, { Key::H, Key::F1 }, "The help screen." },
				{ KeyAction::Comms, "comms", "Screen: comms", Both, EdgeKind, { KeyChord(Key::H, S) }, "Hail: the comms panel." },
				{ KeyAction::Drawer, "drawer", "Screen: bottom drawer", Both, EdgeKind, { KeyChord(Key::U, S) }, "The bottom drawer up or down." },
				{ KeyAction::GalaxyMap, "galaxyMap", "Screen: galaxy map", Both, EdgeKind, { Key::LeftBracket }, "The galaxy map." },
				{ KeyAction::SystemMap, "systemMap", "Screen: system map", Both, EdgeKind, { KeyChord(Key::LeftBracket, S) }, "The system map." },
				{ KeyAction::Market, "market", "Screen: market", Both, EdgeKind, { Key::M }, "The market." },
				{ KeyAction::DockPort, "dockPort", "Screen: dock / port", Both, EdgeKind, { KeyChord(Key::M, S) }, "Docking clearance at the port alongside, then the port screen." },
				{ KeyAction::Cargo, "cargo", "Screen: cargo", Both, EdgeKind, { Key::I }, "The cargo hold." },
				{ KeyAction::Outfit, "outfit", "Screen: outfitting", Both, EdgeKind, { Key::F2 }, "Outfitting." },
				{ KeyAction::Bounties, "bounties", "Screen: bounty board", Both, EdgeKind, { Key::F4 }, "The bounty board of the port in view." },
				{ KeyAction::Contracts, "contracts", "Screen: contracts", Both, EdgeKind, { Key::F7 }, "The contracts board." },
				{ KeyAction::Fleet, "fleet", "Screen: fleet", Both, EdgeKind, { Key::F10 }, "Your fleet and property." },
				{ KeyAction::Trade, "trade", "Screen: trade desk", Both, EdgeKind, { Key::Backslash }, "The trade desk." },
				{ KeyAction::Hail, "hail", "Screen: envoy (hail polity)", Both, EdgeKind, { Key::F12 }, "Hail the polity of the ship locked, selected or hailing you." },
				{ KeyAction::Logs, "logs", "Screen: recovered logs", Both, EdgeKind, { KeyChord(Key::L, S) }, "The ship logs and station records recovered." },
				{ KeyAction::Discoveries, "discoveries", "Screen: discoveries", Both, EdgeKind, { KeyChord(Key::O, S) }, "Everything found, by kind and day." },
				{ KeyAction::SpawnPilot, "spawnPilot", "Screen: spawn & pilot", Both, EdgeKind, { Key::V }, "Pick a hull to spawn and pilot at the camera." },
				{ KeyAction::SpawnPractice, "spawnPractice", "Screen: practice fighter", Both, EdgeKind, { KeyChord(Key::V, S) }, "The practice fighter at the camera, you at its helm." },
				{ KeyAction::QuickSave, "quickSave", "Game: quicksave", Both, EdgeKind, { Key::F5 }, "Save to the quicksave slot." },
				{ KeyAction::QuickLoad, "quickLoad", "Game: quickload", Both, EdgeKind, { Key::F8 }, "Load the quicksave slot." },
				{ KeyAction::Saves, "saves", "Game: save slots", Both, EdgeKind, { Key::F6 }, "The save slots." },
				{ KeyAction::CheatMenu, "devMenu", "Game: dev menu", Both, EdgeKind, { KeyChord(Key::F1, S) }, "The dev menu." },
				{ KeyAction::SpawnMenu, "spawnMenu", "Game: dev spawn menu", Both, EdgeKind, { KeyChord(Key::F2, S) }, "The dev menu on its Spawn tab." },
			};
			return actions;
		}

		const ActionInfo& Info(KeyAction action)
		{
			const auto& all = Actions();
			int i = static_cast<int>(action);
			return all[i >= 0 && i < static_cast<int>(all.size()) ? i : 0];
		}

		bool ChordDown(const KeyChord& c, const InputDevice& kb, int held)
		{
			return !c.Empty() && kb[c.key].isPressed && (held & c.mods) == c.mods;
		}

		bool ChordPressed(const KeyChord& c, const InputDevice& kb, int held)
		{
			return !c.Empty() && kb[c.key].wasPressedThisFrame && (held & c.mods) == c.mods;
		}

		/// Another action live where <paramref name="action"/> is has a chord on the same key needing more of the modifiers
		/// held, and they are: the press is that one's.
		bool PreEmpted(KeyAction action, const KeyChord& c, int held)
		{
			int context = Info(action).context;
			for (const ActionInfo& other : Actions())
			{
				if (other.action == action || (other.context & context) == 0) continue;
				for (const KeyChord& o : KeyBindings::Binding(other.action)->Value)
					if (!o.Empty() && o.key == c.key && o.mods != c.mods && (o.mods & c.mods) == c.mods && (held & o.mods) == o.mods)
						return true;
			}
			return false;
		}
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
				cs::String keywords = "key binding bindings rebind remap keyboard";
				for (const KeyChord& c : a.defaults)
					if (!c.Empty()) keywords = cs::Cat(keywords, " ", c.Display());
				s.push_back(cs::New<KeyBindingSetting>(cs::Cat("controls.key.", a.key), Group, a.name, a.defaults, a.description, keywords));
			}
			if (static_cast<int>(s.size()) != static_cast<int>(KeyAction::Count)) throw cs::InvalidOperationException("Key actions out of step with the enum");
			return s;
		}();
		int i = static_cast<int>(action);
		return settings[i >= 0 && i < static_cast<int>(KeyAction::Count) ? i : 0];
	}

	const char* KeyBindings::Name(KeyAction action) { return Info(action).name; }

	int KeyBindings::Context(KeyAction action) { return Info(action).context; }

	int KeyBindings::HeldMods(const InputDevice& kb)
	{
		int m = KeyMods::None;
		if (kb[Key::LeftShift].isPressed || kb[Key::RightShift].isPressed) m |= KeyMods::Shift;
		if (kb[Key::LeftCtrl].isPressed || kb[Key::RightCtrl].isPressed) m |= KeyMods::Ctrl;
		if (kb[Key::LeftAlt].isPressed || kb[Key::RightAlt].isPressed) m |= KeyMods::Alt;
		return m;
	}

	bool KeyBindings::Held(KeyAction action, const InputDevice& kb)
	{
		if (!kb.present) return false;
		int held = HeldMods(kb);
		for (const KeyChord& c : Binding(action)->Value)
			if (ChordDown(c, kb, held)) return true;
		return false;
	}

	bool KeyBindings::Pressed(KeyAction action, const InputDevice& kb)
	{
		if (!kb.present) return false;
		int held = HeldMods(kb);
		bool heldKind = Info(action).held;
		for (const KeyChord& c : Binding(action)->Value)
			if (ChordPressed(c, kb, held) && (heldKind || !PreEmpted(action, c, held))) return true;
		return false;
	}

	cs::String KeyBindings::Keys(KeyAction action) { return Binding(action)->ValueText(); }

	bool KeyBindings::Capture(const InputDevice& kb, KeyChord& chord)
	{
		if (!kb.present) return false;
		int held = HeldMods(kb);
		for (int i = static_cast<int>(Key::None) + 1; i < static_cast<int>(Key::Count); i++)
		{
			auto k = static_cast<Key>(i);
			if (k == Key::Escape || IsModifier(k)) continue;
			if (kb[k].wasPressedThisFrame)
			{
				chord = KeyChord(k, held);
				return true;
			}
		}
		// A modifier let go of with nothing else pressed: the modifier itself (Shift to boost, Alt to flip).
		for (int i = static_cast<int>(Key::None) + 1; i < static_cast<int>(Key::Count); i++)
		{
			auto k = static_cast<Key>(i);
			if (IsModifier(k) && kb[k].wasReleasedThisFrame)
			{
				chord = KeyChord(k);
				return true;
			}
		}
		return false;
	}

	cs::List<KeyAction> KeyBindings::Conflicts(KeyAction action, const KeyChord& chord)
	{
		cs::List<KeyAction> found;
		if (chord.Empty()) return found;
		const ActionInfo& mine = Info(action);
		bool mineByDefault = Binding(action)->HasByDefault(chord);
		for (const ActionInfo& other : Actions())
		{
			if (other.action == action || (other.context & mine.context) == 0) continue;
			const auto& b = Binding(other.action);
			if (!b->Has(chord)) continue;
			if (mineByDefault && b->HasByDefault(chord)) continue; // shared on purpose, told apart by where each is live
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

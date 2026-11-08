#pragma once

// US-T249: the keyboard's action map. Every key InputState reads (and the few top-level keys read beside it: comms,
// the drawer, the void stop's sensors) is an action here with up to three key chords, each a key and the modifiers
// held with it (Shift, Ctrl, Alt). Each action's chords are one setting in the "Key bindings" group (Settings >
// Controls > Key bindings, UI/PauseMenu), kept per profile like every setting, with Reset per action and for the page.
// Defaults are the original's keys, so a profile that never rebinds plays exactly as before.
//
// Matching. A chord is down while its key is and all of its modifiers are held (more held is fine: Shift+W still thrusts,
// boosting). A press, though, goes to the most specific chord: when another action that can be live at the same time
// (their contexts overlap) has a chord on the same key needing more of the modifiers held, and they are, the press is
// that one's alone -- Shift+T is the tractor's, not the lock's; Shift+Alt+K the sky back, not forward. Held actions are
// never pre-empted this way (Shift+J held is still J held).
//
// Contexts. A key may mean one thing in command and another at the controls (F focus / flight assist, T reset time /
// lock, Tab cycle ships / ping, R reorient / drift, B watch / boarding, Backspace home / up to the helm, X news / roll):
// each action says where it is live, InputState::Arbitrate keeps only the owner's meanings, and the bindings page warns
// of a chord given to two actions live in the same place (unless both have it by default, as the pairs above do).
//
// US-T83: the mech's jump jets share Space with the pause by default: while a walker (a mech) is held the press is the
// jets' (InputState::GiveWalkerKeys), anywhere else the pause's, so the page does not call it a clash.
//
// Fixed: Esc (it backs out of everything, menus included), the number keys, the mouse, and the keys inside panels and
// menus (arrows, Enter, PgUp/PgDn in a list). The gamepad's buttons are its own map (Input/Gamepad.h, PadAction, under
// Settings > Controls) and feed the same InputState fields after the keys; a pad binding needs no KeyAction.
//
// No Unreal headers here: only UnrealInputDevice.cpp touches the engine.

#include "Input/InputDevice.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Input
{
	/// <summary>Every rebindable key action, in the bindings page's order.</summary>
	enum class KeyAction : int
	{
		// Move (the camera's swim in command, thrust and strafe at the controls)
		MoveForward, MoveBack, StrafeLeft, StrafeRight, Rise, Sink, Boost, Slow,
		// Flight
		RollLeft, RollRight, Manoeuvre, Cruise, SystemCruise, Drift, FlightAssist, JumpJets,
		// Combat and sensors
		LockTarget, Tractor, DropMine, TunePhasers, RunHot, SensorPing, SilentRunning, PowerToggle, SelfDestruct,
		// Reactor
		PowerWeapons, PowerShields, PowerEngines, PowerBalance, PowerAllWeapons, PowerAllShields, PowerAllEngines,
		// Travel
		ActivateGate, Eject, EngageFtl, FtlAutopilot, NextFtlTarget, PrevFtlTarget, ClearFtlCourse, RollPatches,
		// Seats
		Pilot, EmbodyCrew, DeployMech, NextSeat, PrevSeat, ManGun, JumpToCraft, MoveToHost, Picker, Boarding,
		// Command view
		Focus, Home, CycleShips, CycleBackward, WatchBattle, ResetTime, Reorient, News,
		// Time and scale
		TogglePause, TimeFaster, TimeSlower, ToggleScale,
		// View
		Orbits, Labels, Lanes, Satellites, SkyNext, SkyPrev, ToggleHud, ClearScreen,
		// Screens and panels
		Help, Comms, Drawer, GalaxyMap, SystemMap, Market, DockPort, Cargo, Outfit, Bounties, Contracts, Fleet, Trade, Hail,
		Logs, Discoveries, Encyclopedia, SpawnPilot, SpawnPractice, QuickSave, QuickLoad, Saves, CheatMenu, SpawnMenu,
		Count,
	};

	/// <summary>Modifier flags of a <see cref="KeyChord"/>.</summary>
	namespace KeyMods
	{
		constexpr int None = 0, Shift = 1, Ctrl = 2, Alt = 4;
	}

	/// <summary>A key and the modifiers held with it ("Shift+T"); <see cref="Key::None"/>: an empty slot.</summary>
	struct KeyChord
	{
		Key key = Key::None;
		int mods = KeyMods::None;

		KeyChord() = default;
		KeyChord(Key k, int m = KeyMods::None) : key(k), mods(m) {}

		bool Empty() const { return key == Key::None; }
		bool operator==(const KeyChord& o) const { return key == o.key && (key == Key::None || mods == o.mods); }
		bool operator!=(const KeyChord& o) const { return !(*this == o); }

		/// <summary>As a profile keeps it: "Shift+T", "Backquote", "None".</summary>
		cs::String Format() const;
		/// <summary>Takes back what <see cref="Format"/> wrote; false for anything else.</summary>
		static bool TryParse(const cs::String& text, KeyChord& chord);
		/// <summary>As the menu and Help print it: "Shift+T", "`", "Numpad +".</summary>
		cs::String Display() const;
	};

	/// <summary>Where an action is live (flags): the command view, at the controls of something embodied, or both.</summary>
	namespace KeyContext
	{
		constexpr int Command = 1, Controls = 2, Both = 3;
	}

	/// <summary>One action's keys (up to <see cref="Slots"/> chords): a setting of its own, kept in the profile as
	/// "Shift+K, NumpadMultiply" (or "None" when every slot is empty).</summary>
	class KeyBindingSetting final : public Core::GameSetting
	{
	public:
		static constexpr int Slots = 3;
		KeyChord Default[Slots];
		KeyChord Value[Slots];

		KeyBindingSetting(const cs::String& key, const cs::String& group, const cs::String& label, const KeyChord (&defaults)[Slots],
			const cs::String& description, const cs::String& keywords);

		/// No control kind of its own in Core: the bindings page draws its slots (UI/PauseMenu); a pick among keys.
		Core::SettingKind Kind() const override { return Core::SettingKind::Choice; }
		bool IsDefault() const override;
		cs::String ValueText() const override;
		cs::String Format() const override;
		bool TryLoad(const cs::String& text) override;
		void SetToDefault() override;

		/// <summary>Puts <paramref name="chord"/> in <paramref name="slot"/> (empty: clears it); the same chord in another
		/// slot is dropped, and the slots close up so the bound ones come first.</summary>
		void Set(int slot, KeyChord chord);

		bool Has(const KeyChord& chord) const;
		bool HasByDefault(const KeyChord& chord) const;

	private:
		void Compact();
	};

	class KeyBindings final
	{
	public:
		/// <summary>The settings group the bindings are in (listed under Controls).</summary>
		static constexpr const char* Group = "Key bindings";

		/// <summary>Builds every binding setting (the declaration source GameSettingsHost registers).</summary>
		static void Declare();

		static const cs::Ref<KeyBindingSetting>& Binding(KeyAction action);
		/// <summary>The action as the page and Help name it ("Lock target").</summary>
		static const char* Name(KeyAction action);
		static int Context(KeyAction action);

		/// <summary>The modifiers held now (either Shift, either Ctrl, either Alt).</summary>
		static int HeldMods(const InputDevice& kb);

		/// <summary>Any of the action's chords is down now.</summary>
		static bool Held(KeyAction action, const InputDevice& kb);
		/// <summary>One of the action's chords went down this frame (and no more specific chord took the press).</summary>
		static bool Pressed(KeyAction action, const InputDevice& kb);
		static bool Held(KeyAction action) { return Held(action, InputDevice::current()); }
		static bool Pressed(KeyAction action) { return Pressed(action, InputDevice::current()); }

		/// <summary>The action's keys as Help prints them ("G  /  `"; "unbound").</summary>
		static cs::String Keys(KeyAction action);

		/// <summary>The bindings page listening for a key: a chord pressed this frame (a non-modifier key with what is held,
		/// or a modifier let go of on its own). Esc is never taken (it cancels).</summary>
		static bool Capture(const InputDevice& kb, KeyChord& chord);

		/// <summary>The other actions live where <paramref name="action"/> is that <paramref name="chord"/> also triggers
		/// (pairs that share it by default left out: they are told apart by context).</summary>
		static cs::List<KeyAction> Conflicts(KeyAction action, const KeyChord& chord);

		/// <summary>Whether any binding stands off its default (Help then lists the changed ones).</summary>
		static bool AnyChanged();

		/// <summary>The binding setting's action (Count when it is not a binding).</summary>
		static KeyAction ActionOf(const Core::GameSetting& setting);
	};
}

#pragma once

// US-T249: the action map. Every key InputState reads (and the few top-level keys read beside it: comms, the drawer, the
// void stop's sensors) is an action here. US-T437: each action has three slots, Primary, Secondary and Tertiary, and a
// slot can hold any input (Input/BindingCore.h): a key with the modifiers held with it (Shift, Ctrl, Alt), a mouse button
// (the side buttons too) or a wheel step, a gamepad button, a trigger or a stick direction; and each slot has a trigger
// mode (press, hold, double-tap, long press; Auto, the action's own, by default). Each action's slots are one setting in
// the "Key bindings" group (Settings > Controls > Key bindings, UI/PauseMenu), kept per profile like every setting, with
// Reset per action and for the page. Defaults are the original's keys and the pad's buttons of before (the pad map,
// Input/Gamepad.h, folded into this table), so a profile that never rebinds plays exactly as before.
//
// Matching. A key or mouse binding is down while its input is and all of its modifiers are held (more held is fine:
// Shift+W still thrusts, boosting). A press, though, goes to the most specific binding: when another action that can be
// live at the same time (their contexts overlap) has a binding on the same input needing more of the modifiers held, and
// they are, the press is that one's alone -- Shift+T is the tractor's, not the lock's; Shift+Alt+K the sky back, not
// forward. Held actions are never pre-empted this way (Shift+J held is still J held). A pad binding has no modifiers.
//
// Contexts. A key may mean one thing in command and another at the controls (F focus / flight assist, T reset time /
// lock, Tab cycle ships / ping, R reorient / drift, B watch / boarding, Backspace home / up to the helm, X news / roll,
// pad Y focus / lock): each action says where it is live, InputState::Arbitrate keeps only the owner's meanings, and the
// bindings page warns of an input given to two actions live in the same place that one touch would fire both (unless both
// have it by default, as the pairs above do; a double-tap or long press on an input is told apart from its press).
//
// US-T83: the mech's jump jets share Space with the pause by default: while a walker (a mech) is held the press is the
// jets' (InputState::GiveWalkerKeys), anywhere else the pause's, so the page does not call it a clash.
//
// Fixed: Esc (it backs out of everything, menus included), the number keys, the left and right mouse buttons' fire, select
// and look (they bind only with a modifier held), the sticks' movement and aim, the d-pad's steps, and the keys inside
// panels and menus (arrows, Enter, PgUp/PgDn in a list). While a menu has the pad (Gamepad::MenuMode) no pad binding
// fires but the pause menu's.
//
// No Unreal headers here: only UnrealInputDevice.cpp touches the engine.

#include "Input/BindingCore.h"
#include "Input/InputDevice.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Input
{
	class GamepadDevice;

	/// <summary>Every rebindable action, in the bindings page's order (the page groups them by their name's prefix).</summary>
	enum class KeyAction : int
	{
		// Move (the camera's swim in command, thrust and strafe at the controls)
		MoveForward, MoveBack, StrafeLeft, StrafeRight, Rise, Sink, Boost, Slow,
		// Flight
		RollLeft, RollRight, Manoeuvre, Cruise, SystemCruise, Drift, FlightAssist, JumpJets,
		// Combat and sensors
		Fire /* US-T437 */, SecondaryFire /* US-T437 */, LockTarget, LockAttacker /* US-T307 */, Tractor, DropMine, TunePhasers, RunHot, SensorPing, SilentRunning, PowerToggle, SelfDestruct,
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
		Orbits, Labels, Lanes, Satellites, SkyNext, SkyPrev, ToggleHud, ClearScreen, HudMode /* US-T437 */, Layout1, Layout2, Layout3, Layout4, Layout5 /* US-T441 */,
		// Screens and panels
		PauseMenu /* US-T437 */, Help, Comms, Drawer, GalaxyMap, SystemMap, Market, DockPort, Cargo, Outfit, Bounties, Contracts, Fleet, Trade, Hail,
		Logs, Discoveries, Encyclopedia, Society /* US-T293 */, Notices /* US-T345 */, BattlePlot /* US-T367 */, SpawnPilot, SpawnPractice, QuickSave, QuickLoad, Rewind /* US-T99 */, HoldRewind /* US-T409 */, Saves, CheatMenu, SpawnMenu,
		Count,
	};

	/// <summary>Where an action is live (flags): the command view, at the controls of something embodied, or both.</summary>
	namespace KeyContext
	{
		constexpr int Command = 1, Controls = 2, Both = 3;
	}

	/// <summary>US-T437: which devices a read listens to (flags).</summary>
	namespace BindSources
	{
		constexpr int Keyboard = 1, Mouse = 2, Pad = 4, KeyboardMouse = Keyboard | Mouse, All = Keyboard | Mouse | Pad;
	}

	/// <summary>One action's bindings (up to <see cref="Slots"/>): a setting of its own, kept in the profile as
	/// "Shift+K, NumpadMultiply", "Mouse4, DoubleTap:Pad:A" (or "None" when every slot is empty; Input/BindingCore.h).</summary>
	class KeyBindingSetting final : public Core::GameSetting
	{
	public:
		static constexpr int Slots = BindingSlots;
		InputBinding Default[Slots];
		InputBinding Value[Slots];
		/// <summary>The action is held (thrust, boost, roll), not pressed (a toggle, a screen): its bindings' Auto mode.</summary>
		const bool HeldKind;

		KeyBindingSetting(const cs::String& key, const cs::String& group, const cs::String& label, const InputBinding (&defaults)[Slots],
			bool heldKind, const cs::String& description, const cs::String& keywords);

		/// <summary>"Primary", "Secondary", "Tertiary".</summary>
		static const char* SlotName(int slot);

		/// No control kind of its own in Core: the bindings page draws its slots (UI/PauseMenu); a pick among inputs.
		Core::SettingKind Kind() const override { return Core::SettingKind::Choice; }
		bool IsDefault() const override;
		cs::String ValueText() const override;
		cs::String Format() const override;
		bool TryLoad(const cs::String& text) override;
		void SetToDefault() override;

		/// <summary>Puts <paramref name="binding"/> in <paramref name="slot"/> (empty: clears it); the same binding in
		/// another slot is dropped, and the slots close up so the bound ones come first.</summary>
		void Set(int slot, InputBinding binding);
		/// <summary>The slot's trigger mode (Auto when it is the action's own).</summary>
		void SetTrigger(int slot, Trigger trigger);
		/// <summary>Every slot at once (as a profile's text would set them), remembered.</summary>
		void Assign(const InputBinding (&values)[Slots]);

		bool Has(const InputBinding& binding) const;
		bool HasByDefault(const InputBinding& binding) const;

		/// <summary>Bumped on every change: the timing kept for a stateful binding starts again.</summary>
		int Version = 0;

	private:
		void AfterEdit(const InputBinding (&before)[Slots]);
	};

	class KeyBindings final
	{
	public:
		/// <summary>The settings group the bindings are in (listed under Controls).</summary>
		static constexpr const char* Group = "Key bindings";

		/// <summary>Builds every binding setting (the declaration source GameSettingsHost registers).</summary>
		static void Declare();

		static const cs::Ref<KeyBindingSetting>& Binding(KeyAction action);
		/// <summary>The action as Help names it ("Combat: lock target").</summary>
		static const char* Name(KeyAction action);
		/// <summary>The page's group of the action (its name's prefix: "Combat"), and its name without it ("Lock target").</summary>
		static cs::String GroupOf(KeyAction action);
		static cs::String ShortName(KeyAction action);
		static int Context(KeyAction action);
		static bool HeldKind(KeyAction action);

		/// <summary>The trigger modes' and listening's tunables (Input/BindingCore.h).</summary>
		static const BindingTuning& Tuning();

		/// <summary>The modifiers held now (either Shift, either Ctrl, either Alt).</summary>
		static int HeldMods(const InputDevice& kb);

		/// <summary>Any of the action's bindings is down now (the pad's left out while a menu has it).</summary>
		static bool Held(KeyAction action, const InputDevice& kb);
		/// <summary>One of the action's bindings fired this frame (and no more specific binding took the press).</summary>
		static bool Pressed(KeyAction action, const InputDevice& kb);
		static bool Held(KeyAction action) { return Held(action, InputDevice::current()); }
		static bool Pressed(KeyAction action) { return Pressed(action, InputDevice::current()); }
		/// <summary>As above, from the devices in <paramref name="sources"/> only (<see cref="BindSources"/>), the pad's
		/// read whatever the menu mode.</summary>
		static bool Held(KeyAction action, const InputDevice& kb, int sources);
		static bool Pressed(KeyAction action, const InputDevice& kb, int sources);

		/// <summary>Once a frame, before anything reads an action: the timing of every stateful binding (a mode other than
		/// the action's own, or a pad stick or trigger read against a threshold) steps on, so a double-tap or long press is
		/// timed whether or not its action is read that frame.</summary>
		static void Tick();

		/// <summary>The action's bindings as a hint prints them ("G  /  `"; "unbound"): the pad's when the pad was used
		/// last, else the keyboard's and mouse's (all of them when the device used has none).</summary>
		static cs::String Keys(KeyAction action);

		/// <summary>The bindings page starts listening: what is held now (a stick pushed, a trigger pulled) is not taken
		/// until let go.</summary>
		static void BeginCapture(const GamepadDevice& pad);
		/// <summary>The bindings page listening: an input pressed this frame (a non-modifier key with what is held, or a
		/// modifier let go of on its own; a mouse button or wheel step, the left and right buttons only with a modifier; a
		/// pad button, trigger or stick pushed past BindingTuning::CaptureStick). Esc is never taken (it cancels).</summary>
		static bool Capture(const InputDevice& kb, const GamepadDevice& pad, InputBinding& binding);

		/// <summary>The other actions live where <paramref name="action"/> is that one touch of <paramref name="binding"/>
		/// would also fire (pairs that share it by default left out: they are told apart by context).</summary>
		static cs::List<KeyAction> Conflicts(KeyAction action, const InputBinding& binding);

		/// <summary>Whether any binding stands off its default (Help then lists the changed ones).</summary>
		static bool AnyChanged();

		/// <summary>The binding setting's action (Count when it is not a binding).</summary>
		static KeyAction ActionOf(const Core::GameSetting& setting);
	};
}

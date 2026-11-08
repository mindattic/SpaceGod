#pragma once

// US-T250: the gamepad. One more device under InputState beside the keyboard and mouse (Input/InputDevice.h): the sticks,
// the triggers and the buttons of an Xbox-style pad, filled once a frame by UnrealInputDevice::Feed (Unreal's Gamepad_*
// keys) and read by InputState::Poll into the same fields the keys feed, so nothing downstream knows which device
// flew. The left stick thrusts and strafes (swims, in command), the right stick aims (the string aim's look, US-T168,
// as the mouse turns it) or looks about in command, the triggers fire, the bumpers roll, the face buttons boost, drift,
// flip and lock; the d-pad shares the reactor at the controls and steps time and ships in command.
//
// Menus (MenuMode, set by the app while a menu, panel or screen is up, and on the main menu): the pad is the GUI's
// instead (UnityCompat/GuiPadNav): d-pad or left stick moves a visible focus between the controls, A presses, B (or Menu)
// is Esc, the right stick scrolls. The game then reads nothing else of the pad.
//
// Every action's button is a Controls setting (remappable from Settings > Controls, kept per profile), as are the dead
// zone, the aim sensitivity and an inverted aim. The Help screen shows the pad's buttons when the pad was used last
// (LastUsed).
//
// No Unreal headers here: only UnrealInputDevice.cpp touches the engine.

#include "Input/InputDevice.h"
#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;
	class ToggleSetting;
}

namespace SpaceGod::Input
{
	/// <summary>An Xbox-layout pad's buttons (Unreal's Gamepad_FaceButton_Bottom is A ...). <c>Count</c> is not a button.</summary>
	enum class PadButton : int
	{
		None,
		A, B, X, Y,
		LeftBumper, RightBumper, LeftTrigger, RightTrigger,
		View, Menu, LeftStick, RightStick,
		DPadUp, DPadDown, DPadLeft, DPadRight,
		Count,
	};

	/// <summary>The button's glyph as the Help screen and the settings print it ("A", "LB", "RT", "View" ...).</summary>
	const char* ToString(PadButton button);

	/// <summary>One frame of the pad: the sticks (raw, -1..1, y up: pushed away from the player is +), the triggers (0..1)
	/// and every button as a <see cref="ButtonControl"/> (a trigger is also a button, pulled past half way).</summary>
	class GamepadDevice final
	{
	public:
		/// <summary>False when there is nothing to read: everything then reads idle.</summary>
		bool present = false;
		Vector2 leftStick, rightStick;
		float leftTrigger = 0, rightTrigger = 0;

		const ButtonControl& operator[](PadButton button) const;
		ButtonControl& operator[](PadButton button);

		/// <summary>Everything idle and released; <see cref="present"/> left as it is.</summary>
		void Clear();

		/// <summary>Any button went down this frame, or a stick or trigger is pushed well past its dead zone.</summary>
		bool Touched() const;

		/// <summary>The one pad (the first connected; a second pad reads into the same).</summary>
		static GamepadDevice& current();

	private:
		ButtonControl buttons[static_cast<int>(PadButton::Count)];
		ButtonControl none;
	};

	/// <summary>The pad's remappable actions, in Settings > Controls' order.</summary>
	enum class PadAction : int
	{
		Fire, Secondary, RollLeft, RollRight, Boost, Drift, Flip, LockFocus, FlightAssist, Embody, Pause, Help,
		Count,
	};

	class Gamepad final
	{
	public:
		/// <summary>Controls: the sticks' dead zone (radial; what is inside reads 0, the rest is rescaled to start from 0).</summary>
		static const cs::Ref<Core::RangeSetting>& DeadZone();
		/// <summary>Controls: how fast the right stick turns the aim and the look at full deflection (180 degrees a second at 1x).</summary>
		static const cs::Ref<Core::RangeSetting>& AimSensitivity();
		/// <summary>Controls: pushing the right stick forward looks (and pitches) down instead of up.</summary>
		static const cs::Ref<Core::ToggleSetting>& InvertAim();
		/// <summary>Controls: the button an action is on.</summary>
		static const cs::Ref<Core::ChoiceSetting>& Binding(PadAction action);
		/// <summary>What the action does, as its setting and the Help screen name it.</summary>
		static const char* ActionName(PadAction action);

		/// <summary>Builds every pad setting (the declaration source GameSettingsHost registers).</summary>
		static void Declare();

		/// <summary>The button <paramref name="action"/> is bound to now (<see cref="PadButton::None"/>: unbound).</summary>
		static PadButton ButtonFor(PadAction action);
		/// <summary>The bound button's state this frame (never pressed when unbound).</summary>
		static const ButtonControl& Action(PadAction action);

		/// <summary>A raw stick through the dead zone (radial, rescaled), the result's length 0..1.</summary>
		static Vector2 Stick(Vector2 raw);
		/// <summary>The right stick as a mouse delta this frame (the counts the mouse look is tuned for): a rate, squared for
		/// fine aim near the centre, so holding the stick over swings the look on as a mouse moved on would.</summary>
		static Vector2 AimDelta(float dt);

		/// <summary>Degrees a second the look turns at full deflection, at 1x <see cref="AimSensitivity"/>.</summary>
		static constexpr float FullDeflectionDegS = 180.0f;

		/// <summary>The pad is the GUI's this frame (a menu, panel or screen is up; the main menu): set by the app's actor
		/// after each Update, read by InputState::Poll and UnityCompat/GuiPadNav the frame after.</summary>
		static inline bool MenuMode = false;

		/// <summary>The pad was the last device touched (the keyboard or mouse used since clears it): the focus highlight and
		/// the Help screen's pad buttons show only then.</summary>
		static inline bool LastUsed = false;
	};
}

// US-T250: the gamepad (Input/Gamepad.h).

#include "Input/Gamepad.h"
#include "Camera/SpaceCamera.h"
#include "Settings/GameSetting.h"

#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod::Input
{
	const char* ToString(PadButton button)
	{
		static const char* const names[] =
		{
			"None",
			"A", "B", "X", "Y",
			"LB", "RB", "LT", "RT",
			"View", "Menu", "LS", "RS",
			"D-pad up", "D-pad down", "D-pad left", "D-pad right",
			"Count",
		};
		static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(PadButton::Count) + 1, "PadButton names out of step with the enum");
		int i = static_cast<int>(button);
		return i >= 0 && i <= static_cast<int>(PadButton::Count) ? names[i] : "";
	}

	// ---------------------------------------------------------------- the device

	const ButtonControl& GamepadDevice::operator[](PadButton button) const
	{
		int i = static_cast<int>(button);
		return i > static_cast<int>(PadButton::None) && i < static_cast<int>(PadButton::Count) ? buttons[i] : none;
	}

	ButtonControl& GamepadDevice::operator[](PadButton button)
	{
		int i = static_cast<int>(button);
		if (i > static_cast<int>(PadButton::None) && i < static_cast<int>(PadButton::Count)) return buttons[i];
		none = ButtonControl();
		return none;
	}

	void GamepadDevice::Clear()
	{
		for (auto& b : buttons) b = ButtonControl();
		none = ButtonControl();
		leftStick = rightStick = Vector2::zero();
		leftTrigger = rightTrigger = 0;
	}

	bool GamepadDevice::Touched() const
	{
		if (!present) return false;
		for (int i = static_cast<int>(PadButton::None) + 1; i < static_cast<int>(PadButton::Count); i++)
			if (buttons[i].wasPressedThisFrame) return true;
		return leftStick.sqrMagnitude() > 0.25f || rightStick.sqrMagnitude() > 0.25f || leftTrigger > 0.5f || rightTrigger > 0.5f;
	}

	GamepadDevice& GamepadDevice::current()
	{
		static GamepadDevice device;
		return device;
	}

	// ---------------------------------------------------------------- settings

	namespace
	{
		/// The buttons a binding can pick, in the setting's order (the d-pad is the reactor's and the menus', not bindable),
		/// then None.
		const PadButton bindable[] =
		{
			PadButton::A, PadButton::B, PadButton::X, PadButton::Y,
			PadButton::LeftBumper, PadButton::RightBumper, PadButton::LeftTrigger, PadButton::RightTrigger,
			PadButton::View, PadButton::Menu, PadButton::LeftStick, PadButton::RightStick,
		};
		constexpr int bindableCount = static_cast<int>(sizeof(bindable) / sizeof(bindable[0]));

		const cs::List<cs::String>& BindingOptions()
		{
			static const cs::List<cs::String> options = []
			{
				cs::List<cs::String> o;
				for (PadButton b : bindable) o.Add(ToString(b));
				o.Add("None");
				return o;
			}();
			return options;
		}

		int OptionOf(PadButton button)
		{
			for (int i = 0; i < bindableCount; i++)
				if (bindable[i] == button) return i;
			return bindableCount; // None
		}

		struct ActionInfo
		{
			const char* key;
			const char* name;
			PadButton button;
			const char* description;
		};

		const ActionInfo actions[] =
		{
			{ "controls.pad.fire", "Fire", PadButton::RightTrigger, "Held: fires the guns at the controls (the left mouse button's)." },
			{ "controls.pad.secondary", "Secondary fire", PadButton::LeftTrigger, "Held: a seat's second trigger (the right mouse button's at the controls)." },
			{ "controls.pad.rollLeft", "Roll left / descend", PadButton::LeftBumper, "Held: rolls anticlockwise at the controls (Z); in command the camera sinks." },
			{ "controls.pad.rollRight", "Roll right / rise", PadButton::RightBumper, "Held: rolls clockwise at the controls (X); in command the camera rises." },
			{ "controls.pad.boost", "Boost", PadButton::A, "Held: boost (Shift)." },
			{ "controls.pad.drift", "Drift / back", PadButton::B, "At the controls: drift (R, hold or toggle as the drift key is set). In command: back out (Esc)." },
			{ "controls.pad.flip", "Flip (manoeuvre)", PadButton::X, "Held: the turn-and-burn's flip onto the retrograde (Alt)." },
			{ "controls.pad.lockFocus", "Lock target / focus", PadButton::Y, "At the controls: lock the target (T). In command: focus the selection (F)." },
			{ "controls.pad.flightAssist", "Flight assist", PadButton::RightStick, "At the controls: flight assist on or off (F)." },
			{ "controls.pad.embody", "Embody / let go", PadButton::LeftStick, "Take the controls of what is selected, or let go of them (P)." },
			{ "controls.pad.pause", "Pause menu", PadButton::Menu, "Esc: back out, and the pause menu." },
			{ "controls.pad.help", "Help", PadButton::View, "The help screen (H)." },
		};
		static_assert(sizeof(actions) / sizeof(actions[0]) == static_cast<size_t>(PadAction::Count), "pad actions out of step with the enum");
	}

	const cs::Ref<RangeSetting>& Gamepad::DeadZone()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("controls.pad.deadZone", SettingGroups::Controls,
			"Gamepad stick dead zone", 0.2, 0.05, 0.5, 0.01,
			"How far a stick moves before it counts: raise it if a worn stick drifts on its own.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::ToInt(cs::Math::Round(v * 100)), "%"); }),
			"gamepad controller stick deadzone drift");
		return value;
	}

	const cs::Ref<RangeSetting>& Gamepad::AimSensitivity()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("controls.pad.aimSensitivity", SettingGroups::Controls,
			"Gamepad aim sensitivity", 1.0, 0.25, 3.0, 0.05,
			"How fast the right stick turns the view (and a fighter's aim) at full deflection.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.00"), "x"); }),
			"gamepad controller stick aim look speed");
		return value;
	}

	const cs::Ref<ToggleSetting>& Gamepad::InvertAim()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("controls.pad.invertAim", SettingGroups::Controls,
			"Invert gamepad aim", false, "Pushing the right stick forward looks (and pitches) down instead of up.",
			"gamepad controller stick y pitch flight invert");
		return value;
	}

	const cs::Ref<ChoiceSetting>& Gamepad::Binding(PadAction action)
	{
		static const std::vector<cs::Ref<ChoiceSetting>> settings = []
		{
			std::vector<cs::Ref<ChoiceSetting>> s;
			for (const ActionInfo& a : actions)
				s.push_back(cs::New<ChoiceSetting>(a.key, SettingGroups::Controls, cs::Cat("Gamepad: ", a.name), BindingOptions(),
					OptionOf(a.button), a.description, "gamepad controller button binding remap"));
			return s;
		}();
		int i = static_cast<int>(action);
		return settings[i >= 0 && i < static_cast<int>(PadAction::Count) ? i : 0];
	}

	const char* Gamepad::ActionName(PadAction action)
	{
		int i = static_cast<int>(action);
		return i >= 0 && i < static_cast<int>(PadAction::Count) ? actions[i].name : "";
	}

	void Gamepad::Declare()
	{
		DeadZone();
		AimSensitivity();
		InvertAim();
		for (int i = 0; i < static_cast<int>(PadAction::Count); i++) Binding(static_cast<PadAction>(i));
	}

	// ---------------------------------------------------------------- reads

	PadButton Gamepad::ButtonFor(PadAction action)
	{
		int index = Binding(action)->Index;
		return index >= 0 && index < bindableCount ? bindable[index] : PadButton::None;
	}

	const ButtonControl& Gamepad::Action(PadAction action)
	{
		return GamepadDevice::current()[ButtonFor(action)];
	}

	Vector2 Gamepad::Stick(Vector2 raw)
	{
		float dead = static_cast<float>(DeadZone()->Value);
		float m = raw.magnitude();
		if (m <= dead || m <= 0.0001f) return Vector2::zero();
		float scaled = Mathf::Clamp01((m - dead) / Mathf::Max(0.0001f, 1.0f - dead));
		return raw * (scaled / m);
	}

	Vector2 Gamepad::AimDelta(float dt)
	{
		Vector2 s = Stick(GamepadDevice::current().rightStick);
		float m = s.magnitude();
		if (m <= 0) return Vector2::zero();
		// Squared response: a light touch for fine aim, full deflection for a fast swing.
		s = s * m;
		if (InvertAim()->Value) s.y = -s.y;
		float degPerCount = Mathf::Max(0.0001f, Cameras::SpaceCamera::LookSensitivity());
		float counts = FullDeflectionDegS * static_cast<float>(AimSensitivity()->Value) * dt / degPerCount;
		return s * counts;
	}
}

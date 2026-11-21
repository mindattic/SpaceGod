// US-T250: the gamepad (Input/Gamepad.h).

#include "Input/Gamepad.h"
#include "Camera/SpaceCamera.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Input
{
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

	void Gamepad::Declare()
	{
		DeadZone();
		AimSensitivity();
		InvertAim();
	}

	// ---------------------------------------------------------------- reads

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

#pragma once

// The keyboard and mouse through Unreal (US-T29), and the gamepad (US-T250, GamepadDevice::current() from the Gamepad_*
// keys and axes): fills InputDevice::current() once a frame from the local player
// controller's raw key polling (IsInputKeyDown, WasInputKeyJustPressed/Released, GetMousePosition, the mouse axes and
// the MouseWheelAxis key), before the app's Update polls it.
//
// Why polling and not Enhanced Input (as the C# track decided too): the original polls every key once a frame in one
// place (InputState::Poll) and resolves the shared keys itself (F is focus or flight assist, T reset time or lock
// target, B watch battle or boarding...). Polling keeps the bindings in code, in one table, exactly as the original's,
// with no InputAction / InputMappingContext assets per binding.
//
// Unity -> Unreal key names: backquote is Tilde, quote is Apostrophe, minus is Hyphen, the numpad keys are
// Add/Subtract/Multiply/Divide, and Unreal has one Enter for the main and numpad keys.

#include "Input/InputDevice.h"

class APlayerController;

namespace SpaceGod::Input
{
	class UnrealInputDevice final
	{
	public:
		/// <summary>Reads this frame's keyboard and mouse from <paramref name="pc"/> into <see cref="InputDevice::current"/>
		/// (null or invalid: no device, everything idle). Call once a frame, before the app's Update.</summary>
		static void Feed(APlayerController* pc, float dt);

		/// <summary>Unreal's raw mouse delta comes in input counts; pixels are what the original's sensitivities are
		/// tuned for (Unity's Mouse.delta on Windows is the same raw counts, so 1 by default).</summary>
		static inline float MouseDeltaScale = 1.0f;

		/// <summary>Unity's raw wheel units per notch (Windows' WHEEL_DELTA): Unreal reports the wheel in notches.</summary>
		static constexpr float WheelUnitsPerNotch = 120.0f;
	};
}

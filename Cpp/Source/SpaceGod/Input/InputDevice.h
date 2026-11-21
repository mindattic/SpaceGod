#pragma once

// The device seam under InputState (US-T29): Unity's Input System devices (Keyboard.current, Mouse.current) as one
// plain C++ snapshot per frame, so InputState.Poll reads line for line like the original (`kb[Key::W].isPressed`
// for `kb.wKey.isPressed`, `mouse.rightButton.wasPressedThisFrame`, `mouse.scrollY` for `mouse.scroll.ReadValue().y`).
// UnrealInputDevice::Feed fills it from the player controller once a frame, before the app's Update; a headless run,
// a tour or a test may write InputDevice::current() itself instead (the C# track's ScriptedInputDevice).
//
// No Unreal headers here: only UnrealInputDevice.cpp touches the engine.

#include "Input/BindingCore.h" // US-T437: Key, MouseButton, PadButton and the bindings' pure part
#include "UnityCompat/UnityTypes.h"

namespace SpaceGod::Input
{
	/// <summary>One key or mouse button this frame, as Unity's <c>ButtonControl</c> reports it: held, went down this
	/// frame, came up this frame (both edges can be set in one frame for a tap shorter than a frame).</summary>
	struct ButtonControl
	{
		bool isPressed = false;
		bool wasPressedThisFrame = false;
		bool wasReleasedThisFrame = false;
	};

	/// <summary>
	/// One frame of keyboard and mouse as the Unity Input System reported it: each key's and button's state; the
	/// pointer in Unity's screen space (pixels, origin bottom-left); its delta (pixels, y up); and the wheel in Unity's
	/// raw units (120 a notch on a standard Windows mouse; InputState scales it back to notches).
	/// </summary>
	class InputDevice final
	{
	public:
		/// <summary>False when there is no keyboard and mouse to read (Unity's <c>Keyboard.current == null</c> or
		/// <c>Mouse.current == null</c>): everything then reads idle.</summary>
		bool present = false;

		/// <summary><c>mouse.position.ReadValue()</c>: pixels, origin bottom-left.</summary>
		Vector2 position;

		/// <summary><c>mouse.delta.ReadValue()</c>: pixels this frame, y up.</summary>
		Vector2 delta;

		/// <summary><c>mouse.scroll.ReadValue().y</c>: Unity's raw wheel units, positive away from the player.</summary>
		float scrollY = 0;

		ButtonControl leftButton;
		ButtonControl rightButton;
		ButtonControl middleButton;

		/// <summary>US-T437: the side buttons (Unity's <c>backButton</c> / <c>forwardButton</c>: Mouse 4 and Mouse 5).</summary>
		ButtonControl backButton;
		ButtonControl forwardButton;

		/// <summary>US-T437: a wheel step this frame, as a button tapped within the frame (held, went down and came up all
		/// set on a frame the wheel turned that way), for a binding to the wheel.</summary>
		ButtonControl wheelUp;
		ButtonControl wheelDown;

		/// <summary><c>kb[key]</c> (the Input System's own indexer; <c>kb.wKey</c> is <c>kb[Key.W]</c>).</summary>
		const ButtonControl& operator[](Key key) const;
		ButtonControl& operator[](Key key);

		const ButtonControl& Button(MouseButton button) const;
		ButtonControl& Button(MouseButton button);

		/// <summary>Everything idle and released, the pointer kept where it was; <see cref="present"/> left as it is.</summary>
		void Clear();

		/// <summary>The one keyboard and mouse (Unity's <c>Keyboard.current</c> and <c>Mouse.current</c> together).</summary>
		static InputDevice& current();

	private:
		ButtonControl keys[static_cast<int>(Key::Count)];
		/// Handed out for Key::None, Key::Count and anything out of range: never pressed.
		ButtonControl none;
	};
}

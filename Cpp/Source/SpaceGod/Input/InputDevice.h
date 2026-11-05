#pragma once

// The device seam under InputState (US-T29): Unity's Input System devices (Keyboard.current, Mouse.current) as one
// plain C++ snapshot per frame, so InputState.Poll reads line for line like the original (`kb[Key::W].isPressed`
// for `kb.wKey.isPressed`, `mouse.rightButton.wasPressedThisFrame`, `mouse.scrollY` for `mouse.scroll.ReadValue().y`).
// UnrealInputDevice::Feed fills it from the player controller once a frame, before the app's Update; a headless run,
// a tour or a test may write InputDevice::current() itself instead (the C# track's ScriptedInputDevice).
//
// No Unreal headers here: only UnrealInputDevice.cpp touches the engine.

#include "UnityCompat/UnityTypes.h"

namespace SpaceGod::Input
{
	/// <summary>
	/// The keys the game reads, by the names of Unity Input System's <c>Key</c> members the original's bindings use
	/// (<c>kb.wKey</c> is <see cref="Key::W"/>, <c>kb.backquoteKey</c> is <see cref="Key::Backquote"/>...).
	/// <see cref="UnrealInputDevice"/> maps each onto its Unreal <c>FKey</c>. <c>Count</c> is not a key (the table size).
	/// </summary>
	enum class Key : int
	{
		None,
		A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
		Space, Enter, NumpadEnter, Escape, Tab, Backspace, Backquote, Quote, Semicolon, Comma, Period, Slash, Backslash,
		LeftBracket, RightBracket, Minus, Equals, NumpadPlus, NumpadMinus, NumpadMultiply, NumpadDivide,
		LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
		Insert, Delete, Home, End, PageUp, PageDown, Pause,
		UpArrow, DownArrow, LeftArrow, RightArrow,
		Count,
	};
	const char* ToString(Key key);

	enum class MouseButton : int
	{
		Left,
		Right,
		Middle,
	};
	const char* ToString(MouseButton button);

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

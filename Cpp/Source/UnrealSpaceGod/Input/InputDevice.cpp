#include "Input/InputDevice.h"

namespace SpaceGod::Input
{
	const char* ToString(Key key)
	{
		static const char* const names[] =
		{
			"None",
			"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
			"Digit0", "Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9",
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
			"Space", "Enter", "NumpadEnter", "Escape", "Tab", "Backspace", "Backquote", "Quote", "Semicolon", "Comma", "Period", "Slash", "Backslash",
			"LeftBracket", "RightBracket", "Minus", "Equals", "NumpadPlus", "NumpadMinus", "NumpadMultiply", "NumpadDivide",
			"LeftShift", "RightShift", "LeftCtrl", "RightCtrl", "LeftAlt", "RightAlt",
			"Insert", "Delete", "Home", "End", "PageUp", "PageDown", "Pause",
			"UpArrow", "DownArrow", "LeftArrow", "RightArrow",
			"Count",
		};
		static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(Key::Count) + 1, "Key names out of step with the enum");
		int i = static_cast<int>(key);
		return i >= 0 && i <= static_cast<int>(Key::Count) ? names[i] : "";
	}

	const char* ToString(MouseButton button)
	{
		switch (button)
		{
			case MouseButton::Left: return "Left";
			case MouseButton::Right: return "Right";
			case MouseButton::Middle: return "Middle";
		}
		return "";
	}

	const ButtonControl& InputDevice::operator[](Key key) const
	{
		int i = static_cast<int>(key);
		return i > static_cast<int>(Key::None) && i < static_cast<int>(Key::Count) ? keys[i] : none;
	}

	ButtonControl& InputDevice::operator[](Key key)
	{
		int i = static_cast<int>(key);
		if (i > static_cast<int>(Key::None) && i < static_cast<int>(Key::Count)) return keys[i];
		none = ButtonControl();
		return none;
	}

	const ButtonControl& InputDevice::Button(MouseButton button) const
	{
		return button == MouseButton::Left ? leftButton : button == MouseButton::Right ? rightButton : middleButton;
	}

	ButtonControl& InputDevice::Button(MouseButton button)
	{
		return button == MouseButton::Left ? leftButton : button == MouseButton::Right ? rightButton : middleButton;
	}

	void InputDevice::Clear()
	{
		for (auto& k : keys) k = ButtonControl();
		none = ButtonControl();
		leftButton = rightButton = middleButton = ButtonControl();
		delta = Vector2::zero();
		scrollY = 0;
	}

	InputDevice& InputDevice::current()
	{
		static InputDevice device;
		return device;
	}
}

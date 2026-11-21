#include "Input/InputDevice.h"

namespace SpaceGod::Input
{
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
		return const_cast<InputDevice*>(this)->Button(button);
	}

	ButtonControl& InputDevice::Button(MouseButton button)
	{
		switch (button)
		{
			case MouseButton::Left: return leftButton;
			case MouseButton::Right: return rightButton;
			case MouseButton::Back: return backButton; // US-T437
			case MouseButton::Forward: return forwardButton;
			default: return middleButton;
		}
	}

	void InputDevice::Clear()
	{
		for (auto& k : keys) k = ButtonControl();
		none = ButtonControl();
		leftButton = rightButton = middleButton = ButtonControl();
		backButton = forwardButton = wheelUp = wheelDown = ButtonControl(); // US-T437
		delta = Vector2::zero();
		scrollY = 0;
	}

	InputDevice& InputDevice::current()
	{
		static InputDevice device;
		return device;
	}
}

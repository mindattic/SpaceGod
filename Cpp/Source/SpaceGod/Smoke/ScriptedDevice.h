#pragma once

// The C# track's ScriptedInputDevice: keys held / tapped / pressed this frame, the mouse, written into
// InputDevice::current() before each app frame (the app actor's Feed is off while a script runs). Shared by the smoke
// (SpaceGodSmoke.cpp, US-T38) and the co-op smoke's client (Coop/CoopSmoke.cpp, US-T44).

#include "Input/InputDevice.h"
#include "UnityCompat/UnityTypes.h"

#include <set>

namespace SpaceGod::Smoke
{
	struct ScriptedDevice
	{
		std::set<Input::Key> held, pressed, tapped;
		std::set<Input::MouseButton> buttons, buttonsPressed, buttonsReleased;
		Vector2 mouseDelta;
		float scrollY = 0;

		void Hold(Input::Key k)
		{
			if (held.insert(k).second) pressed.insert(k);
		}
		void Release(Input::Key k) { held.erase(k); }
		/// Pressed (and held) for the next frame only.
		void Tap(Input::Key k)
		{
			pressed.insert(k);
			held.insert(k);
			tapped.insert(k);
		}
		void HoldButton(Input::MouseButton b)
		{
			if (buttons.insert(b).second) buttonsPressed.insert(b);
		}
		void ReleaseButton(Input::MouseButton b)
		{
			if (buttons.erase(b) > 0) buttonsReleased.insert(b);
		}

		void Write() const
		{
			auto& d = Input::InputDevice::current();
			d.Clear();
			d.present = true;
			d.position = Vector2(Screen::width / 2.0f, Screen::height / 2.0f);
			d.delta = mouseDelta;
			d.scrollY = scrollY;
			for (int k = 1; k < static_cast<int>(Input::Key::Count); k++)
			{
				auto key = static_cast<Input::Key>(k);
				auto& c = d[key];
				c.isPressed = held.count(key) > 0;
				c.wasPressedThisFrame = pressed.count(key) > 0;
			}
			for (Input::MouseButton b : { Input::MouseButton::Left, Input::MouseButton::Right, Input::MouseButton::Middle })
			{
				auto& c = d.Button(b);
				c.isPressed = buttons.count(b) > 0;
				c.wasPressedThisFrame = buttonsPressed.count(b) > 0;
				c.wasReleasedThisFrame = buttonsReleased.count(b) > 0;
			}
		}

		/// This frame's edges cleared (after the game has polled).
		void EndFrame()
		{
			pressed.clear();
			buttonsPressed.clear();
			buttonsReleased.clear();
			for (Input::Key k : tapped) held.erase(k);
			tapped.clear();
			mouseDelta = Vector2(0, 0);
			scrollY = 0;
		}
	};
}

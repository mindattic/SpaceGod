#include "Input/UnrealInputDevice.h"
#include "Input/Gamepad.h" // US-T250
#include "UnrealSpace.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"

namespace SpaceGod::Input
{
	namespace
	{
		/// Each Key's Unreal FKey (an invalid FKey for Key::None). Built on first use: EKeys' statics are only set up
		/// once InputCore has started, long before the first frame is fed.
		const TArray<FKey>& Map()
		{
			static const TArray<FKey> map = []
			{
				TArray<FKey> m;
				m.SetNum(static_cast<int>(Key::Count));
				auto set = [&m](Key k, const FKey& f) { m[static_cast<int>(k)] = f; };

				const FKey letters[] =
				{
					EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H, EKeys::I, EKeys::J, EKeys::K, EKeys::L, EKeys::M,
					EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S, EKeys::T, EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z,
				};
				for (int i = 0; i < 26; i++) set(static_cast<Key>(static_cast<int>(Key::A) + i), letters[i]);

				const FKey digits[] =
				{
					EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine,
				};
				for (int i = 0; i < 10; i++) set(static_cast<Key>(static_cast<int>(Key::Digit0) + i), digits[i]);

				const FKey functions[] =
				{
					EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8, EKeys::F9, EKeys::F10, EKeys::F11, EKeys::F12,
				};
				for (int i = 0; i < 12; i++) set(static_cast<Key>(static_cast<int>(Key::F1) + i), functions[i]);

				set(Key::Space, EKeys::SpaceBar);
				set(Key::Enter, EKeys::Enter);
				set(Key::NumpadEnter, EKeys::Enter); // Unreal reports the numpad's Enter as Enter
				set(Key::Escape, EKeys::Escape);
				set(Key::Tab, EKeys::Tab);
				set(Key::Backspace, EKeys::BackSpace);
				set(Key::Backquote, EKeys::Tilde);
				set(Key::Quote, EKeys::Apostrophe);
				set(Key::Semicolon, EKeys::Semicolon);
				set(Key::Comma, EKeys::Comma);
				set(Key::Period, EKeys::Period);
				set(Key::Slash, EKeys::Slash);
				set(Key::Backslash, EKeys::Backslash);
				set(Key::LeftBracket, EKeys::LeftBracket);
				set(Key::RightBracket, EKeys::RightBracket);
				set(Key::Minus, EKeys::Hyphen);
				set(Key::Equals, EKeys::Equals);
				set(Key::NumpadPlus, EKeys::Add);
				set(Key::NumpadMinus, EKeys::Subtract);
				set(Key::NumpadMultiply, EKeys::Multiply);
				set(Key::NumpadDivide, EKeys::Divide);
				set(Key::LeftShift, EKeys::LeftShift);
				set(Key::RightShift, EKeys::RightShift);
				set(Key::LeftCtrl, EKeys::LeftControl);
				set(Key::RightCtrl, EKeys::RightControl);
				set(Key::LeftAlt, EKeys::LeftAlt);
				set(Key::RightAlt, EKeys::RightAlt);
				set(Key::Insert, EKeys::Insert);
				set(Key::Delete, EKeys::Delete);
				set(Key::Home, EKeys::Home);
				set(Key::End, EKeys::End);
				set(Key::PageUp, EKeys::PageUp);
				set(Key::PageDown, EKeys::PageDown);
				set(Key::Pause, EKeys::Pause);
				set(Key::UpArrow, EKeys::Up);
				set(Key::DownArrow, EKeys::Down);
				set(Key::LeftArrow, EKeys::Left);
				set(Key::RightArrow, EKeys::Right);
				return m;
			}();
			return map;
		}

		void Read(APlayerController* pc, const FKey& key, ButtonControl& into)
		{
			into.isPressed = pc->IsInputKeyDown(key);
			into.wasPressedThisFrame = pc->WasInputKeyJustPressed(key);
			into.wasReleasedThisFrame = pc->WasInputKeyJustReleased(key);
		}

		/// US-T250: each PadButton's Unreal FKey (Xbox layout: FaceButton_Bottom is A, Special_Left is View ...).
		const TArray<FKey>& PadMap()
		{
			static const TArray<FKey> map = []
			{
				TArray<FKey> m;
				m.SetNum(static_cast<int>(PadButton::Count));
				auto set = [&m](PadButton b, const FKey& f) { m[static_cast<int>(b)] = f; };
				set(PadButton::A, EKeys::Gamepad_FaceButton_Bottom);
				set(PadButton::B, EKeys::Gamepad_FaceButton_Right);
				set(PadButton::X, EKeys::Gamepad_FaceButton_Left);
				set(PadButton::Y, EKeys::Gamepad_FaceButton_Top);
				set(PadButton::LeftBumper, EKeys::Gamepad_LeftShoulder);
				set(PadButton::RightBumper, EKeys::Gamepad_RightShoulder);
				set(PadButton::LeftTrigger, EKeys::Gamepad_LeftTrigger);
				set(PadButton::RightTrigger, EKeys::Gamepad_RightTrigger);
				set(PadButton::View, EKeys::Gamepad_Special_Left);
				set(PadButton::Menu, EKeys::Gamepad_Special_Right);
				set(PadButton::LeftStick, EKeys::Gamepad_LeftThumbstick);
				set(PadButton::RightStick, EKeys::Gamepad_RightThumbstick);
				set(PadButton::DPadUp, EKeys::Gamepad_DPad_Up);
				set(PadButton::DPadDown, EKeys::Gamepad_DPad_Down);
				set(PadButton::DPadLeft, EKeys::Gamepad_DPad_Left);
				set(PadButton::DPadRight, EKeys::Gamepad_DPad_Right);
				return m;
			}();
			return map;
		}

		/// US-T250: the pad, raw (InputState and the GUI apply the dead zone from the settings, not the input ini's).
		void FeedPad(APlayerController* pc)
		{
			GamepadDevice& pad = GamepadDevice::current();
			pad.present = true;
			const TArray<FKey>& map = PadMap();
			for (int i = static_cast<int>(PadButton::None) + 1; i < static_cast<int>(PadButton::Count); i++)
			{
				ButtonControl& control = pad[static_cast<PadButton>(i)];
				const FKey& key = map[i];
				if (key.IsValid()) Read(pc, key, control);
				else control = ButtonControl();
			}
			UPlayerInput* in = pc->PlayerInput;
			pad.leftStick = Vector2(in->GetRawKeyValue(EKeys::Gamepad_LeftX), in->GetRawKeyValue(EKeys::Gamepad_LeftY));
			pad.rightStick = Vector2(in->GetRawKeyValue(EKeys::Gamepad_RightX), in->GetRawKeyValue(EKeys::Gamepad_RightY));
			pad.leftTrigger = FMath::Clamp(in->GetRawKeyValue(EKeys::Gamepad_LeftTriggerAxis), 0.0f, 1.0f);
			pad.rightTrigger = FMath::Clamp(in->GetRawKeyValue(EKeys::Gamepad_RightTriggerAxis), 0.0f, 1.0f);
		}

		/// US-T250: the keyboard or the mouse touched this frame (a key or button down, the pointer moved, the wheel).
		bool KeyboardMouseTouched(const InputDevice& device)
		{
			for (int i = static_cast<int>(Key::None) + 1; i < static_cast<int>(Key::Count); i++)
				if (device[static_cast<Key>(i)].wasPressedThisFrame) return true;
			return device.leftButton.wasPressedThisFrame || device.rightButton.wasPressedThisFrame || device.middleButton.wasPressedThisFrame
				|| device.delta.sqrMagnitude() > 9.0f || device.scrollY != 0.0f;
		}
	}

	void UnrealInputDevice::Feed(APlayerController* pc, float dt)
	{
		(void)dt; // edges are Unreal's own per-frame ones; nothing here integrates over time
		InputDevice& device = InputDevice::current();
		if (pc == nullptr || !IsValid(pc) || pc->PlayerInput == nullptr)
		{
			device.present = false;
			device.Clear();
			GamepadDevice::current().present = false; // US-T250
			GamepadDevice::current().Clear();
			return;
		}
		device.present = true;

		const TArray<FKey>& map = Map();
		for (int i = static_cast<int>(Key::None) + 1; i < static_cast<int>(Key::Count); i++)
		{
			ButtonControl& control = device[static_cast<Key>(i)];
			const FKey& key = map[i];
			if (key.IsValid()) Read(pc, key, control);
			else control = ButtonControl();
		}
		Read(pc, EKeys::LeftMouseButton, device.leftButton);
		Read(pc, EKeys::RightMouseButton, device.rightButton);
		Read(pc, EKeys::MiddleMouseButton, device.middleButton);

		// Unreal: origin top-left; Unity: bottom-left. Off the viewport (or no viewport) the pointer stays where it was.
		float x = 0, y = 0;
		if (pc->GetMousePosition(x, y)) device.position = UnrealSpace::MouseToUnity(x, y, Screen::height);

		// The raw mouse axes (input counts, both y up), not GetInputMouseDelta: that one is the processed value, scaled
		// by the project's legacy axis config (MouseX/MouseY Sensitivity 0.07 in the default input ini), while Unity's
		// Mouse.delta is the raw counts the original's look sensitivity is tuned for.
		const float dx = pc->PlayerInput->GetRawKeyValue(EKeys::MouseX);
		const float dy = pc->PlayerInput->GetRawKeyValue(EKeys::MouseY);
		device.delta = Vector2(dx, dy) * MouseDeltaScale;

		// Unreal's wheel axis is a notch a unit; Unity's raw wheel is 120 a notch (InputState scales it back, so a notch
		// is one Scroll step on both). The scroll keys catch a frame whose axis was already consumed.
		float notches = pc->GetInputAnalogKeyState(EKeys::MouseWheelAxis);
		if (notches == 0.0f)
			notches = (pc->WasInputKeyJustPressed(EKeys::MouseScrollUp) ? 1.0f : 0.0f) - (pc->WasInputKeyJustPressed(EKeys::MouseScrollDown) ? 1.0f : 0.0f);
		device.scrollY = notches * WheelUnitsPerNotch;

		// US-T250: the pad, and which device was used last (the GUI's focus highlight and the Help screen's pad buttons).
		FeedPad(pc);
		if (GamepadDevice::current().Touched()) Gamepad::LastUsed = true;
		else if (KeyboardMouseTouched(device)) Gamepad::LastUsed = false;
	}
}

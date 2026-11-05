#pragma once

// The keyboard and mouse the UI polls outside OnGUI, standing in for the Unity Input System calls the original's UI
// makes there (MainMenu's and ModelViewer's `Keyboard.current.escapeKey.wasPressedThisFrame`, `Mouse.current.delta`).
// Fed by GuiHost's input processor (the same Slate events the GUI's Event passes are made from); a frame's presses
// are latched at the start of the frame, so every Update in that frame sees the same edges, as Unity's do.

#include "UnityCompat/UnityGui.h"

namespace SpaceGod::UI
{
	struct UiInput
	{
		/// `Keyboard.current != null`: there is a keyboard and mouse to read (false in a headless run).
		static bool present();
		/// `kb.<key>Key.wasPressedThisFrame`.
		static bool WasPressedThisFrame(KeyCode key);
		/// `kb.<key>Key.isPressed`.
		static bool IsPressed(KeyCode key);
		/// `mouse.leftButton / rightButton / middleButton .isPressed` (0, 1, 2).
		static bool IsMousePressed(int button);
		static bool WasMousePressedThisFrame(int button);
		/// `mouse.delta.ReadValue()`: pixels this frame, y up (Unity's screen space).
		static UnityCompat::Vector2 MouseDelta();
		/// `mouse.scroll.ReadValue().y`: Unity's raw units, 120 a notch, positive away from the player.
		static float ScrollY();
		/// `mouse.position.ReadValue()`: pixels, origin bottom-left (Unity's screen space).
		static UnityCompat::Vector2 MousePosition();
	};
}

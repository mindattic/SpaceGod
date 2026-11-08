#pragma once

// US-T250: the GUI by gamepad. Unity's IMGUI has no focus to move between buttons, so the host adds one: every enabled
// button, toggle and slider drawn in a frame's Repaint is a stop (its rect on the screen, cut to its clip), and while the
// pad is the GUI's (Input::Gamepad::MenuMode: a menu, panel or screen is up, or the main menu) the d-pad or the left
// stick moves a focus to the nearest stop that way, A presses the focused control, B (or the pause button) is Esc, and the
// right stick scrolls what is under the focus. A move with no stop that way scrolls instead, so a long list carries on.
// The focus is drawn as a ring round its control, while the pad is the device used last.
//
// How a press reaches the control: the host runs one more event pass at the start of the frame, a KeyDown carrying a
// pseudo key code (Unity's JoystickButton numbering, which no keyboard handler reads), and the control whose id is the
// focused one takes it (GUI::Button returns true, a toggle flips, a slider steps a twentieth of its range). Control ids
// are the frame's draw order, so a focus outlives a screen that adds or drops a control before it only by its place:
// a focus whose id is gone moves to the stop nearest where it was.

#include "UnityCompat/UnityGui.h"

#include <vector>

namespace SpaceGod::UnityCompat
{
	struct GuiPadNav
	{
		enum class Kind { Button, Slider };

		/// The pseudo keys of the host's pad events (Unity's KeyCode.JoystickButton0..2).
		static constexpr KeyCode Accept = static_cast<KeyCode>(330);
		static constexpr KeyCode Decrease = static_cast<KeyCode>(331);
		static constexpr KeyCode Increase = static_cast<KeyCode>(332);

		/// The pad is the GUI's this frame.
		static bool Engaged();

		/// Repaint: control `id` was drawn at `screen` (GUI units, cut to its clip) — a stop for the pad's focus. A control
		/// drawn at `local` (the current group's coordinates) then calls DrawFocus, which rings it when it has the focus.
		static void Register(int id, const Rect& screen, Kind kind);
		static void DrawFocus(int id, const Rect& local);

		/// KeyDown: this event is the pad pressing control `id`.
		static bool Pressed(const Event& e, int id);
		/// KeyDown: this event steps slider `id` (-1 down, 1 up, 0 not it).
		static int Nudge(const Event& e, int id);

		/// The host, at the start of a frame (before the queued input's passes): reads the pad, moves the focus over the last
		/// frame's stops and appends the events to run (`mouse`: the pointer, GUI units). `escape` is set when B or the pause
		/// button was pressed (the host also latches Esc for UiInput, as a key press would).
		static void BeginFrame(std::vector<Event>& events, Vector2 mouse, bool& escape);
	};
}

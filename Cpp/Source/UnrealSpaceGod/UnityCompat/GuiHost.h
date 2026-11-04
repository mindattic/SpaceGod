#pragma once

// The host of the Unity-style GUI (UnityGui.h) inside Unreal: what Unity's engine does around every MonoBehaviour's
// OnGUI. A full-screen, hit-test-invisible Slate widget over the game viewport runs, at paint time each frame,
// Unity's event loop over the registered OnGUI handlers (a Layout pass before every event; one pass per input event
// that came in since the last frame; then Layout + Repaint) and draws the Repaint pass with Slate: boxes (9-slice
// for a style's border), textures, text in Arial (the system's, as the C# track draws; Roboto without it) at Unity's pixel em, clip zones for groups / scroll views / clipped
// text, render transforms for GUI.matrix. Input comes from a Slate input pre-processor that never consumes anything
// (the game reads the same keys and decides with UnitySpaceApp.UiRects whether the pointer is over the UI, as the
// original does), and that also feeds UI::UiInput.
//
// Usage (the owners of OnGUI: UnitySpaceApp's actor, MainMenu's host, ModelViewer, AsteroidBelt):
//     GuiHost::Register(this, [this] { OnGUI(); });   // once, e.g. after Awake
//     GuiHost::Unregister(this);                       // in OnDestroy
// The widget is (re)installed on the game viewport automatically (a map load clears viewport widgets).

#include "UnityCompat/UnityGui.h"

#include <functional>
#include <utility>
#include <vector>

namespace SpaceGod::UnityCompat
{
	/// A scripted frame's keyboard and mouse (the C# track's GuiInput): the pointer in GUI pixels (origin top-left),
	/// buttons held / pressed / released this frame (left, right, middle), wheel notches (positive away from the
	/// player), keys that went down with the character each types.
	struct ScriptedGuiInput
	{
		Vector2 mouse = Vector2(-1000, -1000);
		bool held[3] = {}, pressed[3] = {}, released[3] = {};
		float wheelNotches = 0;
		std::vector<std::pair<KeyCode, char32_t>> keysDown;
		/// The modifier keys held (the C# GuiInput.Modifiers), on every event of the frame; kept until changed (US-T52).
		EventModifiers modifiers = EventModifiers::None;
		/// This frame's edges cleared (after the frame ran); held buttons and the pointer stay.
		void Clear();
	};

	struct GuiHost
	{
		/// One GUI frame over the registered handlers from scripted input, outside Slate's paint (the headless smoke):
		/// Unity's event passes then Layout + Repaint, measured but not drawn. GUI::Stats() and GUI::DrawnButtons() then
		/// describe it.
		static void ScriptedFrame(ScriptedGuiInput& input, float time);

		/// Adds an OnGUI handler; lower `order` runs first in each pass (Unity: GUI.depth / script order). Re-registering
		/// an owner replaces its handler.
		static void Register(const void* owner, std::function<void()> onGui, int order = 0);
		static void Unregister(const void* owner);

		/// Releases an Unreal texture made from a Texture2D's pixels (Texture2D's destructor calls it).
		static void ReleaseNativeTexture(void* native);

		/// Removes the overlay and the input processor (module shutdown).
		static void Shutdown();
	};
}

namespace SpaceGod
{
	using UnityCompat::GuiHost;
}
